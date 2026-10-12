#include "T384Transport.h"

#include "T384Engine.h"
#include "T384CalibrationProtocol.h"

#include <QtCore/QByteArrayView>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStringList>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <utility>

namespace serein::scope::t384 {
namespace {

constexpr auto Raw16StreamPath = "/raw16.stream";
constexpr auto DiagnosticsPath = "/diag";
constexpr auto CalibrationManifestPath = "/api/v1/calibration/v1/manifest";
constexpr auto CalibrationDataPath = "/api/v1/calibration/v1/data";
constexpr int InitialRetryIntervalMs = 500;
constexpr int InitialRequestTimeoutMs = 5000;
constexpr int InitialConnectionTimeoutMs = 30000;
constexpr int ModuleStateDeadlineMs = 40000;
constexpr int ModuleStatePollMs = 100;
constexpr int ModuleStateReleasePollMs = 250;
constexpr int ModuleStateReleaseTimeoutMs = 10000;

} // namespace

T384Transport::T384Transport(std::unique_ptr<T384Engine> engine,
                             QObject *parent)
    : QObject(parent), m_engine(std::move(engine)),
      m_network(std::make_unique<QNetworkAccessManager>(this))
{
    m_retryTimer.setSingleShot(true);
    m_initialRequestTimer.setSingleShot(true);
    m_connectDeadlineTimer.setSingleShot(true);
    m_moduleStatePollTimer.setSingleShot(true);
    m_moduleStateReleaseTimer.setSingleShot(true);
    m_moduleStateDeadlineTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this,
            &T384Transport::beginConnectionPreflight);
    connect(&m_initialRequestTimer, &QTimer::timeout, this,
            &T384Transport::initialRequestTimedOut);
    connect(&m_connectDeadlineTimer, &QTimer::timeout, this,
            &T384Transport::initialConnectionTimedOut);
    connect(&m_moduleStatePollTimer, &QTimer::timeout, this,
            &T384Transport::requestModuleStateStatus);
    connect(&m_moduleStateReleaseTimer, &QTimer::timeout, this,
            &T384Transport::requestModuleStateStreamRelease);
    connect(&m_moduleStateDeadlineTimer, &QTimer::timeout, this, [this] {
        cleanupModuleState(tr("读取采集边界状态超过%1 ms")
                               .arg(ModuleStateDeadlineMs));
    });
}

T384Transport::~T384Transport()
{
    stop();
}

bool T384Transport::isActive() const noexcept
{
    return m_streamReply && !m_stopping;
}

QString T384Transport::state() const
{
    return m_state;
}

QString T384Transport::endpoint() const
{
    return m_endpoint.toString(QUrl::RemovePath | QUrl::RemoveQuery
                               | QUrl::RemoveFragment);
}

QString T384Transport::lastError() const
{
    return m_lastError;
}

QVariantMap T384Transport::diagnostics() const
{
    return m_diagnostics;
}

bool T384Transport::replaceEngine(std::unique_ptr<T384Engine> engine,
                                  QString *error)
{
    if (!engine) {
        if (error) *error = tr("T384 数据引擎不能为空");
        return false;
    }
    m_engine = std::move(engine);
    resetDecoder();
    return true;
}

void T384Transport::start(const QString &endpoint)
{
    stop();
    if (!endpoint.trimmed().isEmpty()) m_endpoint = normalizeEndpoint(endpoint);
    if (!m_endpoint.isValid() || m_endpoint.scheme() != QStringLiteral("http")
        || m_endpoint.host().isEmpty()) {
        setError(tr("T384 endpoint 无效：%1").arg(endpoint));
        return;
    }
    if (!m_engine) {
        setError(tr("T384 数据引擎不能为空"));
        return;
    }

    m_stopping = false;
    m_lastError.clear();
    m_pendingBytes.clear();
    m_engine->reset();
    m_initialRetryEnabled = true;
    m_streamReceivedData = false;
    m_recoveringStream = false;
    m_connectPreflight = false;
    m_connectElapsed.start();
    m_connectDeadlineTimer.start(InitialConnectionTimeoutMs);
    m_network->clearConnectionCache();
    beginConnectionPreflight();
}

void T384Transport::beginConnectionPreflight()
{
    if (m_stopping || !m_initialRetryEnabled || m_streamReply
        || m_diagnosticsReply) return;
    m_connectPreflight = true;
    if (m_state != QStringLiteral("connecting")
        && m_state != QStringLiteral("reconnecting")) {
        setState(m_recoveringStream ? QStringLiteral("reconnecting")
                                    : QStringLiteral("connecting"));
    }
    m_network->clearConnectionCache();
    requestDiagnostics();
}

void T384Transport::beginStreamRequest()
{
    if (m_stopping || !m_initialRetryEnabled || m_streamReply) return;

    m_streamReceivedData = false;
    if (m_state != QStringLiteral("connecting")
        && m_state != QStringLiteral("reconnecting")) {
        setState(m_recoveringStream ? QStringLiteral("reconnecting")
                                    : QStringLiteral("connecting"));
    }

    QNetworkRequest request(endpointUrl(QString::fromLatin1(Raw16StreamPath)));
    request.setRawHeader("Accept", "application/octet-stream");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_streamReply = m_network->get(request);
    const QPointer<QNetworkReply> attempt = m_streamReply;
    m_initialRequestTimer.start(InitialRequestTimeoutMs);
    connect(attempt, &QNetworkReply::readyRead, this, [this, attempt] {
        if (!attempt || m_streamReply != attempt || m_stopping) return;
        const int status = attempt->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 0 && (status < 200 || status >= 300)) return;
        const QByteArray chunk = attempt->readAll();
        if (chunk.isEmpty()) return;
        const bool firstChunk = !m_streamReceivedData;
        m_streamReceivedData = true;
        m_initialRetryEnabled = false;
        m_retryTimer.stop();
        m_initialRequestTimer.stop();
        m_connectDeadlineTimer.stop();
        if (firstChunk) {
            QVariantMap metadata;
            metadata.insert(QStringLiteral("temperature_model"),
                            QString::fromUtf8(attempt->rawHeader(
                                "X-T384-Temperature-Model")));
            metadata.insert(QStringLiteral("y16_linear_x100"),
                            QString::fromUtf8(attempt->rawHeader(
                                "X-T384-Y16-Linear-X100")));
            metadata.insert(QStringLiteral("frame_width"),
                            QString::fromUtf8(attempt->rawHeader(
                                "X-T384-Frame-Width")));
            metadata.insert(QStringLiteral("frame_height"),
                            QString::fromUtf8(attempt->rawHeader(
                                "X-T384-Frame-Height")));
            metadata.insert(QStringLiteral("pixel_format"),
                            QString::fromUtf8(attempt->rawHeader(
                                "X-T384-Pixel-Format")));
            emit streamMetadataReady(metadata);
            QTimer::singleShot(500, this, [this] {
                if (!m_stopping && m_state == QStringLiteral("streaming"))
                    requestDiagnostics();
            });
        }
        emit rawDataReceived(chunk);
        emit bytesReceived(chunk.size());
        m_pendingBytes.append(chunk);
        consumeBufferedData();
        m_recoveringStream = false;
        setState(QStringLiteral("streaming"));
    });
    connect(attempt, &QNetworkReply::finished, this, [this, attempt] {
        if (!attempt || m_streamReply != attempt) return;
        QPointer<QNetworkReply> reply = std::exchange(m_streamReply, {});
        if (!reply) return;
        m_initialRequestTimer.stop();
        const bool stopped = m_stopping;
        const QString error = reply->error() == QNetworkReply::NoError
                                  ? QString{}
                                  : reply->errorString();
        const QByteArray responseBody = reply->readAll().trimmed();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (stopped) return;
        if (m_initialRetryEnabled && !m_streamReceivedData
            && (status < 200 || status >= 300 || !error.isEmpty())) {
            const QString reason = status >= 400 && !responseBody.isEmpty()
                ? tr("HTTP %1：%2").arg(status).arg(
                      QString::fromUtf8(responseBody.left(240)))
                : error.isEmpty() ? tr("HTTP %1").arg(status) : error;
            scheduleInitialRetry(reason);
        } else {
            m_streamReceivedData = false;
            m_initialRetryEnabled = true;
            m_recoveringStream = true;
            m_connectElapsed.restart();
            m_connectDeadlineTimer.start(InitialConnectionTimeoutMs);
            scheduleInitialRetry(error.isEmpty()
                ? tr("RAW16 流意外结束（HTTP %1）").arg(status)
                : tr("RAW16 流中断：%1").arg(error));
        }
    });
}

void T384Transport::scheduleInitialRetry(const QString &message)
{
    if (!m_initialRetryEnabled || m_stopping) return;
    if (m_connectElapsed.isValid()
        && m_connectElapsed.elapsed() < InitialConnectionTimeoutMs) {
        setState(QStringLiteral("reconnecting"));
        m_lastError = tr("等待 T384 网络恢复：%1").arg(message);
        const int remainingMs = InitialConnectionTimeoutMs
            - int(m_connectElapsed.elapsed());
        emit retryScheduled(message, InitialRetryIntervalMs,
                            qMax(0, remainingMs));
        m_retryTimer.start(InitialRetryIntervalMs);
        return;
    }
    m_initialRetryEnabled = false;
    m_connectDeadlineTimer.stop();
    setError(tr("T384 NCM 连接超时：%1").arg(message));
}

void T384Transport::initialRequestTimedOut()
{
    if (!m_initialRetryEnabled || m_stopping || !m_streamReply
        || m_streamReceivedData) return;
    QPointer<QNetworkReply> reply = std::exchange(m_streamReply, {});
    reply->abort();
    reply->deleteLater();
    scheduleInitialRetry(tr("首帧等待超过 %1 ms").arg(InitialRequestTimeoutMs));
}

void T384Transport::initialConnectionTimedOut()
{
    if (!m_initialRetryEnabled || m_stopping) return;
    m_retryTimer.stop();
    m_initialRequestTimer.stop();
    if (m_streamReply) {
        QPointer<QNetworkReply> reply = std::exchange(m_streamReply, {});
        reply->abort();
        reply->deleteLater();
    }
    if (m_diagnosticsReply) {
        disconnect(m_diagnosticsReply, nullptr, this, nullptr);
        m_diagnosticsReply->abort();
        m_diagnosticsReply->deleteLater();
        m_diagnosticsReply = nullptr;
    }
    m_connectPreflight = false;
    m_initialRetryEnabled = false;
    setError(m_recoveringStream
                 ? tr("T384 RAW16 流重连超时（%1 ms）")
                       .arg(InitialConnectionTimeoutMs)
                 : tr("等待 T384 USB 网络恢复超过 %1 ms")
                       .arg(InitialConnectionTimeoutMs));
}

void T384Transport::stop()
{
    const bool wasActive = isActive() || m_state == QStringLiteral("connecting")
                           || m_state == QStringLiteral("reconnecting")
                           || m_state == QStringLiteral("streaming");
    m_stopping = true;
    m_initialRetryEnabled = false;
    m_recoveringStream = false;
    m_connectPreflight = false;
    m_retryTimer.stop();
    m_initialRequestTimer.stop();
    m_connectDeadlineTimer.stop();
    if (m_streamReply) {
        disconnect(m_streamReply, nullptr, this, nullptr);
        m_streamReply->abort();
        m_streamReply->deleteLater();
        m_streamReply = nullptr;
    }
    if (m_diagnosticsReply) {
        disconnect(m_diagnosticsReply, nullptr, this, nullptr);
        m_diagnosticsReply->abort();
        m_diagnosticsReply->deleteLater();
        m_diagnosticsReply = nullptr;
    }
    if (m_calibrationReply) {
        disconnect(m_calibrationReply, nullptr, this, nullptr);
        m_calibrationReply->abort();
        m_calibrationReply->deleteLater();
        m_calibrationReply = nullptr;
    }
    m_moduleStatePollTimer.stop();
    m_moduleStateReleaseTimer.stop();
    m_moduleStateDeadlineTimer.stop();
    if (m_moduleStateReply) {
        disconnect(m_moduleStateReply, nullptr, this, nullptr);
        m_moduleStateReply->abort();
        m_moduleStateReply->deleteLater();
        m_moduleStateReply = nullptr;
    }
    if (!m_moduleStateTag.isEmpty()) cleanupModuleState(tr("数据流已停止"));
    m_pendingBytes.clear();
    if (wasActive) setState(QStringLiteral("disconnected"));
}

void T384Transport::requestCalibrationManifest()
{
    if (!m_network || !m_endpoint.isValid()) return;
    if (m_calibrationReply) { m_calibrationReply->abort(); m_calibrationReply->deleteLater(); }
    QNetworkRequest request(endpointUrl(QString::fromLatin1(CalibrationManifestPath)));
    request.setRawHeader("Accept", "application/json");
    m_calibrationReply = m_network->get(request);
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 200 &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300;
        const QString message = ok ? QString{} : reply->errorString(); reply->deleteLater();
        if (ok) emit calibrationManifestReady(body);
        emit calibrationOperationFinished(QStringLiteral("manifest"), ok,
                                          ok ? QString{} : message);
    });
}

void T384Transport::requestCalibrationData()
{
    if (!m_network || !m_endpoint.isValid()) return;
    if (m_calibrationReply) { m_calibrationReply->abort(); m_calibrationReply->deleteLater(); }
    QNetworkRequest request(endpointUrl(QString::fromLatin1(CalibrationDataPath)));
    request.setRawHeader("Accept", "application/json");
    m_calibrationReply = m_network->get(request);
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() >= 200 &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 300;
        const QString message = ok ? QString{} : reply->errorString(); reply->deleteLater();
        if (ok) emit calibrationDataReady(body);
        emit calibrationOperationFinished(QStringLiteral("read"), ok,
                                          ok ? QString{} : message);
    });
}

void T384Transport::uploadCalibrationData(const QByteArray &payload)
{
    if (!m_network || !m_endpoint.isValid() || payload.isEmpty()) return;
    if (m_calibrationReply) { m_calibrationReply->abort(); m_calibrationReply->deleteLater(); }
    QNetworkRequest request(endpointUrl(QString::fromLatin1(CalibrationDataPath)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    request.setRawHeader("Accept", "application/json");
    m_calibrationReply = m_network->put(request, payload);
    connect(m_calibrationReply, &QNetworkReply::uploadProgress, this,
            &T384Transport::calibrationProgress);
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString message = ok ? QString{} : (reply->errorString().isEmpty() ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
        reply->deleteLater();
        emit calibrationOperationFinished(QStringLiteral("upload"), ok, message);
    });
}

void T384Transport::commitCalibration()
{
    if (!m_network || !m_endpoint.isValid()) return;
    QNetworkRequest request(endpointUrl(QStringLiteral("/api/v1/calibration/v1/commit")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    m_calibrationReply = m_network->post(request, QByteArray{});
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString msg = ok ? QString{} : (reply->errorString().isEmpty() ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
        reply->deleteLater(); emit calibrationOperationFinished(QStringLiteral("commit"), ok, msg);
    });
}

void T384Transport::abortCalibration()
{
    if (!m_network || !m_endpoint.isValid()) return;
    QNetworkRequest request(endpointUrl(QStringLiteral("/api/v1/calibration/v1/abort")));
    m_calibrationReply = m_network->post(request, QByteArray{});
    connect(m_calibrationReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_calibrationReply.data();
        m_calibrationReply = nullptr;
        if (!reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        const QString msg = ok ? QString{} : (reply->errorString().isEmpty() ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
        reply->deleteLater(); emit calibrationOperationFinished(QStringLiteral("abort"), ok, msg);
    });
}

void T384Transport::requestCalibrationBoundaryState(const QString &tag)
{
    if (!m_network || !m_endpoint.isValid() || tag.trimmed().isEmpty()) return;
    if (m_moduleStateReply || !m_moduleStateTag.isEmpty()) {
        emit calibrationBoundaryStateReady(tag, false, {},
                                           tr("已有机芯状态事务正在执行"));
        return;
    }
    m_moduleStateTag = tag;
    m_moduleStateStatus.clear();
    m_moduleStateSnapshot.clear();
    m_moduleStateTransaction = 0;
    m_moduleStateDownloaded = false;
    m_moduleStateReleaseElapsed.start();
    m_moduleStateDeadlineTimer.start(ModuleStateDeadlineMs);
    requestModuleStateStreamRelease();
}

void T384Transport::requestModuleStateStreamRelease()
{
    if (m_moduleStateTag.isEmpty() || m_moduleStateReply) return;
    if (m_moduleStateReleaseElapsed.elapsed() >= ModuleStateReleaseTimeoutMs) {
        cleanupModuleState(tr("视频流未释放；请关闭其他画面后重试"));
        return;
    }
    QNetworkRequest request(endpointUrl(QString::fromLatin1(DiagnosticsPath)));
    request.setTransferTimeout(3000);
    m_moduleStateReply = m_network->get(request);
    connect(m_moduleStateReply, &QNetworkReply::finished, this, [this] {
        QPointer<QNetworkReply> reply = std::exchange(m_moduleStateReply, {});
        if (!reply) return;
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError && status == 200;
        reply->deleteLater();
        if (ok) {
            const QString active = parseDiagnostics(body)
                .value(QStringLiteral("stream.active")).toString();
            if (active == QStringLiteral("0")) {
                startModuleStateRead();
                return;
            }
            if (active != QStringLiteral("1")) {
                cleanupModuleState(tr("设备诊断缺少有效的stream.active状态"));
                return;
            }
        }
        m_moduleStateReleaseTimer.start(ModuleStateReleasePollMs);
    });
}

void T384Transport::startModuleStateRead()
{
    if (m_moduleStateTag.isEmpty() || m_moduleStateReply) return;
    QNetworkRequest request(endpointUrl(
        QStringLiteral("/api/v1/module-files/read")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/octet-stream"));
    request.setRawHeader("Accept", "application/json");
    m_moduleStateReply = m_network->post(request, QByteArrayLiteral("cal-state"));
    connect(m_moduleStateReply, &QNetworkReply::finished, this, [this] {
        QPointer<QNetworkReply> reply = std::exchange(m_moduleStateReply, {});
        if (!reply) return;
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString networkError = reply->error() == QNetworkReply::NoError
            ? QString{} : reply->errorString();
        reply->deleteLater();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (status == 409) {
            if (m_moduleStateReleaseElapsed.elapsed() < ModuleStateReleaseTimeoutMs)
                m_moduleStateReleaseTimer.start(ModuleStateReleasePollMs);
            else
                cleanupModuleState(tr("视频流或机芯状态仍被占用；请关闭其他画面后重试"));
            return;
        }
        if (!networkError.isEmpty() || status != 202 || !document.isObject()) {
            cleanupModuleState(networkError.isEmpty()
                ? tr("启动cal-state失败：HTTP %1 / %2")
                      .arg(status).arg(parseError.errorString())
                : networkError);
            return;
        }
        m_moduleStateStatus = document.object().toVariantMap();
        m_moduleStateTransaction =
            m_moduleStateStatus.value(QStringLiteral("transaction")).toUInt();
        if (m_moduleStateTransaction == 0) {
            cleanupModuleState(tr("cal-state未返回有效事务号"));
            return;
        }
        requestModuleStateStatus();
    });
}

void T384Transport::requestModuleStateStatus()
{
    if (m_moduleStateTag.isEmpty() || m_moduleStateReply) return;
    QNetworkRequest request(endpointUrl(
        QStringLiteral("/api/v1/module-files/status")));
    request.setRawHeader("Accept", "application/json");
    m_moduleStateReply = m_network->get(request);
    connect(m_moduleStateReply, &QNetworkReply::finished, this, [this] {
        QPointer<QNetworkReply> reply = std::exchange(m_moduleStateReply, {});
        if (!reply) return;
        const int httpStatus = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString networkError = reply->error() == QNetworkReply::NoError
            ? QString{} : reply->errorString();
        reply->deleteLater();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (!networkError.isEmpty() || httpStatus != 200 || !document.isObject()) {
            cleanupModuleState(networkError.isEmpty()
                ? tr("读取cal-state状态失败：HTTP %1").arg(httpStatus)
                : networkError);
            return;
        }
        const QVariantMap status = document.object().toVariantMap();
        if (status.value(QStringLiteral("transaction")).toUInt()
                != m_moduleStateTransaction) {
            cleanupModuleState(tr("cal-state事务号在轮询期间变化"));
            return;
        }
        m_moduleStateStatus = status;
        if (m_moduleStateDownloaded) {
            if (status.value(QStringLiteral("dvp_paused")).toBool()
                || status.value(QStringLiteral("cleanup_failed")).toBool()) {
                cleanupModuleState(tr("cal-state下载后采集仍暂停"));
                return;
            }
            finishModuleState(true, m_moduleStateSnapshot, {});
            return;
        }
        const QString state = status.value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("reading")) {
            m_moduleStatePollTimer.start(ModuleStatePollMs);
            return;
        }
        if (state != QStringLiteral("ready")
            || status.value(QStringLiteral("id")).toString()
                   != QStringLiteral("cal-state")
            || !status.value(QStringLiteral("identity_verified")).toBool()
            || status.value(QStringLiteral("error")).toInt() != 0
            || status.value(QStringLiteral("cleanup_failed")).toBool()
            || status.value(QStringLiteral("length")).toInt() != 8
            || status.value(QStringLiteral("received")).toInt() != 8) {
            cleanupModuleState(tr("cal-state机芯结果未通过身份/长度/错误门禁"));
            return;
        }
        requestModuleStateData();
    });
}

void T384Transport::requestModuleStateData()
{
    QUrl url = endpointUrl(QStringLiteral("/api/v1/module-files/data"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("transaction"),
                       QString::number(m_moduleStateTransaction));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/octet-stream");
    m_moduleStateReply = m_network->get(request);
    connect(m_moduleStateReply, &QNetworkReply::finished, this, [this] {
        QPointer<QNetworkReply> reply = std::exchange(m_moduleStateReply, {});
        if (!reply) return;
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString networkError = reply->error() == QNetworkReply::NoError
            ? QString{} : reply->errorString();
        const quint32 responseTransaction =
            reply->rawHeader("X-T384-Transaction").toUInt();
        bool crcHeaderOk = false;
        const quint32 responseCrc =
            reply->rawHeader("X-T384-CRC32").toUInt(&crcHeaderOk, 16);
        reply->deleteLater();
        const quint32 expectedCrc =
            m_moduleStateStatus.value(QStringLiteral("crc32")).toUInt();
        if (!networkError.isEmpty() || status != 200 || body.size() != 8
            || responseTransaction != m_moduleStateTransaction || !crcHeaderOk
            || responseCrc != expectedCrc || crc32(body) != expectedCrc) {
            cleanupModuleState(networkError.isEmpty()
                ? tr("cal-state数据长度/事务/CRC校验失败") : networkError);
            return;
        }
        const auto le16 = [&body](const int offset) {
            return quint16(quint8(body[offset]))
                | quint16(quint8(body[offset + 1])) << 8;
        };
        const quint16 gainCode = le16(0);
        const quint16 vtemp = le16(2);
        const quint16 ffc = le16(4);
        const quint16 shutter = le16(6);
        if ((gainCode != 0xffffu && gainCode > 3u) || ffc > 1u
            || shutter > 2u) {
            cleanupModuleState(tr("cal-state返回未知枚举值"));
            return;
        }
        static const QStringList gains = {
            QStringLiteral("low"), QStringLiteral("high"),
            QStringLiteral("low-low"), QStringLiteral("high-high")};
        const QString pn = m_moduleStateStatus.value(QStringLiteral("pn")).toString();
        const QString sn = m_moduleStateStatus.value(QStringLiteral("sn")).toString();
        const QString fwHex =
            m_moduleStateStatus.value(QStringLiteral("fw_hex")).toString();
        const QByteArray identity = calibrationIdentity(
            pn.toUtf8(), sn.toUtf8(), fwHex.toLatin1());
        m_moduleStateSnapshot = {
            {QStringLiteral("format"), QStringLiteral("t384-calibration-state-v1")},
            {QStringLiteral("identity_sha256_hex"),
             QString::fromLatin1(identity.toHex())},
            {QStringLiteral("pn"), pn}, {QStringLiteral("sn"), sn},
            {QStringLiteral("fw_hex"), fwHex},
            {QStringLiteral("gain"), gainCode == 0xffffu
                ? QStringLiteral("unknown") : gains[int(gainCode)]},
            {QStringLiteral("gain_verified"), gainCode != 0xffffu},
            {QStringLiteral("vtemp_raw"), int(vtemp)},
            {QStringLiteral("auto_ffc_enabled"), bool(ffc)},
            {QStringLiteral("shutter_status"), int(shutter)},
            {QStringLiteral("transaction"), int(m_moduleStateTransaction)},
            {QStringLiteral("crc32"), qint64(expectedCrc)},
            {QStringLiteral("sampling"),
             QStringLiteral("paused-module-query-not-per-frame")}};
        m_moduleStateDownloaded = true;
        m_moduleStatePollTimer.start(ModuleStatePollMs);
    });
}

void T384Transport::finishModuleState(const bool success,
                                      const QVariantMap &snapshot,
                                      const QString &message)
{
    const QString tag = std::exchange(m_moduleStateTag, {});
    // snapshot may refer to m_moduleStateSnapshot; keep it before clearing state.
    const QVariantMap completedSnapshot = snapshot;
    m_moduleStatePollTimer.stop();
    m_moduleStateReleaseTimer.stop();
    m_moduleStateDeadlineTimer.stop();
    m_moduleStateTransaction = 0;
    m_moduleStateStatus.clear();
    m_moduleStateSnapshot.clear();
    m_moduleStateDownloaded = false;
    emit calibrationBoundaryStateReady(tag, success, completedSnapshot, message);
}

void T384Transport::cleanupModuleState(const QString &message)
{
    if (m_moduleStateReply) {
        disconnect(m_moduleStateReply, nullptr, this, nullptr);
        m_moduleStateReply->abort();
        m_moduleStateReply->deleteLater();
        m_moduleStateReply = nullptr;
    }
    if (m_moduleStateTransaction != 0 && m_network && m_endpoint.isValid()) {
        QNetworkRequest request(endpointUrl(
            QStringLiteral("/api/v1/module-files/abort")));
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/octet-stream"));
        QNetworkReply *cleanup = m_network->post(
            request, QByteArray::number(m_moduleStateTransaction));
        connect(cleanup, &QNetworkReply::finished, cleanup,
                &QObject::deleteLater);
    }
    finishModuleState(false, {}, message);
}

quint32 T384Transport::crc32(const QByteArray &bytes)
{
    quint32 value = 0xffffffffu;
    for (const char c : bytes) {
        value ^= quint8(c);
        for (int bit = 0; bit != 8; ++bit)
            value = (value >> 1u)
                ^ (0xedb88320u & quint32(-qint32(value & 1u)));
    }
    return value ^ 0xffffffffu;
}

void T384Transport::resetDecoder()
{
    m_pendingBytes.clear();
    if (m_engine) m_engine->reset();
}

void T384Transport::requestDiagnostics()
{
    if (!m_network || !m_endpoint.isValid()) return;
    if (m_diagnosticsReply) {
        disconnect(m_diagnosticsReply, nullptr, this, nullptr);
        m_diagnosticsReply->abort();
        m_diagnosticsReply->deleteLater();
    }
    QNetworkRequest request(endpointUrl(QString::fromLatin1(DiagnosticsPath)));
    request.setRawHeader("Accept", "text/plain");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_diagnosticsReply = m_network->get(request);
    connect(m_diagnosticsReply, &QNetworkReply::finished, this, [this] {
        QPointer<QNetworkReply> reply = std::exchange(m_diagnosticsReply, {});
        if (!reply) return;
        const QByteArray body = reply->readAll();
        const QString error = reply->error() == QNetworkReply::NoError
                                  ? QString{}
                                  : reply->errorString();
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        const bool connectionPreflight = m_connectPreflight;
        m_connectPreflight = false;
        if (m_stopping) return;
        if (!error.isEmpty() || status < 200 || status >= 300) {
            const QString reason = tr("读取 /diag 失败：%1")
                .arg(error.isEmpty() ? tr("HTTP %1").arg(status) : error);
            if (connectionPreflight) scheduleInitialRetry(reason);
            else emit errorOccurred(reason);
            return;
        }
        m_diagnostics = parseDiagnostics(body);
        emit diagnosticsReady(m_diagnostics);
        if (!connectionPreflight) return;
        if (m_diagnostics.value(QStringLiteral("stream.active")).toString()
                == QStringLiteral("1")) {
            scheduleInitialRetry(tr("设备RAW16流仍被其他客户端或旧连接占用"));
            return;
        }
        if (m_diagnostics.value(QStringLiteral("source.stream_ready")).toString()
                != QStringLiteral("1")) {
            scheduleInitialRetry(tr("设备采集源尚未就绪"));
            return;
        }
        beginStreamRequest();
    });
}

void T384Transport::setState(const QString &state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(m_state);
}

void T384Transport::setError(const QString &error)
{
    m_lastError = error;
    setState(QStringLiteral("error"));
    emit errorOccurred(error);
}

void T384Transport::consumeBufferedData()
{
    while (!m_pendingBytes.isEmpty() && m_engine) {
        const ParseResult result = m_engine->feed(
            QByteArrayView(m_pendingBytes.constData(), m_pendingBytes.size()));
        if (!result.packets.isEmpty()) emit packetsDecoded(result.packets);
        if (result.consumed < 0 || result.consumed > m_pendingBytes.size()) {
            setError(tr("T384 解码器返回非法 consumed=%1").arg(result.consumed));
            return;
        }
        if (result.consumed == 0) return;
        m_pendingBytes.remove(0, result.consumed);
    }
}

QUrl T384Transport::endpointUrl(const QString &path) const
{
    QUrl url = m_endpoint;
    url.setPath(path);
    url.setQuery(QUrlQuery{});
    url.setFragment({});
    return url;
}

QUrl T384Transport::normalizeEndpoint(const QString &endpoint)
{
    QString normalized = endpoint.trimmed();
    if (!normalized.contains(QStringLiteral("://"))) {
        normalized.prepend(QStringLiteral("http://"));
    }
    QUrl url = QUrl::fromUserInput(normalized);
    url.setPath({});
    url.setQuery(QUrlQuery{});
    url.setFragment({});
    return url;
}

QVariantMap T384Transport::parseDiagnostics(const QByteArray &body)
{
    QVariantMap values;
    const QList<QByteArray> lines = body.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
        const qsizetype separator = trimmed.indexOf('=');
        if (separator <= 0) continue;
        const QString key = QString::fromUtf8(trimmed.first(separator)).trimmed();
        const QString value = QString::fromUtf8(trimmed.sliced(separator + 1)).trimmed();
        if (!key.isEmpty()) values.insert(key, value);
    }
    return values;
}

} // namespace serein::scope::t384
