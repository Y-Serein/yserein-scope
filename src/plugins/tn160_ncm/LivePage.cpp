#include "LivePage.h"
#include "ThermalView.h"
#include "Tn160Engine.h"
#include "Tn160ProtocolEngines.h"
#include "TrendWidget.h"

#include <serein/scope/AppPaths.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>

#include <cmath>
#include <exception>
#include <limits>

namespace serein::scope::tn160ncm {

namespace {

constexpr int RawFrameWordsPerRow = FrameWidth + 2;
constexpr int RawFrameSize = 2 + FrameHeight * RawFrameWordsPerRow * 2 + 2;
constexpr int MaxRawCaptureFrames = 64;
constexpr int FfcSoftSilenceMs = 700;
constexpr int MonitorHardSilenceMs = 10000;
constexpr int RawHardSilenceMs = 10000;
constexpr int UiFrameIntervalMs = 100;
constexpr int UiFrameQueueCapacity = 3;
constexpr int FpsWindowMs = 2000;
constexpr int FpsWarmupMs = 1000;
constexpr quint8 MonitorCommand = 0x11;
constexpr quint8 RawCommand = 0xaa;

enum SampleValueIndex {
    CenterRoiC = 0,
    NtcC = 1,
    CenterRoiAdu = 2,
    VtempRaw = 3,
    CenterPointC = 4,
    CenterPointAdu = 5,
    NtcAdu = 6,
    NtcRefAdu = 7,
    TLowC = 8,
    THighC = 9,
    SmoothLowAdu = 10,
    SmoothHighAdu = 11,
    MeanDiff = 12,
    VtempSmooth = 13,
    NucDecision = 14,
    NucCount = 15,
    NucDg = 16,
    NucDv = 17,
    NucDn = 18,
    BootNtcAdu = 19,
    BootDeltaAdu = 20,
    WarmupComp = 21,
    NormalComp = 22,
    TotalComp = 23,
    Anchor = 24,
    EffectiveAnchor = 25,
};

double sampleValue(const SamplePacket &sample, const int index)
{
    return index >= 0 && index < sample.values.size()
        ? sample.values.at(index) : std::numeric_limits<double>::quiet_NaN();
}

int sampleInteger(const SamplePacket &sample, const int index, const int fallback = 0)
{
    const double value = sampleValue(sample, index);
    return std::isfinite(value) ? int(qRound(value)) : fallback;
}

QString formattedTemperature(const double value, const int decimals = 1)
{
    return std::isfinite(value)
        ? QStringLiteral("%1°C").arg(value, 0, 'f', decimals)
        : QStringLiteral("—");
}

QString formattedInteger(const double value)
{
    return std::isfinite(value) ? QString::number(qRound64(value))
                                : QStringLiteral("—");
}

QString nucDecisionLabel(const int decision)
{
    switch (decision) {
    case 0: return QStringLiteral("ACC");
    case 1: return QStringLiteral("REJ_VN");
    case 2: return QStringLiteral("REJ_LIM");
    case 3: return QStringLiteral("REJ_JMP");
    case 4: return QStringLiteral("NOFRM");
    case 255: return QStringLiteral("--");
    default: return QStringLiteral("EV%1").arg(decision);
    }
}

QString diagnosticExportPath(const QString &extension)
{
    QString rootPath = serein::scope::AppPaths::dataRoot();
    if (rootPath.isEmpty()) {
        rootPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    QDir root(rootPath);
    root.mkpath(QStringLiteral("logs"));
    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss"));
    return root.filePath(QStringLiteral("logs/tn160_diagnostic_%1.%2")
                             .arg(stamp, extension));
}

QString withExtension(QString path, const QString &extension)
{
    if (QFileInfo(path).suffix().isEmpty()) path += QLatin1Char('.') + extension;
    return path;
}

bool savePngImage(const QString &path, const QImage &image, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QObject::tr("无法创建导出目录");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (!image.save(&file, "PNG")) {
        if (error) *error = QObject::tr("PNG 编码失败");
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QByteArray csvNumber(const double value, const int precision = 12)
{
    return std::isfinite(value) ? QByteArray::number(value, 'g', precision)
                                : QByteArray{};
}

bool saveDiagnosticCsv(const QString &path,
                       const QVector<DiagnosticRecord> &records,
                       QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QObject::tr("无法创建导出目录");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    static const QByteArray Header =
        "host_timestamp_us,t_s,center_roi_c,center_raw_c,center_roi_adu,"
        "center_raw_adu,vtemp_raw_adu,vtemp_smooth_adu,vtemp_delta_adu,"
        "ntc_adu,ntc_c,ntc_ref_adu,ntc_ref_c,t_low_c,t_high_c,"
        "smooth_low_adu,smooth_high_adu,mean_diff,warmup_comp,normal_comp,"
        "total_comp,anchor,effective_anchor,nuc_decision,nuc_count,nuc_dg,"
        "nuc_dv,nuc_dn,boot_ntc_adu,boot_delta_adu,ffc_event\n";
    if (file.write("\xef\xbb\xbf", 3) != 3 || file.write(Header) != Header.size()) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    const double vtempReference = records.isEmpty()
        ? std::numeric_limits<double>::quiet_NaN()
        : records.constFirst().values[VtempRaw];
    for (const DiagnosticRecord &record : records) {
        const auto &v = record.values;
        const double ntcRefC = std::isfinite(v[NtcRefAdu])
            ? Tn160Engine::ntcCelsius(quint16(std::clamp<qint64>(
                qRound64(v[NtcRefAdu]), 0, 65535)))
            : std::numeric_limits<double>::quiet_NaN();
        const double vtempDelta = std::isfinite(v[VtempRaw])
            && std::isfinite(vtempReference) ? v[VtempRaw] - vtempReference
                                             : std::numeric_limits<double>::quiet_NaN();
        QByteArray row;
        row.reserve(360);
        const auto add = [&row](const QByteArray &field) {
            if (!row.isEmpty()) row.append(',');
            row.append(field);
        };
        add(QByteArray::number(record.timestampUs));
        add(csvNumber(record.seconds, 10));
        add(csvNumber(v[CenterRoiC]));
        add(csvNumber(v[CenterPointC]));
        add(csvNumber(v[CenterRoiAdu]));
        add(csvNumber(v[CenterPointAdu]));
        add(csvNumber(v[VtempRaw]));
        add(csvNumber(v[VtempSmooth]));
        add(csvNumber(vtempDelta));
        add(csvNumber(v[NtcAdu]));
        add(csvNumber(v[NtcC]));
        add(csvNumber(v[NtcRefAdu]));
        add(csvNumber(ntcRefC));
        add(csvNumber(v[TLowC]));
        add(csvNumber(v[THighC]));
        add(csvNumber(v[SmoothLowAdu]));
        add(csvNumber(v[SmoothHighAdu]));
        add(csvNumber(v[MeanDiff]));
        add(csvNumber(v[WarmupComp]));
        add(csvNumber(v[NormalComp]));
        add(csvNumber(v[TotalComp]));
        add(csvNumber(v[Anchor]));
        add(csvNumber(v[EffectiveAnchor]));
        add(csvNumber(v[NucDecision]));
        add(csvNumber(v[NucCount]));
        add(csvNumber(v[NucDg]));
        add(csvNumber(v[NucDv]));
        add(csvNumber(v[NucDn]));
        add(csvNumber(v[BootNtcAdu]));
        add(csvNumber(v[BootDeltaAdu]));
        add(record.ffcEvent.toUtf8());
        row.append('\n');
        if (file.write(row) != row.size()) {
            if (error) *error = file.errorString();
            file.cancelWriting();
            return false;
        }
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QString saveRawCapture(const QVector<QByteArray> &frames, QString *error)
{
    if (frames.isEmpty()) {
        if (error) *error = QObject::tr("没有可保存的原始帧");
        return {};
    }
    const QString rootPath = serein::scope::AppPaths::dataRoot();
    QDir root(rootPath);
    if (!root.mkpath(QStringLiteral("thermal_state_capture"))) {
        if (error) *error = QObject::tr("无法创建原始采集目录");
        return {};
    }
    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString folderName = QStringLiteral("thermal_%1").arg(stamp);
    const QString folderPath = root.filePath(
        QStringLiteral("thermal_state_capture/%1").arg(folderName));
    if (!root.mkpath(QStringLiteral("thermal_state_capture/%1").arg(folderName))) {
        if (error) *error = QObject::tr("无法创建原始采集结果目录");
        return {};
    }

    QByteArray bytes;
    bytes.reserve(frames.size() * RawFrameSize);
    for (const QByteArray &frame : frames) bytes.append(frame);
    const QByteArray digest = QCryptographicHash::hash(
        bytes, QCryptographicHash::Sha256).toHex();

    QSaveFile rawFile(QDir(folderPath).filePath(QStringLiteral("raw_capture.bin")));
    if (!rawFile.open(QIODevice::WriteOnly)
        || rawFile.write(bytes) != bytes.size() || !rawFile.commit()) {
        if (error) *error = QObject::tr("写入原始采集文件失败：%1")
                                  .arg(rawFile.errorString());
        return {};
    }

    QJsonObject protocol;
    protocol.insert(QStringLiteral("transport"), QStringLiteral("USB CDC-NCM / HTTP"));
    protocol.insert(QStringLiteral("raw_endpoint"), QStringLiteral("/frame.adc16"));
    protocol.insert(QStringLiteral("monitor_endpoint"), QStringLiteral("/frame.temp8"));
    protocol.insert(QStringLiteral("raw_frame_bytes"), RawFrameSize);
    QJsonObject capture;
    capture.insert(QStringLiteral("frame_count"), frames.size());
    capture.insert(QStringLiteral("width"), FrameWidth);
    capture.insert(QStringLiteral("height"), FrameHeight);
    capture.insert(QStringLiteral("row_metadata_words"), 2);
    QJsonObject archive;
    archive.insert(QStringLiteral("file"), QStringLiteral("raw_capture.bin"));
    archive.insert(QStringLiteral("sha256"), QString::fromLatin1(digest));
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"),
                   QStringLiteral("tn160.thermal-state-raw-capture.v1"));
    manifest.insert(QStringLiteral("captured_at"),
                   QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    manifest.insert(QStringLiteral("protocol"), protocol);
    manifest.insert(QStringLiteral("capture"), capture);
    manifest.insert(QStringLiteral("archive"), archive);

    QSaveFile manifestFile(QDir(folderPath).filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::WriteOnly)) {
        if (error) *error = QObject::tr("写入原始采集清单失败：%1")
                                  .arg(manifestFile.errorString());
        return {};
    }
    const QByteArray json = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (manifestFile.write(json) != json.size() || !manifestFile.commit()) {
        if (error) *error = QObject::tr("提交原始采集清单失败：%1")
                                  .arg(manifestFile.errorString());
        return {};
    }
    return folderPath;
}

} // namespace

LivePage::LivePage(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host),
      m_session(std::make_unique<Tn160Engine>(), this)
{
    setObjectName(QStringLiteral("tn160LivePage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_workspace = new WorkspaceCanvas(QStringLiteral("tn160/live-workbench-v5"));

    auto *imageContent = new QWidget;
    imageContent->setObjectName(QStringLiteral("componentContent"));
    auto *imageLayout = new QVBoxLayout(imageContent);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    m_thermal = new ThermalView;
    m_thermal->setObjectName(QStringLiteral("tn160ThermalView"));
    m_thermal->setCrosshairVisible(true);
    m_thermal->setExtremaVisible(true);
    imageLayout->addWidget(m_thermal, 1);

    // The host owns the panel chrome and layout. The component owns the
    // content offered to those fixed slots through IWorkspaceUiProvider.
    m_trend = new TrendWidget(this);
    m_trend->setObjectName(QStringLiteral("tn160TrendWidget"));
    m_trend->hide();

    m_workspace->addComponent(QStringLiteral("thermal"), tr("TN160 热图"),
                              imageContent, QRectF(0.0, 0.0, 1.0, 1.0),
                              QSize(380, 220));

    m_contextPanel = new QWidget(this);
    m_contextPanel->setObjectName(QStringLiteral("workspaceContextContent"));
    auto *contextLayout = new QVBoxLayout(m_contextPanel);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    contextLayout->setSpacing(8);
    auto *connectionHint = new QLabel(tr("TN160 USB CDC-NCM 监控"), m_contextPanel);
    connectionHint->setObjectName(QStringLiteral("pageSubtitle"));
    contextLayout->addWidget(connectionHint);
    contextLayout->addWidget(new QLabel(tr("USB NCM 地址"), m_contextPanel));
    m_port = new QComboBox(m_contextPanel);
    m_port->setEditable(true);
    m_port->setObjectName(QStringLiteral("tn160Port"));
    m_port->setPlaceholderText(tr("http://192.168.17.1"));
    contextLayout->addWidget(m_port);
    auto *connectionActions = new QHBoxLayout;
    m_scan = new QPushButton(tr("扫描设备"), m_contextPanel);
    m_capture = new QPushButton(tr("开始采集"), m_contextPanel);
    m_capture->setProperty("kind", QStringLiteral("primary"));
    connectionActions->addWidget(m_scan);
    connectionActions->addWidget(m_capture, 1);
    contextLayout->addLayout(connectionActions);
    m_state = new QLabel(tr("未连接"), m_contextPanel);
    m_state->setObjectName(QStringLiteral("statusPill"));
    contextLayout->addWidget(m_state);

    auto *advanced = new QFrame(m_contextPanel);
    advanced->setObjectName(QStringLiteral("workspaceAdvancedSection"));
    auto *advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(6);
    auto *advancedToggle = new QPushButton(tr("原始数据与导出"), advanced);
    advancedToggle->setObjectName(QStringLiteral("workspaceAdvancedToggle"));
    advancedToggle->setCheckable(true);
    auto *advancedBody = new QWidget(advanced);
    advancedBody->setObjectName(QStringLiteral("workspaceAdvancedBody"));
    auto *advancedBodyLayout = new QVBoxLayout(advancedBody);
    advancedBodyLayout->setContentsMargins(0, 0, 0, 0);
    advancedBodyLayout->setSpacing(6);
    auto *rawCount = new QSpinBox(advancedBody);
    rawCount->setRange(1, MaxRawCaptureFrames);
    rawCount->setValue(32);
    auto *rawCapture = new QPushButton(tr("采集原始帧"), advancedBody);
    rawCapture->setProperty("kind", QStringLiteral("quiet"));
    auto *exportPngButton = new QPushButton(tr("导出趋势 PNG"), advancedBody);
    auto *exportCsvButton = new QPushButton(tr("导出诊断 CSV"), advancedBody);
    advancedBodyLayout->addWidget(new QLabel(tr("原始帧数量"), advancedBody));
    advancedBodyLayout->addWidget(rawCount);
    advancedBodyLayout->addWidget(rawCapture);
    advancedBodyLayout->addWidget(exportPngButton);
    advancedBodyLayout->addWidget(exportCsvButton);
    advancedBody->hide();
    advancedLayout->addWidget(advancedToggle);
    advancedLayout->addWidget(advancedBody);
    contextLayout->addWidget(advanced);
    contextLayout->addStretch();

    m_inspectorPanel = new QWidget(this);
    m_inspectorPanel->setObjectName(QStringLiteral("workspaceInspectorMetrics"));
    auto *inspectorLayout = new QVBoxLayout(m_inspectorPanel);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(0);
    const auto addGroup = [inspectorLayout](const QString &title) {
        auto *label = new QLabel(title);
        label->setObjectName(QStringLiteral("metricGroupLabel"));
        inspectorLayout->addWidget(label);
    };
    const auto addMetric = [inspectorLayout](const QString &name, QLabel **value,
                                             const bool primary = false) {
        auto *row = new QFrame;
        row->setObjectName(primary ? QStringLiteral("metricPrimary")
                                   : QStringLiteral("metricRow"));
        row->setFixedHeight(30);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(4, 0, 4, 0);
        auto *label = new QLabel(name, row);
        label->setObjectName(primary ? QStringLiteral("metricPrimaryLabel")
                                     : QStringLiteral("metricLabel"));
        *value = new QLabel(primary ? QStringLiteral("--.-- °C")
                                    : QStringLiteral("—"), row);
        (*value)->setObjectName(primary ? QStringLiteral("metricPrimaryValue")
                                        : QStringLiteral("metricValue"));
        (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(label, 1);
        rowLayout->addWidget(*value);
        inspectorLayout->addWidget(row);
    };
    addGroup(tr("采集"));
    addMetric(tr("状态"), &m_inspectorStatus);
    addMetric(tr("帧率"), &m_inspectorFps);
    addGroup(tr("温度"));
    addMetric(QStringLiteral("Center 5×5"), &m_inspectorCenter, true);
    addMetric(QStringLiteral("NTC"), &m_inspectorNtc);
    addGroup(tr("原始量"));
    addMetric(QStringLiteral("Center ADU"), &m_inspectorCenterAdu);
    addMetric(QStringLiteral("VTEMP ADU"), &m_inspectorVtemp);
    auto *diagnosticToggle = new QPushButton(tr("高级诊断"), m_inspectorPanel);
    diagnosticToggle->setObjectName(QStringLiteral("workspaceDiagnosticToggle"));
    diagnosticToggle->setCheckable(true);
    m_inspectorDiagnostics = new QLabel(tr("等待完整 TN160 遥测帧"), m_inspectorPanel);
    m_inspectorDiagnostics->setObjectName(QStringLiteral("workspaceDiagnosticDetails"));
    m_inspectorDiagnostics->setWordWrap(true);
    m_inspectorDiagnostics->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_inspectorDiagnostics->hide();
    inspectorLayout->addWidget(diagnosticToggle);
    inspectorLayout->addWidget(m_inspectorDiagnostics);
    inspectorLayout->addStretch();

    root->addWidget(m_workspace, 1);

    connect(m_workspace, &WorkspaceCanvas::focusModeChanged,
            this, &LivePage::focusModeChanged);
    connect(m_scan, &QPushButton::clicked, this, &LivePage::refreshPorts);
    connect(m_port, &QComboBox::editTextChanged,
            this, &LivePage::endpointChanged);
    connect(m_capture, &QPushButton::clicked, this, &LivePage::toggleCapture);
    connect(advancedToggle, &QPushButton::toggled, advancedBody, &QWidget::setVisible);
    connect(rawCapture, &QPushButton::clicked, this,
            [this, rawCount] {
                if (rawCaptureActive()) {
                    cancelRawCapture();
                } else {
                    startRawCapture(rawCount->value());
                }
            });
    connect(this, &LivePage::rawCaptureStateChanged, rawCapture,
            [this, rawCapture](const QString &state, const QString &) {
                const bool capturing = state == QStringLiteral("raw-capturing");
                rawCapture->setText(capturing ? tr("停止原始采集")
                                              : tr("采集原始帧"));
                rawCapture->setProperty(
                    "kind", capturing ? QStringLiteral("warning")
                                       : QStringLiteral("quiet"));
                rawCapture->setEnabled(state != QStringLiteral("raw-restoring"));
                rawCapture->style()->unpolish(rawCapture);
                rawCapture->style()->polish(rawCapture);
            });
    connect(exportPngButton, &QPushButton::clicked, this, &LivePage::exportPng);
    connect(exportCsvButton, &QPushButton::clicked, this, &LivePage::exportCsv);
    connect(diagnosticToggle, &QPushButton::toggled,
            m_inspectorDiagnostics, &QWidget::setVisible);
    connect(this, &LivePage::connectionStateChanged, this,
            [this](const QString &, const QString &text) {
                if (m_inspectorStatus) m_inspectorStatus->setText(text);
            });
    connect(this, &LivePage::telemetryChanged, this,
            [this](const QString &center, const QString &ntc,
                   const QString &centerAdu, const QString &vtemp,
                   const QString &fps) {
                if (m_inspectorCenter) m_inspectorCenter->setText(center);
                if (m_inspectorNtc) m_inspectorNtc->setText(ntc);
                if (m_inspectorCenterAdu) m_inspectorCenterAdu->setText(centerAdu);
                if (m_inspectorVtemp) m_inspectorVtemp->setText(vtemp);
                if (m_inspectorFps) m_inspectorFps->setText(fps);
            });
    connect(this, &LivePage::diagnosticsChanged, this,
            [this](const QString &details) {
                if (m_inspectorDiagnostics) m_inspectorDiagnostics->setText(details);
            });
    connect(&m_session, &Tn160NcmTransport::stateChanged,
            this, &LivePage::handleState);
    connect(&m_session, &Tn160NcmTransport::errorOccurred,
            this, &LivePage::handleError);
    connect(&m_session, &Tn160NcmTransport::packetsDecoded,
            this, &LivePage::handleDecodedPackets);
    m_uiRefresh.setSingleShot(true);
    m_uiRefresh.setInterval(UiFrameIntervalMs);
    m_uiRefresh.setTimerType(Qt::PreciseTimer);
    connect(&m_uiRefresh, &QTimer::timeout, this, &LivePage::flushPendingUi);
    m_rawTimeout.setSingleShot(true);
    connect(&m_rawTimeout, &QTimer::timeout, this, &LivePage::handleRawTimeout);
    m_monitorTimeout.setSingleShot(true);
    connect(&m_monitorTimeout, &QTimer::timeout,
            this, &LivePage::handleMonitorTimeout);
    refreshPorts();
}

QWidget *LivePage::contextPanel() const noexcept
{
    return m_contextPanel;
}

QWidget *LivePage::inspectorPanel() const noexcept
{
    return m_inspectorPanel;
}

QWidget *LivePage::outputPanel() const noexcept
{
    return m_trend;
}

bool LivePage::activateLibraryComponent(const QString &componentId)
{
    return m_workspace
        && m_workspace->addOrShowComponentAt(componentId, QPoint(-1, -1));
}

void LivePage::shutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    clearPendingUi();
    m_rawTimeout.stop();
    m_monitorTimeout.stop();
    m_monitorAwaitingFirstFrame = false;
    m_monitorGapActive = false;
    m_rawCapture = false;
    m_rawRestoring = false;
    m_rawGapActive = false;
    m_monitorSilence.invalidate();
    m_rawSilence.invalidate();
    m_session.shutdown();
    m_ioTasks.closeAndWait();
}

void LivePage::refreshPorts()
{
    if (m_port->currentText().trimmed().isEmpty()) {
        setPort(QStringLiteral("http://192.168.17.1"));
    }
    emit scanRequested();
}

void LivePage::toggleCapture()
{
    if (m_rawCapture || m_rawRestoring) {
        cancelRawCapture();
        return;
    }
    if (m_session.isActive() || m_session.isConnecting()) {
        clearPendingUi();
        m_monitorTimeout.stop();
        m_monitorAwaitingFirstFrame = false;
        m_monitorGapActive = false;
        m_monitorSilence.invalidate();
        m_session.close();
        return;
    }
    if (m_port->currentText().isEmpty()) {
        handleError(tr("请先填写 USB NCM 地址"));
        return;
    }
    m_session.setDecodingEnabled(true);
    m_session.resetDecoder();
    m_trend->clear();
    m_thermal->clear();
    clearDiagnosticHistory();
    emit diagnosticsChanged(tr("等待完整 TN160 遥测帧"));
    clearPendingUi();
    m_elapsed.restart();
    m_session.open(m_port->currentText());
}

void LivePage::startRawCapture(const int frameCount)
{
    if (m_rawCapture || m_rawRestoring) return;
    if (!m_session.isActive()) {
        handleError(tr("请先连接 TN160，再开始原始帧采集"));
        return;
    }
    m_rawFrames.clear();
    m_rawTargetFrames = std::clamp(frameCount, 1, MaxRawCaptureFrames);
    m_rawCapture = true;
    m_rawRestoring = false;
    m_rawCancelled = false;
    m_rawGapActive = false;
    m_rawFailureMessage.clear();
    QString engineError;
    if (!m_session.replaceEngine(std::make_unique<Tn160RawFrameEngine>(),
                                 &engineError)) {
        handleError(tr("无法启动原始帧解析：%1").arg(engineError));
        return;
    }
    clearPendingUi();
    m_monitorTimeout.stop();
    m_monitorAwaitingFirstFrame = false;
    m_monitorGapActive = false;
    m_monitorSilence.invalidate();
    m_session.write(QByteArray(1, char(RawCommand)));
    m_rawSilence.restart();
    m_rawTimeout.start(FfcSoftSilenceMs);
    setState(QStringLiteral("raw-capturing"),
             tr("原始采集中 %1/%2").arg(0).arg(m_rawTargetFrames));
    if (m_host) m_host->log(QStringLiteral("info"),
                            tr("TN160 通过 USB NCM 开始采集 %1 帧原始 RAW_U16")
                                .arg(m_rawTargetFrames));
}

void LivePage::cancelRawCapture()
{
    if (!m_rawCapture && !m_rawRestoring) return;
    if (m_rawRestoring) return;
    m_rawTimeout.stop();
    m_rawCancelled = true;
    m_rawCapture = false;
    m_rawGapActive = false;
    m_rawSilence.invalidate();
    m_rawFrames.clear();
    restoreMonitorAfterRawCapture();
    if (m_host) m_host->log(QStringLiteral("warning"), tr("TN160 原始帧采集已取消"));
}

bool LivePage::rawCaptureActive() const noexcept
{
    return m_rawCapture || m_rawRestoring;
}

void LivePage::clearDiagnosticSession()
{
    if (m_trend) m_trend->clear();
    clearDiagnosticHistory();
    if (m_elapsed.isValid()) m_elapsed.restart();
    emit diagnosticsChanged(tr("诊断历史已清空，等待新帧"));
    if (m_host) {
        m_host->log(QStringLiteral("info"), tr("TN160 诊断趋势与导出历史已清空"));
        m_host->setStatus(tr("TN160 · 诊断历史已清空"));
    }
}

void LivePage::exportPng()
{
    if (!m_trend || m_trend->historySampleCount() < 2) {
        if (m_host) {
            m_host->log(QStringLiteral("warning"),
                        tr("TN160 诊断样本不足，无法导出 PNG"));
            m_host->setStatus(tr("TN160 · 样本不足，无法导出 PNG"));
        }
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("保存完整历史趋势图（PNG）"), diagnosticExportPath(QStringLiteral("png")),
        tr("PNG 图像 (*.png)"));
    if (path.isEmpty()) return;
    exportPngToPath(path);
}

bool LivePage::exportPngToPath(const QString &requestedPath)
{
    if (!m_trend || m_trend->historySampleCount() < 2
        || requestedPath.trimmed().isEmpty()) {
        return false;
    }
    const QString path = withExtension(requestedPath.trimmed(), QStringLiteral("png"));
    const QImage image = m_trend->renderHistoryImage();
    QPointer<LivePage> page(this);
    const bool accepted = m_ioTasks.run([page, path, image] {
        QString error;
        savePngImage(path, image, &error);
        if (!page) return;
        QMetaObject::invokeMethod(page, [page, path, error] {
            if (page) page->completeDiagnosticExport(QStringLiteral("PNG"), path, error);
        }, Qt::QueuedConnection);
    });
    if (!accepted) {
        completeDiagnosticExport(QStringLiteral("PNG"), path,
                                 tr("后台 I/O 队列已满"));
    } else if (m_host) {
        m_host->setStatus(tr("TN160 · 正在导出完整历史 PNG"));
    }
    return accepted;
}

void LivePage::exportCsv()
{
    if (m_diagnosticHistory.size() < 2) {
        if (m_host) {
            m_host->log(QStringLiteral("warning"),
                        tr("TN160 诊断样本不足，无法导出 CSV"));
            m_host->setStatus(tr("TN160 · 样本不足，无法导出 CSV"));
        }
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("保存完整诊断数据（CSV）"), diagnosticExportPath(QStringLiteral("csv")),
        tr("CSV 数据 (*.csv)"));
    if (path.isEmpty()) return;
    exportCsvToPath(path);
}

bool LivePage::exportCsvToPath(const QString &requestedPath)
{
    if (m_diagnosticHistory.size() < 2 || requestedPath.trimmed().isEmpty()) {
        return false;
    }
    const QString path = withExtension(requestedPath.trimmed(), QStringLiteral("csv"));
    const QVector<DiagnosticRecord> records = m_diagnosticHistory;
    QPointer<LivePage> page(this);
    const bool accepted = m_ioTasks.run([page, path, records] {
        QString error;
        saveDiagnosticCsv(path, records, &error);
        if (!page) return;
        QMetaObject::invokeMethod(page, [page, path, error] {
            if (page) page->completeDiagnosticExport(QStringLiteral("CSV"), path, error);
        }, Qt::QueuedConnection);
    });
    if (!accepted) {
        completeDiagnosticExport(QStringLiteral("CSV"), path,
                                 tr("后台 I/O 队列已满"));
    } else if (m_host) {
        m_host->setStatus(tr("TN160 · 正在导出完整诊断 CSV"));
    }
    return accepted;
}

void LivePage::setPort(const QString &port)
{
    if (!m_port) return;
    const QString normalized = port.trimmed();
    if (normalized.isEmpty()) return;
    int index = m_port->findText(normalized, Qt::MatchFixedString);
    if (index < 0) {
        m_port->addItem(normalized, tr("手动地址"));
        index = m_port->count() - 1;
    }
    m_port->setCurrentIndex(index);
}

void LivePage::setDiscoveredEndpoints(const QStringList &endpoints)
{
    const QString current = m_port ? m_port->currentText().trimmed()
                                   : QString{};
    for (const QString &endpoint : endpoints) {
        if (m_port && m_port->findText(endpoint, Qt::MatchFixedString) < 0) {
            m_port->addItem(endpoint);
        }
    }
    if (!current.isEmpty()) setPort(current);
}

void LivePage::setBaud(const int baud)
{
    Q_UNUSED(baud)
}

int LivePage::baudRate() const noexcept
{
    return 0;
}

bool LivePage::connectionActive() const noexcept
{
    return m_session.isActive() || m_session.isConnecting();
}

void LivePage::handleState(const QString &state)
{
    const bool locked = state != QStringLiteral("disconnected");
    QString displayState = state;
    QString text;
    if (state == QStringLiteral("connected")) {
        m_session.setDecodingEnabled(true);
        m_session.resetDecoder();
        m_monitorAwaitingFirstFrame = true;
        m_monitorGapActive = false;
        m_session.write(QByteArray(1, char(MonitorCommand)));
        m_monitorSilence.restart();
        m_monitorTimeout.start(FfcSoftSilenceMs);
        displayState = QStringLiteral("monitor-starting");
        text = tr("等待首帧");
        if (m_host) {
            m_host->log(QStringLiteral("info"),
                        tr("TN160 已建立 USB NCM 监控，等待首帧"));
        }
    } else if (state == QStringLiteral("connecting")) {
        text = tr("连接中");
    } else if (state == QStringLiteral("disconnecting")) {
        m_monitorTimeout.stop();
        m_monitorAwaitingFirstFrame = false;
        m_monitorGapActive = false;
        m_monitorSilence.invalidate();
        text = tr("断开中");
    } else {
        m_monitorTimeout.stop();
        m_monitorAwaitingFirstFrame = false;
        m_monitorGapActive = false;
        m_monitorSilence.invalidate();
        text = tr("未连接");
    }
    setState(displayState, text);
    m_port->setEnabled(!locked);
    m_scan->setEnabled(!locked);
    m_capture->setText(
        state == QStringLiteral("connected") ? tr("停止采集")
        : state == QStringLiteral("connecting") ? tr("取消连接")
                                                  : tr("开始采集"));
    m_capture->setEnabled(state != QStringLiteral("disconnecting"));
    m_capture->setProperty("kind", state == QStringLiteral("connecting")
                                         ? QStringLiteral("warning")
                                         : QStringLiteral("primary"));
    m_capture->style()->unpolish(m_capture);
    m_capture->style()->polish(m_capture);
    if (m_host) m_host->setStatus(tr("TN160 · %1").arg(text));
}

void LivePage::handleData(const QByteArray &data)
{
    if (m_shuttingDown) return;
    m_session.ingest(data);
}

void LivePage::handleDecodedPackets(const PacketList &packets)
{
    if (m_shuttingDown) return;
    if (m_rawCapture) {
        for (const Packet &packet : packets) {
            const auto *image = std::get_if<ImagePacket>(&packet);
            if (image && image->source == QString::fromLatin1(Tn160RawFrameSource)) {
                handleRawFrame(image->pixels);
                if (!m_rawCapture) break;
            }
        }
        return;
    }
    const bool restored = processPackets(packets);
    if (m_rawRestoring && restored) {
        m_rawRestoring = false;
        const bool success = !m_rawCancelled && m_rawFailureMessage.isEmpty();
        const QString message = m_rawFailureMessage;
        QTimer::singleShot(0, this, [this, success, message] {
            finishRawCapture(success, message);
        });
    }
}

bool LivePage::processPackets(const PacketList &packets)
{
    if (!m_elapsed.isValid()) m_elapsed.start();
    bool hasImage = false;
    for (const Packet &packet : packets) {
        if (const auto *image = std::get_if<ImagePacket>(&packet)) {
            hasImage = true;
            m_pendingUiFrames.enqueue(PendingUiFrame{*image, std::nullopt});
            while (m_pendingUiFrames.size() > UiFrameQueueCapacity) {
                m_pendingUiFrames.dequeue();
            }
        } else if (const auto *sample = std::get_if<SamplePacket>(&packet)) {
            if (sample->values.size() >= 4) {
                const double seconds = std::max(m_elapsed.elapsed() / 1000.0, 0.001);
                m_trend->addSample({
                    seconds,
                    sampleValue(*sample, CenterPointC),
                    sampleValue(*sample, CenterRoiC),
                    sampleValue(*sample, NtcC),
                    sampleValue(*sample, CenterPointAdu),
                    sampleValue(*sample, CenterRoiAdu),
                    sampleValue(*sample, VtempRaw),
                    sampleValue(*sample, VtempSmooth),
                    sampleInteger(*sample, NucDecision, 255),
                    sampleInteger(*sample, NucCount),
                    sampleInteger(*sample, NucDg),
                    sampleInteger(*sample, NucDv),
                    sampleInteger(*sample, NucDn),
                });
                appendDiagnosticSample(*sample, seconds);
                if (!m_pendingUiFrames.isEmpty()
                    && !m_pendingUiFrames.back().sample) {
                    m_pendingUiFrames.back().sample = *sample;
                }
            }
        }
    }
    if (hasImage) {
        const bool firstFrame = m_monitorAwaitingFirstFrame;
        const bool recovered = m_monitorGapActive;
        const qint64 silenceMs = m_monitorSilence.isValid()
            ? m_monitorSilence.elapsed() : 0;
        m_monitorAwaitingFirstFrame = false;
        m_monitorGapActive = false;
        m_monitorSilence.restart();
        if (!m_rawRestoring) {
            armMonitorWatchdog();
            if (firstFrame || recovered) {
                setState(QStringLiteral("connected"), tr("采集中"));
                if (m_host) {
                    if (recovered) {
                        m_host->log(QStringLiteral("info"),
                                    tr("TN160 图像流已恢复，空窗 %1 ms；USB NCM 链路未断开")
                                        .arg(silenceMs));
                    }
                    m_host->setStatus(tr("TN160 · 采集中"));
                }
            }
        }
    }
    if (hasImage) scheduleUiRefresh();
    return hasImage;
}

void LivePage::scheduleUiRefresh()
{
    if (m_uiRefresh.isActive() || m_pendingUiFrames.isEmpty()) return;
    const int delayMs = m_lastUiFrame.isValid()
        ? std::max(0, UiFrameIntervalMs - int(m_lastUiFrame.elapsed())) : 0;
    m_uiRefresh.start(delayMs);
}

void LivePage::armMonitorWatchdog()
{
    if (!m_session.isActive() || m_rawCapture || m_rawRestoring) return;
    if (!m_monitorSilence.isValid()) m_monitorSilence.start();
    m_monitorTimeout.start(FfcSoftSilenceMs);
}

void LivePage::flushPendingUi()
{
    if (m_pendingUiFrames.isEmpty()) return;
    const PendingUiFrame pending = m_pendingUiFrames.dequeue();
    m_thermal->setFrame(pending.image.pixels,
                        pending.image.width, pending.image.height);
    const qint64 nowMs = m_elapsed.elapsed();
    m_renderFrameTimesMs.enqueue(nowMs);
    while (!m_renderFrameTimesMs.isEmpty()
           && m_renderFrameTimesMs.head() < nowMs - FpsWindowMs) {
        m_renderFrameTimesMs.dequeue();
    }
    if (pending.sample) updateSample(*pending.sample);
    m_lastUiFrame.restart();
    scheduleUiRefresh();
}

void LivePage::handleRawFrame(const QByteArray &frame)
{
    if (!m_rawCapture || frame.size() != RawFrameSize) return;
    if (m_rawGapActive) {
        const qint64 silenceMs = m_rawSilence.isValid()
            ? m_rawSilence.elapsed() : 0;
        m_rawFrames.clear();
        m_rawGapActive = false;
        if (m_host) {
            m_host->log(QStringLiteral("info"),
                        tr("TN160 原始流已从疑似 FFC 空窗恢复（%1 ms）；"
                           "已丢弃空窗前帧并从 0 重新采集")
                            .arg(silenceMs));
        }
    }
    m_rawFrames.append(frame);
    m_rawSilence.restart();
    m_rawTimeout.start(FfcSoftSilenceMs);
    const int count = m_rawFrames.size();
    if (count == 1 || count % 8 == 0 || count == m_rawTargetFrames) {
        setState(QStringLiteral("raw-capturing"),
                 tr("原始采集中 %1/%2").arg(count).arg(m_rawTargetFrames));
    }
    if (count >= m_rawTargetFrames) {
        m_rawCapture = false;
        m_rawTimeout.stop();
        restoreMonitorAfterRawCapture();
    }
}

void LivePage::restoreMonitorAfterRawCapture()
{
    if (!m_session.isActive()) {
        finishRawCapture(false, tr("恢复监控模式失败：USB NCM 已断开"));
        return;
    }
    m_rawRestoring = true;
    m_rawGapActive = false;
    m_rawSilence.invalidate();
    QString engineError;
    if (!m_session.replaceEngine(std::make_unique<Tn160Engine>(), &engineError)) {
        m_rawRestoring = false;
        finishRawCapture(false, tr("恢复监控解析失败：%1").arg(engineError));
        return;
    }
    m_session.write(QByteArray(1, char(MonitorCommand)));
    setState(QStringLiteral("raw-restoring"), tr("正在恢复监控模式"));
    m_rawTimeout.start(MonitorHardSilenceMs);
}

void LivePage::finishRawCapture(const bool success, const QString &message)
{
    m_rawTimeout.stop();
    m_rawGapActive = false;
    m_rawSilence.invalidate();
    if (m_rawCancelled) {
        if (m_host) m_host->log(QStringLiteral("warning"), tr("TN160 原始帧采集已取消"));
        setState(m_session.isActive() ? QStringLiteral("connected")
                                        : QStringLiteral("disconnected"),
                 m_session.isActive() ? tr("采集中") : tr("未连接"));
        m_rawFrames.clear();
        m_rawFailureMessage.clear();
        m_rawCancelled = false;
        armMonitorWatchdog();
        return;
    }
    if (!success) {
        if (m_host) m_host->log(QStringLiteral("error"), tr("TN160 原始采集失败：%1").arg(message));
        setState(m_session.isActive() ? QStringLiteral("connected")
                                        : QStringLiteral("error"),
                 m_session.isActive() ? tr("采集中") : tr("错误"));
        m_rawFrames.clear();
        m_rawFailureMessage.clear();
        armMonitorWatchdog();
        return;
    }
    const int frameCount = m_rawFrames.size();
    QVector<QByteArray> frames = std::move(m_rawFrames);
    m_rawFrames.clear();
    QPointer<LivePage> page(this);
    const bool accepted = m_ioTasks.run(
        [page, frames = std::move(frames), frameCount]() mutable {
            QString error;
            const QString path = saveRawCapture(frames, &error);
            if (!page) return;
            QMetaObject::invokeMethod(page, [page, path, error, frameCount] {
                if (page) page->completeRawCaptureSave(path, error, frameCount);
            }, Qt::QueuedConnection);
        });
    if (!accepted) {
        completeRawCaptureSave({}, tr("后台 I/O 队列已满"), frameCount);
    } else {
        setState(QStringLiteral("raw-saving"), tr("原始采集保存中"));
    }
    m_rawFailureMessage.clear();
    armMonitorWatchdog();
}

void LivePage::completeRawCaptureSave(const QString &path, const QString &error,
                                      const int frameCount)
{
    if (m_shuttingDown) return;
    if (path.isEmpty()) {
        if (m_host) m_host->log(QStringLiteral("error"),
                                tr("TN160 原始采集保存失败：%1").arg(error));
        setState(m_session.isActive() ? QStringLiteral("connected")
                                        : QStringLiteral("error"),
                 m_session.isActive() ? tr("采集中") : tr("保存失败"));
    } else {
        if (m_host) {
            m_host->log(QStringLiteral("info"),
                        tr("TN160 原始采集已保存：%1（%2 帧）")
                            .arg(path).arg(frameCount));
        }
        setState(m_session.isActive() ? QStringLiteral("connected")
                                      : QStringLiteral("disconnected"),
                 m_session.isActive() ? tr("采集中") : tr("未连接"));
    }
}

void LivePage::updateSample(const SamplePacket &sample)
{
    if (sample.values.size() < 4) return;
    const double fps = renderFpsValue();
    ThermalFrameInfo info;
    info.centerPointC = sampleValue(sample, CenterPointC);
    info.centerRoiC = sampleValue(sample, CenterRoiC);
    info.centerPointAdu = sampleValue(sample, CenterPointAdu);
    info.centerRoiAdu = sampleValue(sample, CenterRoiAdu);
    info.ntcC = sampleValue(sample, NtcC);
    info.tLowC = sampleValue(sample, TLowC);
    info.tHighC = sampleValue(sample, THighC);
    info.vtemp = sampleValue(sample, VtempRaw);
    info.fps = fps;
    m_thermal->setFrameInfo(info);

    const auto formatMetricTemperature = [](const double value) {
        return std::isfinite(value)
            ? QStringLiteral("%1 °C").arg(value, 0, 'f', 2)
            : QStringLiteral("—");
    };
    const QString center = formatMetricTemperature(info.centerRoiC);
    const QString ntc = formatMetricTemperature(info.ntcC);
    const QString centerAdu = formattedInteger(info.centerRoiAdu);
    const QString vtemp = formattedInteger(info.vtemp);
    emit telemetryChanged(center, ntc, centerAdu, vtemp, renderFpsText());

    const double vtempSmooth = sampleValue(sample, VtempSmooth);
    const double vtempReference = m_diagnosticHistory.isEmpty()
        ? std::numeric_limits<double>::quiet_NaN()
        : m_diagnosticHistory.constFirst().values[VtempRaw];
    const double vtempDeltaValue = info.vtemp - vtempReference;
    const QString vtempDelta = std::isfinite(info.vtemp)
        && std::isfinite(vtempReference)
        ? QStringLiteral("%1%2")
              .arg(vtempDeltaValue >= 0.0 ? QStringLiteral("+") : QString(),
                   QString::number(vtempDeltaValue, 'f', 0))
        : QStringLiteral("—");
    const int decision = sampleInteger(sample, NucDecision, 255);
    const int count = sampleInteger(sample, NucCount);
    const double bootDelta = sampleValue(sample, BootDeltaAdu);
    const QString bootDeltaText = std::isfinite(bootDelta) && qRound(bootDelta) != 0xffff
        ? formattedInteger(bootDelta) : QStringLiteral("—");
    const QString details = QStringLiteral(
        "中心点  %1 / %2 ADU\n"
        "Center 5×5  %3 / %4 ADU\n"
        "NTC  %5 / %6 ADU · ref %7\n"
        "VTEMP  %8 / smooth %9 · Δ %10\n"
        "范围  %11~%12°C · smooth %13~%14 ADU\n"
        "anchor  %15 · effective %16 · mean_diff %17\n"
        "NUC  %18#%19 · dG/dV/dN %20/%21/%22\n"
        "comp  W/N/T %23/%24/%25 · boot %26/%27")
        .arg(formattedTemperature(info.centerPointC),
             formattedInteger(info.centerPointAdu),
             formattedTemperature(info.centerRoiC),
             formattedInteger(info.centerRoiAdu),
             formattedTemperature(info.ntcC),
             formattedInteger(sampleValue(sample, NtcAdu)),
             formattedInteger(sampleValue(sample, NtcRefAdu)),
             formattedInteger(info.vtemp),
             formattedInteger(vtempSmooth),
             vtempDelta,
             std::isfinite(info.tLowC) ? QString::number(info.tLowC, 'f', 1)
                                       : QStringLiteral("—"),
             std::isfinite(info.tHighC) ? QString::number(info.tHighC, 'f', 1)
                                        : QStringLiteral("—"),
             formattedInteger(sampleValue(sample, SmoothLowAdu)),
             formattedInteger(sampleValue(sample, SmoothHighAdu)),
             formattedInteger(sampleValue(sample, Anchor)),
             formattedInteger(sampleValue(sample, EffectiveAnchor)),
             std::isfinite(sampleValue(sample, MeanDiff))
                 ? QString::number(sampleValue(sample, MeanDiff), 'f', 1)
                 : QStringLiteral("—"),
             nucDecisionLabel(decision), QString::number(count),
             formattedInteger(sampleValue(sample, NucDg)),
             formattedInteger(sampleValue(sample, NucDv)),
             formattedInteger(sampleValue(sample, NucDn)),
             formattedInteger(sampleValue(sample, WarmupComp)),
             formattedInteger(sampleValue(sample, NormalComp)),
             formattedInteger(sampleValue(sample, TotalComp)),
             formattedInteger(sampleValue(sample, BootNtcAdu)), bootDeltaText);
    emit diagnosticsChanged(details);
}

void LivePage::appendDiagnosticSample(const SamplePacket &sample, const double seconds)
{
    DiagnosticRecord record;
    record.timestampUs = sample.timestampUs;
    record.seconds = seconds;
    record.values.fill(std::numeric_limits<double>::quiet_NaN());
    const int count = std::min(int(sample.values.size()), MonitorSampleValueCount);
    for (int index = 0; index < count; ++index) {
        record.values[index] = sample.values.at(index);
    }

    const int decision = sampleInteger(sample, NucDecision, 255);
    const int nucCount = sampleInteger(sample, NucCount);
    const int dg = sampleInteger(sample, NucDg);
    const int dv = sampleInteger(sample, NucDv);
    const int dn = sampleInteger(sample, NucDn);
    const bool changed = m_haveNucSignature
        && (decision != m_lastNucDecision || nucCount != m_lastNucCount
            || dg != m_lastNucDg || dv != m_lastNucDv || dn != m_lastNucDn);
    if (changed && decision != 255) record.ffcEvent = nucDecisionLabel(decision);
    m_lastNucDecision = decision;
    m_lastNucCount = nucCount;
    m_lastNucDg = dg;
    m_lastNucDv = dv;
    m_lastNucDn = dn;
    m_haveNucSignature = true;
    m_diagnosticHistory.append(std::move(record));
}

double LivePage::renderFpsValue() const
{
    if (m_renderFrameTimesMs.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const qint64 spanMs = m_renderFrameTimesMs.back() - m_renderFrameTimesMs.head();
    if (spanMs < FpsWarmupMs) return std::numeric_limits<double>::quiet_NaN();
    return (m_renderFrameTimesMs.size() - 1) * 1000.0 / spanMs;
}

QString LivePage::renderFpsText() const
{
    const double fps = renderFpsValue();
    if (!std::isfinite(fps)) return tr("测量中");
    return QStringLiteral("%1 fps").arg(fps, 0, 'f', 1);
}

void LivePage::handleError(const QString &message)
{
    if (m_shuttingDown) return;
    m_session.resetDecoder();
    clearPendingUi();
    m_rawTimeout.stop();
    m_monitorTimeout.stop();
    m_monitorAwaitingFirstFrame = false;
    m_monitorGapActive = false;
    m_rawCapture = false;
    m_rawRestoring = false;
    m_rawGapActive = false;
    m_monitorSilence.invalidate();
    m_rawSilence.invalidate();
    m_rawFrames.clear();
    m_rawFailureMessage.clear();
    m_rawCancelled = false;
    setState(QStringLiteral("error"), tr("错误"));
    if (m_host) {
        m_host->log(QStringLiteral("error"), QStringLiteral("TN160 · %1").arg(message));
        m_host->setStatus(tr("TN160 · %1").arg(message));
    }
}

void LivePage::clearPendingUi()
{
    m_uiRefresh.stop();
    m_pendingUiFrames.clear();
    m_renderFrameTimesMs.clear();
    m_lastUiFrame.invalidate();
}

void LivePage::clearDiagnosticHistory()
{
    m_diagnosticHistory.clear();
    m_haveNucSignature = false;
    m_lastNucDecision = 255;
    m_lastNucCount = 0;
    m_lastNucDg = 0;
    m_lastNucDv = 0;
    m_lastNucDn = 0;
}

void LivePage::completeDiagnosticExport(const QString &kind, const QString &path,
                                        const QString &error)
{
    if (m_shuttingDown || !m_host) return;
    if (!error.isEmpty()) {
        m_host->log(QStringLiteral("error"),
                    tr("TN160 %1 导出失败：%2").arg(kind, error));
        m_host->setStatus(tr("TN160 · %1 导出失败").arg(kind));
        return;
    }
    m_host->log(QStringLiteral("info"),
                tr("TN160 完整诊断 %1 已保存：%2").arg(kind, path));
    m_host->setStatus(tr("TN160 · %1 已保存").arg(kind));
}

void LivePage::handleRawTimeout()
{
    if (m_rawCapture) {
        const qint64 silenceMs = m_rawSilence.isValid()
            ? m_rawSilence.elapsed() : RawHardSilenceMs;
        if (silenceMs < FfcSoftSilenceMs) {
            m_rawTimeout.start(int(FfcSoftSilenceMs - silenceMs));
            return;
        }
        if (!m_rawGapActive) {
            m_rawGapActive = true;
            m_session.resetDecoder();
            setState(QStringLiteral("raw-capturing"),
                     tr("疑似 FFC · 等待原始图像恢复，恢复后重新计数"));
            if (m_host) {
                m_host->log(QStringLiteral("warning"),
                            tr("TN160 原始流暂停，USB NCM 保持连接；按 FFC 空窗等待恢复"));
            }
        }
        if (silenceMs < RawHardSilenceMs) {
            m_rawTimeout.start(int(RawHardSilenceMs - silenceMs));
            return;
        }
        m_rawCapture = false;
        m_rawGapActive = false;
        m_rawSilence.invalidate();
        m_rawFailureMessage = tr("连续 10 秒未收到完整原始帧");
        restoreMonitorAfterRawCapture();
        return;
    }
    if (m_rawRestoring) {
        m_rawRestoring = false;
        const QString message = m_rawFailureMessage.isEmpty()
            ? tr("恢复监控模式超时") : m_rawFailureMessage;
        finishRawCapture(false, message);
        if (m_session.isActive()) {
            m_session.close();
        }
    }
}

void LivePage::handleMonitorTimeout()
{
    if (!m_session.isActive() || m_rawCapture || m_rawRestoring) {
        return;
    }
    const qint64 silenceMs = m_monitorSilence.isValid()
        ? m_monitorSilence.elapsed() : MonitorHardSilenceMs;
    if (silenceMs < FfcSoftSilenceMs) {
        m_monitorTimeout.start(int(FfcSoftSilenceMs - silenceMs));
        return;
    }
    if (!m_monitorGapActive) {
        m_monitorGapActive = true;
        const QString text = m_monitorAwaitingFirstFrame
            ? tr("等待首帧 · 可能正在 FFC") : tr("FFC · 等待图像恢复");
        setState(QStringLiteral("connected"), text);
        if (m_host) {
            m_host->log(QStringLiteral("warning"),
                        tr("TN160 图像流暂停，USB NCM 保持连接；按 FFC 空窗等待恢复"));
            m_host->setStatus(tr("TN160 · %1").arg(text));
        }
    }
    if (silenceMs < MonitorHardSilenceMs) {
        m_monitorTimeout.start(int(MonitorHardSilenceMs - silenceMs));
        return;
    }
    m_monitorAwaitingFirstFrame = false;
    m_monitorGapActive = false;
    m_monitorSilence.invalidate();
    setState(QStringLiteral("error"), tr("图像超时"));
    const QString message = tr("USB NCM 保持连接但连续 10 秒未收到 TN160 图像；已停止采集并恢复监控");
    if (m_host) {
        m_host->log(QStringLiteral("error"), QStringLiteral("TN160 · %1").arg(message));
        m_host->setStatus(QStringLiteral("TN160 · %1").arg(message));
    }
    m_session.close();
}

void LivePage::setState(const QString &state, const QString &text)
{
    m_state->setText(text);
    m_state->setProperty("state", state);
    m_state->style()->unpolish(m_state);
    m_state->style()->polish(m_state);
    emit connectionStateChanged(state, text);
    emit rawCaptureStateChanged(state, text);
}

} // namespace serein::scope::tn160ncm
