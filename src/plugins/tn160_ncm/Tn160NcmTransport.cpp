#include "Tn160NcmTransport.h"
#include "Tn160NcmProtocolUtils.h"

#include <QtCore/QDateTime>
#include <QtCore/QByteArrayView>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace serein::scope::tn160ncm {
namespace {

constexpr int Temp8PayloadBytes = FrameWidth * FrameHeight;
constexpr int MetaBytes = 120;
constexpr int Adc16PayloadBytes = Temp8PayloadBytes * 2;
constexpr int RawWordsPerRow = FrameWidth + 2;
constexpr int RawFrameBytes = 2 + FrameHeight * RawWordsPerRow * 2 + 2;
constexpr int CalibrationPacketBytes = 64 + Temp8PayloadBytes * 2
                                     + Temp8PayloadBytes * 4 + 10;
constexpr int ThermalModelPacketBytes = 64;
constexpr quint8 Temp8PixelFormat = 5;
constexpr quint8 Adc16PixelFormat = 1;
constexpr quint32 MetaImageValid = 1u << 0;
constexpr quint32 MetaPayloadCrcValid = 1u << 5;
constexpr int InitialConnectRetryMs = 100;
constexpr int InitialConnectRequestTimeoutMs = 1000;
constexpr int ActiveFrameRequestTimeoutMs = 1500;
constexpr int ActiveFrameFailureLimit = 3;
constexpr int MinimumContinuousAdcIntervalMs = 250;

QVariantMap jsonMap(const QByteArray &body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    return document.isObject() ? document.object().toVariantMap()
                               : QVariantMap{};
}

bool isTn160V1Device(const QByteArray &body)
{
    const QVariantMap root = jsonMap(body);
    const QVariantMap api = root.value(QStringLiteral("api")).toMap();
    const QVariantMap device = root.value(QStringLiteral("device")).toMap();
    return api.value(QStringLiteral("name")).toString()
               == QStringLiteral("sipeed-thermal-http")
           && api.value(QStringLiteral("major")).toUInt() == 1u
           && device.value(QStringLiteral("model")).toString()
                  == QStringLiteral("T160");
}

bool validFrameRecord(const QByteArray &payload, const QByteArray &meta,
                      const int strideBytes, const quint8 pixelFormat)
{
    if (meta.size() != MetaBytes || std::memcmp(meta.constData(), "SITHMETA", 8)
            || quint8(meta.at(8)) != 1 || quint8(meta.at(9)) != 0) {
        return false;
    }
    const auto *bytes = reinterpret_cast<const uchar *>(meta.constData());
    const quint32 flags = qFromLittleEndian<quint32>(bytes + 12);
    return qFromLittleEndian<quint16>(bytes + 10) == MetaBytes
           && (flags & (MetaImageValid | MetaPayloadCrcValid))
                  == (MetaImageValid | MetaPayloadCrcValid)
           && qFromLittleEndian<quint32>(bytes + 28) == quint32(payload.size())
           && qFromLittleEndian<quint32>(bytes + 32)
                  == detail::crc32(QByteArrayView(payload.constData(), payload.size()))
           && qFromLittleEndian<quint16>(bytes + 36) == FrameWidth
           && qFromLittleEndian<quint16>(bytes + 38) == FrameHeight
           && qFromLittleEndian<quint16>(bytes + 40) == strideBytes
           && quint8(meta.at(42)) == pixelFormat
           && qFromLittleEndian<quint32>(bytes + 116)
                  == detail::crc32(QByteArrayView(meta.constData(), 116));
}

} // namespace

Tn160NcmTransport::Tn160NcmTransport(std::unique_ptr<IDataEngine> engine,
                                     QObject *parent)
    : QObject(parent), m_http(this), m_engine(std::move(engine))
{
    m_pollTimer.setSingleShot(true);
    m_connectRetryTimer.setSingleShot(true);
    m_initialRequestTimer.setSingleShot(true);
    m_connectDeadlineTimer.setSingleShot(true);
    m_activeFrameRequestTimer.setSingleShot(true);
    m_continuousAdcTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this,
            &Tn160NcmTransport::pollFrame);
    connect(&m_connectRetryTimer, &QTimer::timeout, this,
            &Tn160NcmTransport::retryInitialConnection);
    connect(&m_initialRequestTimer, &QTimer::timeout, this,
            &Tn160NcmTransport::initialRequestTimedOut);
    connect(&m_connectDeadlineTimer, &QTimer::timeout, this,
            &Tn160NcmTransport::initialConnectionTimedOut);
    connect(&m_activeFrameRequestTimer, &QTimer::timeout, this,
            &Tn160NcmTransport::activeFrameRequestTimedOut);
    connect(&m_continuousAdcTimer, &QTimer::timeout, this,
            &Tn160NcmTransport::pollContinuousAdc16);
    connect(&m_http, &serein::scope::HttpRequestClient::completed,
            this, &Tn160NcmTransport::handleHttpCompleted);
}

Tn160NcmTransport::~Tn160NcmTransport()
{
    shutdown();
}

bool Tn160NcmTransport::isActive() const noexcept
{
    return m_active;
}

bool Tn160NcmTransport::isConnecting() const noexcept
{
    return !m_active && (m_requestPending || m_connectRetryEnabled);
}

QString Tn160NcmTransport::endpoint() const
{
    return m_endpoint;
}

QString Tn160NcmTransport::deviceIdentity() const
{
    return m_deviceIdentity;
}

QString Tn160NcmTransport::deviceSerial() const
{
    return m_deviceSerial;
}

int Tn160NcmTransport::networkSegment() const noexcept
{
    return m_networkSegment;
}

void Tn160NcmTransport::setContinuousAdc16Enabled(const bool enabled,
                                                   const int intervalMs)
{
    m_continuousAdcEnabled = enabled;
    m_continuousAdcIntervalMs =
        std::max(intervalMs, MinimumContinuousAdcIntervalMs);
    if (!enabled) {
        m_continuousAdcTimer.stop();
        m_http.cancel(std::exchange(m_continuousAdcReply, 0));
        return;
    }
    if (m_active && m_mode == Mode::Monitor && !m_uploadInProgress
        && !m_continuousAdcReply) {
        m_continuousAdcTimer.start(0);
    }
}

bool Tn160NcmTransport::continuousAdc16Enabled() const noexcept
{
    return m_continuousAdcEnabled;
}

bool Tn160NcmTransport::replaceEngine(std::unique_ptr<IDataEngine> engine,
                                      QString *error)
{
    if (!engine) {
        if (error) *error = tr("数据引擎不能为空");
        return false;
    }
    m_engine = std::move(engine);
    const QString id = m_engine->id();
    m_mode = id.contains(QStringLiteral("raw-frame"), Qt::CaseInsensitive)
                 ? Mode::Raw
                 : Mode::Monitor;
    return true;
}

void Tn160NcmTransport::shutdown(const QByteArray &finalWrite)
{
    Q_UNUSED(finalWrite)
    close();
}

void Tn160NcmTransport::open(const QString &endpoint)
{
    beginOpen(endpoint, 0);
}

void Tn160NcmTransport::openWhenAvailable(const QString &endpoint,
                                          const int timeoutMs)
{
    beginOpen(endpoint, std::max(timeoutMs, 0));
}

void Tn160NcmTransport::beginOpen(const QString &endpoint,
                                  const int retryTimeoutMs)
{
    close();
    m_lastDiagnostics.clear();
    m_endpoint = detail::normalizeEndpoint(endpoint);
    m_deviceIdentity.clear();
    m_deviceSerial.clear();
    m_networkSegment = 0;
    m_consecutiveFailures = 0;
    m_connectRetryTimeoutMs = retryTimeoutMs;
    m_connectRetryEnabled = retryTimeoutMs > 0;
    if (m_connectRetryEnabled) {
        m_connectRetryElapsed.start();
        m_connectDeadlineTimer.start(retryTimeoutMs);
    }
    m_http.clearConnectionCache();
    setState(QStringLiteral("connecting"));
    request(QStringLiteral("/api/v1/device"));
}

void Tn160NcmTransport::close()
{
    const bool wasConnectedOrConnecting = m_active || isConnecting();
    m_pollTimer.stop();
    m_connectRetryTimer.stop();
    m_initialRequestTimer.stop();
    m_connectDeadlineTimer.stop();
    m_activeFrameRequestTimer.stop();
    m_continuousAdcTimer.stop();
    m_http.cancel(std::exchange(m_reply, 0));
    m_http.cancel(std::exchange(m_diagnosticsReply, 0));
    m_http.cancel(std::exchange(m_uploadReply, 0));
    m_http.cancel(std::exchange(m_thermalModelUploadReply, 0));
    m_http.cancel(std::exchange(m_thermalModelClearReply, 0));
    m_http.cancel(std::exchange(m_thermalModelReadReply, 0));
    m_http.cancel(std::exchange(m_thermalModelStateReply, 0));
    m_http.cancel(std::exchange(m_networkReply, 0));
    m_http.cancel(std::exchange(m_continuousAdcReply, 0));
    m_requestPending = false;
    m_connectRetryEnabled = false;
    m_connectRetryTimeoutMs = 0;
    m_uploadInProgress = false;
    m_networkTargetSegment = 0;
    m_active = false;
    if (wasConnectedOrConnecting) {
        setState(QStringLiteral("disconnected"));
    }
}

void Tn160NcmTransport::retryInitialConnection()
{
    if (!m_connectRetryEnabled || m_active || m_requestPending) return;
    m_http.clearConnectionCache();
    request(QStringLiteral("/api/v1/device"));
}

void Tn160NcmTransport::initialRequestTimedOut()
{
    if (!m_connectRetryEnabled || m_active || !m_requestPending || !m_reply) {
        return;
    }
    m_http.cancel(std::exchange(m_reply, 0));
    m_requestPending = false;
    m_http.clearConnectionCache();
    if (m_connectRetryElapsed.isValid()
        && m_connectRetryElapsed.elapsed() < m_connectRetryTimeoutMs) {
        setState(QStringLiteral("reconnecting"));
        m_connectRetryTimer.start(InitialConnectRetryMs);
    }
}

void Tn160NcmTransport::initialConnectionTimedOut()
{
    if (!m_connectRetryEnabled || m_active) return;
    m_connectRetryTimer.stop();
    m_initialRequestTimer.stop();
    m_http.cancel(std::exchange(m_reply, 0));
    m_requestPending = false;
    m_connectRetryEnabled = false;
    setState(QStringLiteral("disconnected"));
    emit errorOccurred(tr("NCM HTTP 请求失败：等待 USB 网络恢复超时"));
}

void Tn160NcmTransport::activeFrameRequestTimedOut()
{
    if (!m_active || !m_requestPending || !m_reply) return;
    m_http.cancel(std::exchange(m_reply, 0));
    m_requestPending = false;
    handleActiveFrameFailure(tr("在线帧请求超过 %1 ms 无响应")
                                 .arg(ActiveFrameRequestTimeoutMs));
}

void Tn160NcmTransport::handleActiveFrameFailure(const QString &message)
{
    if (!m_active) return;
    if (++m_consecutiveFailures >= ActiveFrameFailureLimit) {
        terminateActiveConnection(
            tr("NCM 连续 %1 次请求失败，设备已断开：%2")
                .arg(ActiveFrameFailureLimit)
                .arg(message));
        return;
    }
    m_pollTimer.start(0);
}

void Tn160NcmTransport::terminateActiveConnection(const QString &message)
{
    if (!m_active) return;
    m_active = false;
    m_pollTimer.stop();
    m_connectRetryTimer.stop();
    m_initialRequestTimer.stop();
    m_connectDeadlineTimer.stop();
    m_activeFrameRequestTimer.stop();
    m_continuousAdcTimer.stop();
    m_http.cancel(std::exchange(m_reply, 0));
    m_http.cancel(std::exchange(m_diagnosticsReply, 0));
    m_http.cancel(std::exchange(m_uploadReply, 0));
    m_http.cancel(std::exchange(m_thermalModelUploadReply, 0));
    m_http.cancel(std::exchange(m_thermalModelClearReply, 0));
    m_http.cancel(std::exchange(m_thermalModelReadReply, 0));
    m_http.cancel(std::exchange(m_thermalModelStateReply, 0));
    m_http.cancel(std::exchange(m_networkReply, 0));
    m_http.cancel(std::exchange(m_continuousAdcReply, 0));
    m_requestPending = false;
    m_connectRetryEnabled = false;
    m_connectRetryTimeoutMs = 0;
    m_uploadInProgress = false;
    m_networkTargetSegment = 0;
    emit errorOccurred(message);
    setState(QStringLiteral("disconnected"));
}

void Tn160NcmTransport::closeAfterWrites(const QByteArray &finalWrite,
                                         int drainDelayMs)
{
    Q_UNUSED(finalWrite)
    if (drainDelayMs > 0) {
        QTimer::singleShot(drainDelayMs, this, &Tn160NcmTransport::close);
    } else {
        close();
    }
}

void Tn160NcmTransport::write(const QByteArray &data)
{
    if (data.isEmpty()) return;
    const quint8 command = quint8(data.at(0));
    if (command == 0xaa) {
        m_mode = Mode::Raw;
        m_continuousAdcTimer.stop();
        m_http.cancel(std::exchange(m_continuousAdcReply, 0));
        m_pollTimer.start(0);
    } else if (command == 0x11) {
        m_mode = Mode::Monitor;
        m_pollTimer.start(0);
        if (m_continuousAdcEnabled && !m_uploadInProgress) {
            m_continuousAdcTimer.start(m_continuousAdcIntervalMs);
        }
    } else if (command == 0x99) {
        m_pollTimer.stop();
    }
    emit bytesWritten(data.size());
}

void Tn160NcmTransport::resetDecoder()
{
    m_lastDiagnostics.clear();
    if (m_engine) m_engine->reset();
}

void Tn160NcmTransport::setDecodingEnabled(const bool enabled)
{
    m_decodingEnabled = enabled;
}

void Tn160NcmTransport::ingest(const QByteArray &data)
{
    Q_UNUSED(data)
}

QUrl Tn160NcmTransport::url(const QString &path) const
{
    return QUrl(m_endpoint + (path.startsWith(QLatin1Char('/')) ? path
                                                                  : QStringLiteral("/") + path));
}

void Tn160NcmTransport::request(const QString &path)
{
    if (!m_decodingEnabled || m_requestPending) return;
    m_requestPending = true;
    m_reply = m_http.get(url(path));
    if (!m_active && m_connectRetryEnabled
        && path == QStringLiteral("/api/v1/device")) {
        m_initialRequestTimer.start(InitialConnectRequestTimeoutMs);
    }
    if (m_active && (path == QStringLiteral("/frame.temp8")
                     || path == QStringLiteral("/frame.adc16"))) {
        m_activeFrameRequestTimer.start(ActiveFrameRequestTimeoutMs);
    }
}

void Tn160NcmTransport::pollFrame()
{
    if (!m_active) return;
    request(m_mode == Mode::Raw ? QStringLiteral("/frame.adc16")
                                : QStringLiteral("/frame.temp8"));
}

void Tn160NcmTransport::refreshDiagnostics()
{
    if (!m_active || m_diagnosticsReply) return;
    m_diagnosticsReply =
        m_http.get(url(QStringLiteral("/api/v1/diagnostics")));
}

void Tn160NcmTransport::pollContinuousAdc16()
{
    if (!m_active || !m_continuousAdcEnabled || m_mode != Mode::Monitor
        || m_uploadInProgress || m_continuousAdcReply) {
        return;
    }
    m_continuousAdcReply =
        m_http.get(url(QStringLiteral("/frame.adc16")));
}

void Tn160NcmTransport::handleFrame(const QByteArray &record)
{
    if (m_mode == Mode::Raw) handleRawFrame(record);
    else handleMonitorFrame(record);
}

void Tn160NcmTransport::handleMonitorFrame(const QByteArray &record)
{
    if (record.size() != Temp8PayloadBytes + MetaBytes) return;
    const qint64 timestampUs = QDateTime::currentMSecsSinceEpoch() * 1000;
    const QByteArray image = record.left(Temp8PayloadBytes);
    const QByteArray meta = record.mid(Temp8PayloadBytes);
    if (!validFrameRecord(image, meta, FrameWidth, Temp8PixelFormat)) return;
    emit bytesReceived(record.size());
    emitMonitorPackets(image, timestampUs, m_lastDiagnostics, meta);
    refreshDiagnostics();
}

void Tn160NcmTransport::emitMonitorPackets(const QByteArray &image,
                                           const qint64 timestampUs,
                                           const QVariantMap &diagnostics,
                                           const QByteArray &meta)
{
    const auto *metaBytes = reinterpret_cast<const uchar *>(meta.constData());
    const quint32 metaFrameNumber = qFromLittleEndian<quint32>(metaBytes + 16);
    const quint32 deviceTimestampMs = qFromLittleEndian<quint32>(metaBytes + 24);
    const quint32 calibrationId = qFromLittleEndian<quint32>(metaBytes + 92);
    const quint16 ffcCount = qFromLittleEndian<quint16>(metaBytes + 98);
    emit monitorFrameMetadata(timestampUs, metaFrameNumber,
                              deviceTimestampMs, calibrationId, ffcCount);
    const quint16 vtemp = jsonU16(diagnostics, QStringLiteral("vtemp_adu"),
                                  8192);
    const qint16 low = jsonI16(diagnostics, QStringLiteral("temperature_low_x10"));
    const qint16 high = jsonI16(diagnostics, QStringLiteral("temperature_high_x10"));
    const qint16 centerRaw = jsonI16(diagnostics, QStringLiteral("center_raw_x10"));
    const qint16 centerRoi = jsonI16(diagnostics, QStringLiteral("center_roi_x10"));
    const QVector<double> values{
        centerRoi == 0x7fff ? std::numeric_limits<double>::quiet_NaN()
                            : centerRoi / 10.0,
        ntcCelsius(jsonU16(diagnostics, QStringLiteral("ntc_adu"))),
        double(jsonU16(diagnostics, QStringLiteral("center_roi_adu"))),
        double(vtemp),
        centerRaw == 0x7fff ? std::numeric_limits<double>::quiet_NaN()
                            : centerRaw / 10.0,
        double(jsonU16(diagnostics, QStringLiteral("center_raw_adu"))),
        double(jsonU16(diagnostics, QStringLiteral("ntc_adu"))),
        double(jsonU16(diagnostics, QStringLiteral("ntc_reference_adu"))),
        low == 0x7fff ? std::numeric_limits<double>::quiet_NaN() : low / 10.0,
        high == 0x7fff ? std::numeric_limits<double>::quiet_NaN() : high / 10.0,
        double(jsonU16(diagnostics, QStringLiteral("smooth_low_adu"))),
        double(jsonU16(diagnostics, QStringLiteral("smooth_high_adu"))),
        jsonDouble(diagnostics, QStringLiteral("mean_diff")),
        double(jsonU16(diagnostics, QStringLiteral("smooth_vtemp_adu"))),
        double(diagnostics.value(QStringLiteral("nuc_decision")).toUInt()),
        double(diagnostics.value(QStringLiteral("nuc_count")).toUInt()),
        double(jsonI16(diagnostics, QStringLiteral("nuc_dg"), 0)),
        double(jsonI16(diagnostics, QStringLiteral("nuc_dv"), 0)),
        double(jsonI16(diagnostics, QStringLiteral("nuc_dn"), 0)),
        double(jsonU16(diagnostics, QStringLiteral("boot_ntc_adu"))),
        double(jsonU16(diagnostics, QStringLiteral("boot_ntc_delta_adu"), 0xffff)),
        double(jsonI16(diagnostics, QStringLiteral("warmup_comp_adu"), 0)),
        double(jsonI16(diagnostics, QStringLiteral("normal_comp_adu"), 0)),
        double(jsonI16(diagnostics, QStringLiteral("total_comp_adu"), 0)),
        double(jsonI32(diagnostics, QStringLiteral("anchor_adu"))),
        double(jsonI32(diagnostics, QStringLiteral("effective_anchor_adu"))),
        diagnostics.contains(QStringLiteral("thermal_ntc_model_valid"))
            ? 1.0 : -1.0,
        diagnostics.value(QStringLiteral("thermal_ntc_model_valid")).toBool()
            ? 1.0 : 0.0,
        double(jsonU16(diagnostics,
                       QStringLiteral("thermal_ntc_model_reference_adu"))),
        double(jsonI16(diagnostics, QStringLiteral("thermal_ntc_model_k_q8"), 0)),
        double(jsonU16(diagnostics,
                       QStringLiteral("thermal_ntc_model_limit_adu"))),
        diagnostics.value(QStringLiteral("thermal_ntc_model_active")).toBool()
            ? 1.0 : 0.0,
        diagnostics.value(QStringLiteral("thermal_startup_model_valid")).toBool()
            ? 1.0 : 0.0,
        diagnostics.value(QStringLiteral("thermal_startup_model_active")).toBool()
            ? 1.0 : 0.0,
        double(jsonU16(diagnostics,
                       QStringLiteral("thermal_startup_model_ntc_threshold_adu"))),
        double(jsonI16(diagnostics,
                       QStringLiteral("thermal_startup_model_amplitude_adu"), 0)),
        double(jsonU16(diagnostics,
                       QStringLiteral("thermal_startup_model_tau_ds"))),
        double(diagnostics.value(QStringLiteral("ffc_state")).toUInt()),
        double(jsonU16(diagnostics, QStringLiteral("row_ref_mean_adu"))),
        double(jsonU16(diagnostics, QStringLiteral("row_ref_span_adu"))),
        double(jsonU16(diagnostics, QStringLiteral("shutter_mean_adu"))),
        diagnostics.contains(QStringLiteral("thermal_row_ref_model_valid"))
            ? 1.0 : -1.0,
        diagnostics.value(QStringLiteral("thermal_row_ref_model_valid")).toBool()
            ? 1.0 : 0.0,
        diagnostics.value(QStringLiteral("thermal_row_ref_model_active")).toBool()
            ? 1.0 : 0.0,
        diagnostics.value(QStringLiteral("thermal_row_ref_sample_valid")).toBool()
            ? 1.0 : 0.0,
        double(jsonU16(diagnostics,
                       QStringLiteral("thermal_row_ref_model_reference_adu"))),
        double(jsonI16(diagnostics,
                       QStringLiteral("thermal_row_ref_model_k_q8"), 0)),
        double(jsonU16(diagnostics,
                       QStringLiteral("thermal_row_ref_model_limit_adu"))),
        double(jsonI16(diagnostics,
                       QStringLiteral("thermal_row_ref_comp_adu"), 0)),
    };
    emit packetsDecoded(PacketList{
        ImagePacket{timestampUs, FrameWidth, FrameHeight, image, QStringLiteral("gray8"),
                    QString::fromLatin1(Tn160MonitorSource)},
        SamplePacket{timestampUs, values,
                     {QStringLiteral("Center 5×5 °C"), QStringLiteral("NTC °C"),
                      QStringLiteral("Center 5×5 ADU"), QStringLiteral("VTEMP ADU"),
                      QStringLiteral("Center raw °C"), QStringLiteral("Center raw ADU"),
                      QStringLiteral("NTC ADU"), QStringLiteral("NTC ref ADU"),
                      QStringLiteral("T low °C"), QStringLiteral("T high °C"),
                      QStringLiteral("Smooth low ADU"), QStringLiteral("Smooth high ADU"),
                      QStringLiteral("Mean diff"), QStringLiteral("Smooth VTEMP ADU"),
                      QStringLiteral("NUC decision"), QStringLiteral("NUC count"),
                      QStringLiteral("NUC dG"), QStringLiteral("NUC dV"),
                      QStringLiteral("NUC dN"), QStringLiteral("Boot NTC ADU"),
                      QStringLiteral("Boot delta ADU"), QStringLiteral("Warmup comp"),
                      QStringLiteral("Normal comp"), QStringLiteral("Total comp"),
                      QStringLiteral("Anchor"), QStringLiteral("Effective anchor"),
                      QStringLiteral("NTC model reported"),
                      QStringLiteral("NTC model valid"),
                      QStringLiteral("NTC model ref ADU"),
                      QStringLiteral("NTC model K Q8"),
                      QStringLiteral("NTC model limit ADU"),
                      QStringLiteral("NTC model active"),
                      QStringLiteral("Startup model valid"),
                      QStringLiteral("Startup model active"),
                      QStringLiteral("Startup NTC gate ADU"),
                      QStringLiteral("Startup amplitude ADU"),
                      QStringLiteral("Startup tau ds"),
                      QStringLiteral("FFC state"),
                      QStringLiteral("Row ref mean ADU"),
                      QStringLiteral("Row ref span ADU"),
                      QStringLiteral("Shutter mean ADU"),
                      QStringLiteral("RR model reported"),
                      QStringLiteral("RR model valid"),
                      QStringLiteral("RR model active"),
                      QStringLiteral("RR sample valid"),
                      QStringLiteral("RR reference ADU"),
                      QStringLiteral("RR K Q8"),
                      QStringLiteral("RR limit ADU"),
                      QStringLiteral("RR comp ADU")},
                     QString::fromLatin1(Tn160MonitorSource)}});
}

void Tn160NcmTransport::handleRawFrame(const QByteArray &record)
{
    if (record.size() != Adc16PayloadBytes + MetaBytes) return;
    const QByteArray payload = record.left(Adc16PayloadBytes);
    const QByteArray meta = record.mid(Adc16PayloadBytes);
    if (!validFrameRecord(payload, meta, FrameWidth * 2, Adc16PixelFormat)) {
        return;
    }
    const QVariantMap diagnostics = m_lastDiagnostics;
    QByteArray raw(RawFrameBytes, char(0));
    raw[0] = char(0xff);
    raw[1] = char(0xff);
    const auto *src = reinterpret_cast<const uchar *>(payload.constData());
    for (int y = 0; y < FrameHeight; ++y) {
        const int row = 2 + y * RawWordsPerRow * 2;
        for (int x = 0; x < FrameWidth; ++x) {
            const quint16 value = qFromLittleEndian<quint16>(src + (y * FrameWidth + x) * 2);
            qToBigEndian(value, reinterpret_cast<uchar *>(raw.data() + row + x * 2));
        }
    }
    const quint16 vtemp = jsonU16(diagnostics, QStringLiteral("vtemp_adu"), 8192);
    qToBigEndian(vtemp, reinterpret_cast<uchar *>(raw.data() + RawFrameBytes - 2));
    emit bytesReceived(record.size());
    if (m_engine) {
        const ParseResult parsed = m_engine->feed(QByteArrayView(raw));
        if (!parsed.packets.isEmpty()) emit packetsDecoded(parsed.packets);
    }
    refreshDiagnostics();
}

void Tn160NcmTransport::uploadCalibration(
    const QByteArray &packet, const bool completeProduction)
{
    if (!m_active) {
        emit calibrationUploadFinished(false, {}, tr("NCM 未连接"));
        return;
    }
    if (m_uploadInProgress) {
        emit calibrationUploadFinished(false, {}, tr("已有标定上传正在进行"));
        return;
    }
    if (packet.size() != CalibrationPacketBytes) {
        emit calibrationUploadFinished(
            false, {},
            tr("TNC1 标定包长度异常：%1 / %2 bytes")
                .arg(packet.size()).arg(CalibrationPacketBytes));
        return;
    }
    m_uploadInProgress = true;
    m_pollTimer.stop();
    m_activeFrameRequestTimer.stop();
    m_continuousAdcTimer.stop();
    m_http.cancel(std::exchange(m_continuousAdcReply, 0));
    HttpRequestClient::Headers headers{
        {"Content-Type", "application/vnd.sipeed.tn160-calibration-v1"},
        {"X-Sipeed-Request", "1"},
        {"X-Sipeed-Calibration-Confirm", "write-v1"},
    };
    if (completeProduction) {
        headers.append(QPair<QByteArray, QByteArray>{
            QByteArray("X-Sipeed-Final-Segment"), QByteArray("17")});
    }
    m_uploadReply = m_http.post(
        url(completeProduction
                ? QStringLiteral("/api/v1/calibration/v1/complete")
                : QStringLiteral("/api/v1/calibration/v1")),
        packet, headers);
}

void Tn160NcmTransport::uploadThermalModel(const QByteArray &packet)
{
    if (!m_active) {
        emit thermalModelUploadFinished(false, {}, tr("NCM 未连接"));
        return;
    }
    if (m_uploadInProgress || m_thermalModelUploadReply
        || packet.size() != ThermalModelPacketBytes) {
        emit thermalModelUploadFinished(
            false, {}, packet.size() != ThermalModelPacketBytes
                ? tr("热模型长度异常：%1 / %2 bytes")
                      .arg(packet.size()).arg(ThermalModelPacketBytes)
                : tr("已有热模型上传正在进行"));
        return;
    }
    m_uploadInProgress = true;
    m_pollTimer.stop();
    m_activeFrameRequestTimer.stop();
    m_continuousAdcTimer.stop();
    m_http.cancel(std::exchange(m_reply, 0));
    m_http.cancel(std::exchange(m_diagnosticsReply, 0));
    m_http.cancel(std::exchange(m_continuousAdcReply, 0));
    m_requestPending = false;
    m_thermalModelUploadReply = m_http.post(
        url(QStringLiteral("/api/v1/calibration/v1/thermal-model")),
        packet,
        {{"Content-Type", "application/vnd.sipeed.tn160-thermal-drift-model-v2"},
         {"X-Sipeed-Request", "1"},
         {"X-Sipeed-Thermal-Model-Confirm", "write-v2"}});
}

void Tn160NcmTransport::clearThermalModel()
{
    if (!m_active) {
        emit thermalModelClearFinished(false, {}, tr("NCM 未连接"));
        return;
    }
    if (m_uploadInProgress || m_thermalModelUploadReply
        || m_thermalModelClearReply) {
        emit thermalModelClearFinished(false, {}, tr("已有热模型事务正在进行"));
        return;
    }
    m_uploadInProgress = true;
    m_pollTimer.stop();
    m_activeFrameRequestTimer.stop();
    m_continuousAdcTimer.stop();
    m_http.cancel(std::exchange(m_reply, 0));
    m_http.cancel(std::exchange(m_diagnosticsReply, 0));
    m_http.cancel(std::exchange(m_continuousAdcReply, 0));
    m_requestPending = false;
    m_thermalModelClearReply = m_http.post(
        url(QStringLiteral("/api/v1/calibration/v1/thermal-model/clear")),
        QByteArray{},
        {{"Content-Type", "application/vnd.sipeed.tn160-thermal-drift-model-v2"},
         {"X-Sipeed-Request", "1"},
         {"X-Sipeed-Thermal-Model-Confirm", "clear-v2"}});
}

void Tn160NcmTransport::requestThermalModel()
{
    if (!m_active || m_thermalModelReadReply) return;
    m_thermalModelReadReply = m_http.get(
        url(QStringLiteral("/api/v1/calibration/v1/thermal-model")));
}

void Tn160NcmTransport::requestThermalModelState()
{
    if (!m_active || m_thermalModelStateReply) return;
    m_thermalModelStateReply = m_http.get(
        url(QStringLiteral("/api/v1/calibration/v1/thermal-model/state")));
}

void Tn160NcmTransport::setNetworkSegment(const int segment)
{
    if (!m_active) {
        emit networkSegmentChangeFinished(false, segment, tr("NCM 未连接"));
        return;
    }
    if (m_networkReply || m_uploadInProgress || segment < 1 || segment > 254) {
        emit networkSegmentChangeFinished(
            false, segment,
            segment < 1 || segment > 254
                ? tr("网段必须在 1 到 254 之间")
                : tr("设备正忙，不能修改网段"));
        return;
    }
    m_pollTimer.stop();
    m_activeFrameRequestTimer.stop();
    m_continuousAdcTimer.stop();
    m_http.cancel(std::exchange(m_continuousAdcReply, 0));
    m_http.cancel(std::exchange(m_reply, 0));
    m_http.cancel(std::exchange(m_diagnosticsReply, 0));
    m_requestPending = false;
    m_networkTargetSegment = segment;
    const QByteArray body =
        QJsonDocument(QJsonObject{{QStringLiteral("segment"), segment}})
            .toJson(QJsonDocument::Compact);
    m_networkReply = m_http.put(
        url(QStringLiteral("/api/v1/network")), body,
        {{"Content-Type", "application/json"},
         {"X-Sipeed-Request", "1"}});
}

void Tn160NcmTransport::handleHttpCompleted(
    const serein::scope::HttpRequestClient::RequestId requestId,
    const int statusCode, const QByteArray &body, const QString &error)
{
    if (requestId == m_continuousAdcReply) {
        m_continuousAdcReply = 0;
        if (m_active && error.isEmpty() && statusCode == 200
            && body.size() == Adc16PayloadBytes + MetaBytes) {
            const QByteArray payload = body.left(Adc16PayloadBytes);
            const QByteArray meta = body.mid(Adc16PayloadBytes);
            if (validFrameRecord(payload, meta, FrameWidth * 2,
                                 Adc16PixelFormat)) {
                const auto *bytes =
                    reinterpret_cast<const uchar *>(meta.constData());
                emit continuousAdc16Record(
                    QDateTime::currentMSecsSinceEpoch() * 1000,
                    body,
                    qFromLittleEndian<quint32>(bytes + 20),
                    qFromLittleEndian<quint32>(bytes + 24),
                    qFromLittleEndian<quint32>(bytes + 92),
                    qFromLittleEndian<quint16>(bytes + 98));
            }
        }
        if (m_active && m_continuousAdcEnabled
            && m_mode == Mode::Monitor && !m_uploadInProgress) {
            m_continuousAdcTimer.start(m_continuousAdcIntervalMs);
        }
        return;
    }

    if (requestId == m_networkReply) {
        m_networkReply = 0;
        const int target = std::exchange(m_networkTargetSegment, 0);
        const QVariantMap result = jsonMap(body);
        const bool success = error.isEmpty() && statusCode == 202
                             && result.value(QStringLiteral("accepted")).toBool();
        const QString responseError =
            result.value(QStringLiteral("error")).toMap()
                .value(QStringLiteral("message")).toString();
        const QString resultError =
            success ? QString{}
                    : (!responseError.isEmpty() ? responseError
                                                : (!error.isEmpty()
                                                       ? error
                                                       : tr("设备拒绝修改网段")));
        if (success) {
            m_active = false;
            m_networkSegment = target;
            emit networkSegmentChangeFinished(true, target, {});
            setState(QStringLiteral("disconnected"));
        } else {
            emit networkSegmentChangeFinished(false, target, resultError);
            if (m_active) m_pollTimer.start(0);
        }
        return;
    }

    if (requestId == m_reply) {
        m_reply = 0;
        m_requestPending = false;
        m_initialRequestTimer.stop();
        m_activeFrameRequestTimer.stop();
        if (m_active && statusCode == 503) {
            m_consecutiveFailures = 0;
            if (!m_uploadInProgress) {
                m_pollTimer.start(m_mode == Mode::Raw ? 20 : 80);
            }
            return;
        }
        if (!error.isEmpty() || (!m_active && statusCode != 200)) {
            if (!m_active) {
                if (m_connectRetryEnabled
                    && m_connectRetryElapsed.isValid()
                    && m_connectRetryElapsed.elapsed()
                           < m_connectRetryTimeoutMs) {
                    m_http.clearConnectionCache();
                    setState(QStringLiteral("reconnecting"));
                    m_connectRetryTimer.start(InitialConnectRetryMs);
                    return;
                }
                m_connectDeadlineTimer.stop();
                m_connectRetryEnabled = false;
                setState(QStringLiteral("disconnected"));
                emit errorOccurred(
                    !error.isEmpty()
                        ? tr("NCM HTTP 请求失败：%1").arg(error)
                        : tr("NCM HTTP 请求失败：HTTP %1").arg(statusCode));
                return;
            }
            handleActiveFrameFailure(error);
            return;
        }
        m_consecutiveFailures = 0;
        if (!m_active) {
            if (!isTn160V1Device(body)) {
                m_connectRetryTimer.stop();
                m_connectDeadlineTimer.stop();
                m_connectRetryEnabled = false;
                setState(QStringLiteral("disconnected"));
                emit errorOccurred(
                    tr("目标不是兼容的 TN160 USB NCM HTTP v1 设备"));
                return;
            }
            const QVariantMap root = jsonMap(body);
            const QVariantMap device =
                root.value(QStringLiteral("device")).toMap();
            const QVariantMap network =
                root.value(QStringLiteral("network")).toMap();
            m_deviceSerial = device.value(QStringLiteral("serial")).toString();
            m_networkSegment = network.value(QStringLiteral("segment")).toInt();
            m_deviceIdentity = tr("serial=%1; firmware=%2; build=%3")
                                   .arg(m_deviceSerial,
                                        device.value(QStringLiteral("firmware"))
                                            .toString(),
                                        device.value(QStringLiteral("build"))
                                            .toString());
            m_connectRetryTimer.stop();
            m_connectDeadlineTimer.stop();
            m_connectRetryEnabled = false;
            m_active = true;
            setState(QStringLiteral("connected"));
            if (m_continuousAdcEnabled && m_mode == Mode::Monitor) {
                m_continuousAdcTimer.start(m_continuousAdcIntervalMs);
            }
        } else {
            handleFrame(body);
        }
        if (m_active && !m_uploadInProgress) {
            m_pollTimer.start(m_mode == Mode::Raw ? 20 : 80);
        }
        return;
    }

    if (requestId == m_diagnosticsReply) {
        m_diagnosticsReply = 0;
        if (error.isEmpty()) m_lastDiagnostics = jsonMap(body);
        return;
    }

    if (requestId == m_thermalModelReadReply) {
        m_thermalModelReadReply = 0;
        const bool success = error.isEmpty() && statusCode == 200
                             && body.size() == ThermalModelPacketBytes;
        emit thermalModelReadbackFinished(
            success, success ? body : QByteArray{},
            success ? QString{}
                    : (!error.isEmpty() ? error
                                        : tr("热模型回读失败：HTTP %1")
                                              .arg(statusCode)));
        return;
    }

    if (requestId == m_thermalModelStateReply) {
        m_thermalModelStateReply = 0;
        const QVariantMap state = jsonMap(body);
        const bool success = error.isEmpty() && statusCode == 200
                             && !state.isEmpty();
        emit thermalModelStateReady(
            success, success ? state : QVariantMap{},
            success ? QString{}
                    : (!error.isEmpty() ? error
                                        : tr("热模型状态读取失败：HTTP %1")
                                              .arg(statusCode)));
        return;
    }

    if (requestId == m_thermalModelUploadReply) {
        m_thermalModelUploadReply = 0;
        const QVariantMap result = jsonMap(body);
        const bool success = error.isEmpty() && statusCode == 200
                             && result.value(QStringLiteral("accepted"))
                                    .toBool();
        const QString responseError =
            result.value(QStringLiteral("error")).toString();
        m_uploadInProgress = false;
        emit thermalModelUploadFinished(
            success, result,
            success ? QString{}
                    : (!responseError.isEmpty()
                           ? responseError
                           : (!error.isEmpty()
                                  ? error
                                  : tr("设备拒绝热模型：HTTP %1")
                                        .arg(statusCode))));
        if (m_active) {
            m_pollTimer.start(0);
            if (m_continuousAdcEnabled && m_mode == Mode::Monitor) {
                m_continuousAdcTimer.start(m_continuousAdcIntervalMs);
            }
        }
        return;
    }

    if (requestId == m_thermalModelClearReply) {
        m_thermalModelClearReply = 0;
        const QVariantMap result = jsonMap(body);
        const bool success = error.isEmpty() && statusCode == 200
                             && result.value(QStringLiteral("accepted"))
                                    .toBool()
                             && result.value(QStringLiteral("state"))
                                    .toString()
                                    == QStringLiteral("cleared");
        const QString responseError =
            result.value(QStringLiteral("error")).toString();
        m_uploadInProgress = false;
        emit thermalModelClearFinished(
            success, result,
            success ? QString{}
                    : (!responseError.isEmpty()
                           ? responseError
                           : (!error.isEmpty()
                                  ? error
                                  : tr("设备拒绝清除热模型：HTTP %1")
                                        .arg(statusCode))));
        if (m_active) m_pollTimer.start(0);
        return;
    }

    if (requestId != m_uploadReply) return;
    m_uploadReply = 0;
    const QVariantMap result = jsonMap(body);
    const bool success = error.isEmpty() && statusCode == 202
                         && result.value(QStringLiteral("accepted")).toBool();
    const QString responseError = result.value(QStringLiteral("error")).toString();
    const QString resultError = success ? QString{}
                                        : (!responseError.isEmpty()
                                               ? responseError
                                               : error);
    m_uploadInProgress = false;
    emit calibrationUploadFinished(success, result, resultError);
    if (!success && m_active) {
        m_pollTimer.start(0);
        if (m_continuousAdcEnabled && m_mode == Mode::Monitor) {
            m_continuousAdcTimer.start(m_continuousAdcIntervalMs);
        }
    }
}

void Tn160NcmTransport::setState(const QString &state)
{
    emit stateChanged(state);
}

double Tn160NcmTransport::ntcCelsius(const quint16 adc)
{
    if (adc == 0 || adc >= 4095) return std::numeric_limits<double>::quiet_NaN();
    constexpr double pullup = 10000.0;
    constexpr double r0 = 10000.0;
    constexpr double beta = 3435.0;
    constexpr double t0 = 298.15;
    const double resistance = pullup * adc / (4095.0 - adc);
    return 1.0 / (1.0 / t0 + std::log(resistance / r0) / beta) - 273.15;
}

quint16 Tn160NcmTransport::jsonU16(const QVariantMap &map,
                                    const QString &key, const quint16 fallback)
{
    const QVariant value = map.value(key);
    return value.isValid() ? quint16(std::clamp(value.toInt(), 0, 65535)) : fallback;
}

qint16 Tn160NcmTransport::jsonI16(const QVariantMap &map,
                                  const QString &key, const qint16 fallback)
{
    const QVariant value = map.value(key);
    return value.isValid() ? qint16(std::clamp(value.toInt(), -32768, 32767)) : fallback;
}

qint32 Tn160NcmTransport::jsonI32(const QVariantMap &map,
                                  const QString &key, const qint32 fallback)
{
    const QVariant value = map.value(key);
    if (!value.isValid()) return fallback;
    return qint32(std::clamp<qint64>(
        value.toLongLong(), std::numeric_limits<qint32>::min(),
        std::numeric_limits<qint32>::max()));
}

double Tn160NcmTransport::jsonDouble(const QVariantMap &map,
                                     const QString &key, const double fallback)
{
    const QVariant value = map.value(key);
    return value.isValid() ? value.toDouble() : fallback;
}

} // namespace serein::scope::tn160ncm
