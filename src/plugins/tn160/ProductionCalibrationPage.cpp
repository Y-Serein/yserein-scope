#include "ProductionCalibrationPage.h"

#include "ThermalView.h"
#include "Tn160PortSelection.h"
#include "Tn160ProtocolEngines.h"

#include <serein/scope/WorkspaceCanvas.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QtEndian>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>

namespace serein::scope::tn160 {
namespace {

constexpr int PixelCount = FrameWidth * FrameHeight;
constexpr int RawWordsPerRow = FrameWidth + 2;
constexpr int RawFrameSize = 2 + FrameHeight * RawWordsPerRow * 2 + 2;
constexpr int RawBaud = 2000000;
constexpr int DumpBaud = 5000000;
constexpr int FfcSoftSilenceMs = 700;
constexpr int RawHardSilenceMs = 10000;
constexpr int RequiredFrames = 32;
constexpr int StateBHighFrames = 16;
constexpr int MaximumFrames = 64;
constexpr int SnapshotFrames = 3;
constexpr qint64 MonitorTraceIntervalUs = 1000000;
constexpr qint64 TargetCycleUs = 120000000;
constexpr int MinimumStateBDeltaNtcAdu = 120;
constexpr int MinimumStateBFfcDelta = 2;
constexpr int ImageBytes = PixelCount * int(sizeof(quint16));
constexpr int GainBytes = PixelCount * int(sizeof(float));
constexpr int BadPointBytes = 10;
constexpr int WriteHeaderBytes = 64;
constexpr char ProductionFirmwareSha256[] =
    "b57c54b9bad0fb32be4f6dc23a3fe1975c068fb4e55718052bce38b6b7c8840e";
constexpr quint8 WriteCommand = 0xec;
constexpr quint8 RawCommand = 0xaa;
constexpr quint8 MonitorCommand = 0x11;
constexpr quint8 ExitToUvcCommand = 0x99;
constexpr quint8 WriteOk = 0x00;
constexpr quint8 WriteReadyImage = 0x10;
constexpr quint8 WriteReadyGain = 0x11;
constexpr quint8 WriteReadyBadPoints = 0x12;
constexpr quint8 WriteReadyHeader = 0x7f;
constexpr float ReferenceLowC = 0.0f;
constexpr float ReferenceHighC = 50.0f;
constexpr char ResetConfig[] =
    "4302727C252E02154018191C82060413917F8200F800000FFFFFFFE6AFEB702C851649000000002F";
constexpr char ResetAlternateConfig[] =
    "4302727C252E02154018191882060413917F8200F800000FFFFFFFE6AFEB702C851649000000002F";
constexpr char InitConfig[] =
    "4302727C252E02154018191C8206041F917F8200F800000FFFFFFFE6AFEB702C851649000000002F";

void appendLe16(QByteArray &bytes, const quint16 value)
{
    char raw[2];
    qToLittleEndian(value, reinterpret_cast<uchar *>(raw));
    bytes.append(raw, sizeof(raw));
}

void appendLe32(QByteArray &bytes, const quint32 value)
{
    char raw[4];
    qToLittleEndian(value, reinterpret_cast<uchar *>(raw));
    bytes.append(raw, sizeof(raw));
}

void appendLeFloat(QByteArray &bytes, const float value)
{
    appendLe32(bytes, std::bit_cast<quint32>(value));
}

quint16 readLe16(const QByteArray &bytes, const qsizetype offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

quint32 readLe32(const QByteArray &bytes, const qsizetype offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
}

float readLeFloat(const QByteArray &bytes, const qsizetype offset)
{
    return std::bit_cast<float>(readLe32(bytes, offset));
}

struct PreviewPanelChrome {
    QFrame *panel = nullptr;
    QVBoxLayout *bodyLayout = nullptr;
};

PreviewPanelChrome makePreviewPanel(const QString &title)
{
    auto *panel = new ComponentSurface(title);
    auto *bodyLayout = new QVBoxLayout(panel->contentHost());
    bodyLayout->setContentsMargins(10, 8, 10, 10);
    bodyLayout->setSpacing(6);
    return {panel, bodyLayout};
}

} // namespace

QString ProductionCalibrationPage::captureName(const CaptureKind kind)
{
    switch (kind) {
    case CaptureKind::Low: return tr("低温");
    case CaptureKind::High: return tr("高温");
    case CaptureKind::HighB: return tr("状态 B 高温");
    case CaptureKind::Offset: return tr("单点 offset");
    case CaptureKind::None: break;
    }
    return tr("未知");
}

int ProductionCalibrationPage::requiredFramesFor(const CaptureKind kind)
{
    return kind == CaptureKind::HighB ? StateBHighFrames : RequiredFrames;
}

ProductionCalibrationPage::ProductionCalibrationPage(IHostServices *host,
                                                       QWidget *parent)
    : QWidget(parent), m_host(host),
      m_transport(std::make_unique<Tn160Engine>(),
                  DecoderRuntime::configuredLimits(), this),
      m_verifyTransfer(this)
{
    setObjectName(QStringLiteral("tn160ProductionCalibrationPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(10);

    m_contextPanel = new QWidget(this);
    m_contextPanel->setObjectName(QStringLiteral("workspaceContextContent"));
    auto *contextLayout = new QVBoxLayout(m_contextPanel);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    contextLayout->setSpacing(8);
    auto *contextHint = new QLabel(tr("生产标定设备会话"), m_contextPanel);
    contextHint->setObjectName(QStringLiteral("pageSubtitle"));
    contextHint->setWordWrap(true);
    contextLayout->addWidget(contextHint);
    contextLayout->addWidget(new QLabel(tr("串口"), m_contextPanel));
    m_port = new QComboBox(m_contextPanel);
    m_port->setObjectName(QStringLiteral("tn160ProductionPort"));
    contextLayout->addWidget(m_port);
    auto *connectionActions = new QHBoxLayout;
    auto *scan = new QPushButton(tr("刷新"), m_contextPanel);
    m_connectionButton = new QPushButton(tr("连接"), m_contextPanel);
    m_connectionButton->setProperty("kind", QStringLiteral("primary"));
    connectionActions->addWidget(scan);
    connectionActions->addWidget(m_connectionButton, 1);
    contextLayout->addLayout(connectionActions);
    m_status = new QLabel(tr("未连接"), m_contextPanel);
    m_status->setObjectName(QStringLiteral("statusPill"));
    contextLayout->addWidget(m_status);
    contextLayout->addStretch();

    auto *control = new QFrame(this);
    control->setObjectName(QStringLiteral("resultPanel"));
    auto *controlLayout = new QHBoxLayout(control);
    controlLayout->setContentsMargins(10, 7, 10, 7);
    controlLayout->setSpacing(8);
    m_boardId = new QLineEdit(control);
    m_boardId->setObjectName(QStringLiteral("tn160ProductionBoardId"));
    m_boardId->setPlaceholderText(tr("PCB 标签（必填）"));
    m_boardId->setMinimumWidth(135);
    m_sensorId = new QLineEdit(control);
    m_sensorId->setObjectName(QStringLiteral("tn160ProductionSensorId"));
    m_sensorId->setPlaceholderText(tr("Sensor 标签 / 批次号（必填）"));
    m_sensorId->setMinimumWidth(160);
    m_newSensor = new QPushButton(tr("新 Sensor"), control);
    controlLayout->addWidget(m_boardId, 1);
    controlLayout->addWidget(m_sensorId, 1);
    controlLayout->addWidget(m_newSensor);
    root->addWidget(control);

    auto *workflow = new QFrame(this);
    workflow->setObjectName(QStringLiteral("resultPanel"));
    auto *workflowLayout = new QHBoxLayout(workflow);
    workflowLayout->setContentsMargins(10, 7, 10, 7);
    workflowLayout->setSpacing(7);
    m_lowButton = new QPushButton(tr("A1  0°C 32 帧"), workflow);
    m_highButton = new QPushButton(tr("A2  50°C 32 帧"), workflow);
    m_computeButton = new QPushButton(tr("3  计算两点 Gain"), workflow);
    m_importButton = new QPushButton(tr("导入 Sensor 包"), workflow);
    m_highBButton = new QPushButton(tr("B1  50°C 16 帧"), workflow);
    m_highBButton->setObjectName(QStringLiteral("tn160ProductionHighB"));
    m_offsetButton = new QPushButton(tr("B2  0°C / 最终 Offset"), workflow);
    m_writeButton = new QPushButton(tr("写入并回读"), workflow);
    m_writeButton->setProperty("kind", QStringLiteral("primary"));
    m_exportButton = new QPushButton(tr("导出完整包"), workflow);
    workflowLayout->addWidget(m_lowButton);
    workflowLayout->addWidget(m_highButton);
    workflowLayout->addWidget(m_computeButton);
    workflowLayout->addSpacing(8);
    workflowLayout->addWidget(m_importButton);
    workflowLayout->addWidget(m_highBButton);
    workflowLayout->addWidget(m_offsetButton);
    workflowLayout->addWidget(m_writeButton);
    workflowLayout->addWidget(m_exportButton);
    workflowLayout->addStretch(1);
    root->addWidget(workflow);

    m_summary = new QLabel(
        tr("正式 V1 量产流程 · 固定 0°C / 50°C · 每温点 32 个有效帧 · "
           "状态 B 按热状态就绪 · 120 s 仅为节拍目标，超出不判失败"),
        this);
    m_summary->setObjectName(QStringLiteral("chartTitle"));
    m_summary->setProperty("role", QStringLiteral("tn160ProductionSummary"));
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    auto *previews = new QGridLayout;
    previews->setContentsMargins(0, 0, 0, 0);
    previews->setHorizontalSpacing(10);
    const auto makePreview = [](const QString &title,
                                ThermalView::PaletteMode mode,
                                ThermalView **view) {
        const PreviewPanelChrome chrome = makePreviewPanel(title);
        *view = new ThermalView(chrome.panel);
        (*view)->setPaletteMode(mode);
        (*view)->setCrosshairVisible(false);
        (*view)->setMinimumSize(160, 105);
        chrome.bodyLayout->addWidget(*view, 1);
        return chrome.panel;
    };
    previews->addWidget(makePreview(tr("低温 32 帧均值"),
                                    ThermalView::PaletteMode::Grayscale,
                                    &m_lowView), 0, 0);
    previews->addWidget(makePreview(tr("高温 32 帧均值"),
                                    ThermalView::PaletteMode::Grayscale,
                                    &m_highView), 0, 1);
    previews->addWidget(makePreview(tr("温差图（高温 - 低温）"),
                                    ThermalView::PaletteMode::Grayscale,
                                    &m_diffView), 0, 2);
    previews->addWidget(makePreview(tr("最终 Flash 背景 Offset"),
                                    ThermalView::PaletteMode::Grayscale,
                                    &m_backgroundView), 1, 0);
    previews->addWidget(makePreview(tr("逐 Sensor Gain"),
                                    ThermalView::PaletteMode::Gain,
                                    &m_gainView), 1, 1);
    const PreviewPanelChrome qualityChrome = makePreviewPanel(tr("坏点 / 采集质量"));
    m_quality = new QLabel(tr("等待生产标定数据"), qualityChrome.panel);
    m_quality->setObjectName(QStringLiteral("tn160ProductionQuality"));
    m_quality->setWordWrap(true);
    m_quality->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_quality->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    qualityChrome.bodyLayout->addWidget(m_quality, 1);
    previews->addWidget(qualityChrome.panel, 1, 2);
    m_lowView->setObjectName(QStringLiteral("tn160ProductionLowView"));
    m_highView->setObjectName(QStringLiteral("tn160ProductionHighView"));
    m_diffView->setObjectName(QStringLiteral("tn160ProductionDiffView"));
    m_backgroundView->setObjectName(QStringLiteral("tn160ProductionBackgroundView"));
    m_gainView->setObjectName(QStringLiteral("tn160ProductionGainView"));
    for (int column = 0; column < 3; ++column) previews->setColumnStretch(column, 1);
    previews->setRowStretch(0, 1);
    previews->setRowStretch(1, 1);
    root->addLayout(previews, 1);

    m_session = new QLabel(tr("尚未创建生产标定记录"), this);
    m_session->setObjectName(QStringLiteral("badPointSummary"));
    m_session->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_session);
    m_timeout.setSingleShot(true);
    connect(m_newSensor, &QPushButton::clicked, this, &ProductionCalibrationPage::newSensorSession);
    connect(scan, &QPushButton::clicked, this, &ProductionCalibrationPage::refreshPorts);
    connect(m_connectionButton, &QPushButton::clicked,
            this, &ProductionCalibrationPage::toggleConnection);
    connect(m_lowButton, &QPushButton::clicked, this, &ProductionCalibrationPage::captureLow);
    connect(m_highButton, &QPushButton::clicked, this, &ProductionCalibrationPage::captureHigh);
    connect(m_highBButton, &QPushButton::clicked,
            this, &ProductionCalibrationPage::captureHighB);
    connect(m_computeButton, &QPushButton::clicked, this, &ProductionCalibrationPage::computeTwoPoint);
    connect(m_importButton, &QPushButton::clicked, this, &ProductionCalibrationPage::importCalibration);
    connect(m_offsetButton, &QPushButton::clicked, this, &ProductionCalibrationPage::captureOffset);
    connect(m_writeButton, &QPushButton::clicked, this, &ProductionCalibrationPage::writeAndVerify);
    connect(m_exportButton, &QPushButton::clicked, this, &ProductionCalibrationPage::exportCalibration);
    connect(&m_transport, &SerialDataSession::stateChanged,
            this, &ProductionCalibrationPage::handleTransportState);
    connect(&m_transport, &SerialDataSession::packetsDecoded,
            this, &ProductionCalibrationPage::handleDecodedPackets);
    connect(&m_transport, &SerialDataSession::errorOccurred,
            this, &ProductionCalibrationPage::handleTransportError);
    connect(&m_timeout, &QTimer::timeout,
            this, &ProductionCalibrationPage::handleTimeout);
    connect(&m_verifyTransfer, &CalibrationTransfer::payloadReady,
            this, &ProductionCalibrationPage::receiveVerifyPayload);
    connect(&m_verifyTransfer, &CalibrationTransfer::completed,
            this, &ProductionCalibrationPage::verifyCompleted);
    connect(&m_verifyTransfer, &CalibrationTransfer::progress,
            this, [this](const QString &message) { log(message); });

    refreshPorts();
    updateControls();
}

QWidget *ProductionCalibrationPage::contextPanel() const noexcept
{
    return m_contextPanel;
}

void ProductionCalibrationPage::shutdown()
{
    if (m_shuttingDown) return;
    m_timeout.stop();
    m_captureGapActive = false;
    m_captureSilence.invalidate();
    QList<QPair<QString, CaptureData>> capturesToPreserve;
    if (m_operation == Operation::Capturing) {
        if (const CaptureData *existing = captureFor(m_captureKind);
            existing && !existing->frames.isEmpty()) {
            capturesToPreserve = failedCaptures();
        } else {
            storeCurrentCapture();
        }
    }
    if (!m_sessionPath.isEmpty() && !m_saveActive) {
        savePackageAsync(m_sessionPath, std::move(capturesToPreserve), {});
    }
    m_shuttingDown = true;
    m_captureRestoring = false;
    m_verifyTransfer.cancel();
    const QByteArray finalWrite = m_transport.isActive()
            && m_operation != Operation::Writing
        ? QByteArray(1, char(ExitToUvcCommand)) : QByteArray{};
    m_transport.shutdown(finalWrite);
    m_operation = Operation::Idle;
    m_ioTasks.closeAndWait();
    m_saveActive = false;
}

void ProductionCalibrationPage::refreshPorts()
{
    const QString current = m_port->currentText();
    const QList<SerialPortInfo> ports = SerialTransport::availablePorts();
    m_port->clear();
    for (const SerialPortInfo &port : ports) m_port->addItem(port.portName);
    const int preferred = preferredTn160PortIndex(ports);
    if (preferred >= 0) {
        m_port->setCurrentIndex(preferred);
    } else if (!current.trimmed().isEmpty()) {
        int index = m_port->findText(current, Qt::MatchFixedString);
        if (index < 0) {
            m_port->addItem(current.trimmed(), tr("手动端口"));
            index = m_port->count() - 1;
        }
        m_port->setCurrentIndex(index);
    }
    log(tr("扫描到 %1 个串口").arg(ports.size()));
}

void ProductionCalibrationPage::setPort(const QString &port)
{
    const QString normalized = port.trimmed();
    if (normalized.isEmpty()) return;
    int index = m_port->findText(normalized, Qt::MatchFixedString);
    if (index < 0) {
        m_port->addItem(normalized, tr("手动端口"));
        index = m_port->count() - 1;
    }
    m_port->setCurrentIndex(index);
}

bool ProductionCalibrationPage::operationActive() const noexcept
{
    return m_operation != Operation::Idle || m_verifyTransfer.isActive()
        || m_saveActive;
}

bool ProductionCalibrationPage::connectionActive() const noexcept
{
    return m_transport.isActive();
}

int ProductionCalibrationPage::baudRate() const noexcept
{
    return RawBaud;
}

void ProductionCalibrationPage::toggleConnection()
{
    if (operationActive()) return;
    if (m_transport.isActive()) {
        m_transport.closeAfterWrites(QByteArray(1, char(ExitToUvcCommand)));
        return;
    }
    if (m_port->currentText().isEmpty()) {
        failOperation(tr("请先选择 TN160 串口"));
        return;
    }
    m_transport.open({m_port->currentText(), RawBaud});
    setStatus(QStringLiteral("connecting"), tr("连接中"));
    updateControls();
}

void ProductionCalibrationPage::newSensorSession()
{
    if (operationActive()) return;
    m_low = {};
    m_high = {};
    m_highB = {};
    m_offset = {};
    m_calibration = {};
    m_currentCapture = {};
    m_frameFilter.reset();
    m_recentTelemetry.clear();
    m_postCaptureTelemetry.clear();
    m_monitorTrace.clear();
    m_stateAReference = {};
    m_lastTraceTimestampUs = 0;
    m_lastMonitorNucCount = -1;
    m_cumulativeNucDg = 0;
    m_monitorContinuityValid = true;
    m_stateBReady = false;
    m_cycleWarningIssued = false;
    m_sessionPath.clear();
    m_verifyPayloads.clear();
    m_sensorId->clear();
    m_session->setText(tr("新 Sensor：等待采集"));
    m_summary->setText(
        tr("正式 V1 量产流程 · 固定 0°C / 50°C · 每温点 32 个有效帧 · "
           "状态 B 按热状态就绪 · 120 s 仅为节拍目标，超出不判失败"));
    updatePreviews();
    log(tr("已清空上一颗 Sensor 的内存状态；磁盘记录未删除"));
    updateControls();
}

void ProductionCalibrationPage::captureLow()
{
    beginCapture(CaptureKind::Low);
}

void ProductionCalibrationPage::captureHigh()
{
    beginCapture(CaptureKind::High);
}

void ProductionCalibrationPage::captureHighB()
{
    if (!m_calibration.valid) {
        failOperation(tr("请先完成状态 A 两点 Gain"));
        return;
    }
    const TelemetryState current = m_recentTelemetry.isEmpty()
        ? TelemetryState{} : m_recentTelemetry.constLast();
    const auto gate = stateBGate(current);
    if (!gate.canCapture()) {
        const QString message = stateBGateMessage(gate);
        if (gate.decision == tn160common::StateBGateDecision::ContinuityLost) {
            failOperation(message);
        } else {
            setStatus(QStringLiteral("connected"),
                      tr("状态 B 未就绪 · 继续等待"));
            log(message, QStringLiteral("warning"));
        }
        return;
    }
    if (!gate.physicalReady()) {
        log(tr("状态 B 在 120 s 节拍目标后仍未达到物理门槛；"
               "允许继续第 4 步，但该数据只能标记为覆盖不足，"
               "不能作为热状态建模合格数据"),
            QStringLiteral("warning"));
    }
    beginCapture(CaptureKind::HighB);
}

void ProductionCalibrationPage::captureOffset()
{
    if (!m_calibration.valid) {
        failOperation(tr("请先计算两点 Gain 或导入 Sensor 标定包"));
        return;
    }
    const TelemetryState current = m_recentTelemetry.isEmpty()
        ? TelemetryState{} : m_recentTelemetry.constLast();
    const auto gate = stateBGate(current);
    if (m_stateAReference.valid && !gate.canCapture()) {
        const QString message = stateBGateMessage(gate);
        if (gate.decision == tn160common::StateBGateDecision::ContinuityLost) {
            failOperation(message);
        } else {
            setStatus(QStringLiteral("connected"),
                      tr("状态 B 未就绪 · 继续等待"));
            log(message, QStringLiteral("warning"));
        }
        return;
    }
    if (m_stateAReference.valid && !gate.physicalReady()) {
        log(tr("状态 B 在 120 s 节拍目标后仍未达到物理门槛；"
               "允许继续第 5 步，生产记录保留覆盖不足告警"),
            QStringLiteral("warning"));
    }
    beginCapture(CaptureKind::Offset);
}

void ProductionCalibrationPage::beginCapture(const CaptureKind kind)
{
    if (operationActive()) return;
    if (!m_transport.isActive()) {
        failOperation(tr("请先连接 TN160"));
        return;
    }
    if (m_boardId->text().trimmed().isEmpty()
        || m_sensorId->text().trimmed().isEmpty()) {
        failOperation(tr("请先填写 PCB 标签和 Sensor 标签"));
        return;
    }
    if (m_recentTelemetry.size() < SnapshotFrames) {
        setStatus(QStringLiteral("connected"), tr("等待监控遥测"));
        log(tr("监控遥测不足 %1 帧，请保持连接后再采集；不判失败")
                .arg(SnapshotFrames),
            QStringLiteral("warning"));
        return;
    }
    QVector<TelemetryState> beforeSamples;
    beforeSamples.reserve(SnapshotFrames);
    for (int index = m_recentTelemetry.size() - SnapshotFrames;
         index < m_recentTelemetry.size(); ++index) {
        beforeSamples.append(m_recentTelemetry.at(index));
    }
    const TelemetryState before = medianTelemetry(beforeSamples);
    if (!before.valid) {
        failOperation(tr("当前热状态遥测无效，不能建立采集前快照"));
        return;
    }
    QString error;
    if (!ensureSessionDirectory(&error)) {
        failOperation(error);
        return;
    }
    if (!m_transport.replaceEngine(std::make_unique<Tn160RawFrameEngine>(),
                                   &error)) {
        failOperation(tr("无法启动 RAW_U16 后台解析：%1").arg(error));
        return;
    }

    m_operation = Operation::Capturing;
    m_captureKind = kind;
    m_captureRestoring = false;
    m_captureSuccess = false;
    m_captureGapActive = false;
    m_captureMessage.clear();
    m_currentCapture = {};
    m_currentCapture.requiredFrames = requiredFramesFor(kind);
    m_currentCapture.before = before;
    m_frameFilter.reset();
    m_transport.write(QByteArray(1, char(RawCommand)));
    m_captureSilence.restart();
    m_timeout.start(FfcSoftSilenceMs);
    setStatus(QStringLiteral("raw-capturing"),
              tr("%1采集 0/%2")
                  .arg(captureName(kind)).arg(m_currentCapture.requiredFrames));
    log(tr("发送 0xAA，开始%1 %2 帧 RAW_U16 采集")
            .arg(captureName(kind)).arg(m_currentCapture.requiredFrames));
    updateControls();
}

void ProductionCalibrationPage::handleTransportState(const QString &state)
{
    if (state == QStringLiteral("connected")) {
        if (m_operation == Operation::Idle) {
            m_transport.write(QByteArray(1, char(MonitorCommand)));
            setStatus(QStringLiteral("connected"), tr("已连接 · 监控模式"));
            log(tr("发送 0x11，显式切入 UART 监控模式"));
        }
    } else if (state == QStringLiteral("disconnected")) {
        if (m_waitingCloseForVerify) {
            m_waitingCloseForVerify = false;
            m_operation = Operation::Verifying;
            m_verifyPayloads.clear();
            setStatus(QStringLiteral("connecting"), tr("等待重启并回读"));
            QTimer::singleShot(900, this, [this] {
                if (m_operation == Operation::Verifying) {
                    m_verifyTransfer.start(CalibrationTransfer::Mode::Dump,
                                           m_portForVerify, RawBaud, DumpBaud);
                }
            });
        } else if (m_operation == Operation::Capturing && !m_captureRestoring) {
            stopCapture(false, tr("采集期间串口断开"));
        } else if (m_operation == Operation::Writing) {
            failOperation(tr("写入期间串口断开"));
        } else if (m_operation == Operation::Idle) {
            if (m_stateAReference.valid && m_monitorContinuityValid) {
                m_monitorContinuityValid = false;
                m_stateBReady = false;
                log(tr("状态 A 建立后串口断开，无法证明期间未 RST；"
                       "本轮 A/B 连续性失效"),
                    QStringLiteral("error"));
            }
            setStatus(QStringLiteral("disconnected"), tr("未连接"));
        }
    } else if (state == QStringLiteral("connecting")) {
        setStatus(QStringLiteral("connecting"), tr("连接中"));
    } else if (state == QStringLiteral("disconnecting")) {
        setStatus(QStringLiteral("disconnecting"), tr("断开中"));
    }
    updateControls();
}

void ProductionCalibrationPage::handleDecodedPackets(const PacketList &packets)
{
    handleMonitorPackets(packets);
    if (m_operation == Operation::Capturing && !m_captureRestoring) {
        QByteArray frame;
        qint64 timestamp = 0;
        for (const Packet &packet : packets) {
            if (const auto *image = std::get_if<ImagePacket>(&packet);
                image && image->source == QString::fromLatin1(Tn160RawFrameSource)) {
                frame = image->pixels;
                timestamp = image->timestampUs;
                continue;
            }
            if (const auto *sample = std::get_if<SamplePacket>(&packet);
                sample && sample->source == QString::fromLatin1(Tn160RawFrameSource)
                && sample->timestampUs == timestamp && sample->values.size() >= 2
                && !frame.isEmpty()) {
                handleRawFrame(frame, sample->values[0],
                               quint16(sample->values[1]));
                frame.clear();
                if (m_operation != Operation::Capturing || m_captureRestoring) return;
            }
        }
    } else if (m_operation == Operation::Writing) {
        for (const Packet &packet : packets) {
            const auto *event = std::get_if<EventPacket>(&packet);
            if (!event || event->category
                    != QString::fromLatin1(Tn160WriteReplySource)) {
                continue;
            }
            if (event->fields.value(QStringLiteral("version")).toUInt() != 1u) {
                failOperation(tr("设备 0xEC 协议版本不兼容"));
                return;
            }
            handleWriteReply(
                quint8(event->fields.value(QStringLiteral("status")).toUInt()),
                quint8(event->fields.value(QStringLiteral("detail")).toUInt()),
                event->fields.value(QStringLiteral("payload_crc")).toUInt(),
                event->fields.value(QStringLiteral("header_crc")).toUInt(),
                event->fields.value(QStringLiteral("flash_header_crc")).toUInt());
            if (m_operation != Operation::Writing) return;
        }
    }
}

void ProductionCalibrationPage::handleMonitorPackets(const PacketList &packets)
{
    for (const Packet &packet : packets) {
        const auto *sample = std::get_if<SamplePacket>(&packet);
        if (!sample
            || sample->source != QString::fromLatin1(Tn160MonitorSource)) {
            continue;
        }
        handleMonitorSample(*sample);
    }
}

void ProductionCalibrationPage::handleMonitorSample(const SamplePacket &packet)
{
    if (packet.values.size() != MonitorSampleCount) return;
    const auto boundedInteger = [&packet](const MonitorSampleIndex index,
                                          const qint64 minimum,
                                          const qint64 maximum) {
        const double value = packet.values.at(index);
        if (!std::isfinite(value)) return minimum;
        return std::clamp<qint64>(std::llround(value), minimum, maximum);
    };

    TelemetryState state;
    state.timestampUs = packet.timestampUs;
    state.ntcC = packet.values.at(MonitorNtcC);
    state.ntcAdu = quint16(boundedInteger(MonitorNtcAdu, 0, 65535));
    state.vtempAdu = quint16(boundedInteger(MonitorVtemp, 0, 65535));
    state.smoothVtempAdu = quint16(
        boundedInteger(MonitorSmoothVtempAdu, 0, 65535));
    state.nucDecision = quint8(
        boundedInteger(MonitorNucDecision, 0, 255));
    state.nucCount = quint8(boundedInteger(MonitorNucCount, 0, 255));
    state.nucDg = qint16(boundedInteger(MonitorNucDg, -32768, 32767));
    state.nucDv = qint16(boundedInteger(MonitorNucDv, -32768, 32767));
    state.nucDn = qint16(boundedInteger(MonitorNucDn, -32768, 32767));
    state.bootNtcAdu = quint16(
        boundedInteger(MonitorBootNtcAdu, 0, 65535));
    state.anchor = qint32(
        boundedInteger(MonitorAnchor, std::numeric_limits<qint32>::min(),
                       std::numeric_limits<qint32>::max()));
    state.effectiveAnchor = qint32(
        boundedInteger(MonitorEffectiveAnchor,
                       std::numeric_limits<qint32>::min(),
                       std::numeric_limits<qint32>::max()));
    state.meanDiff = float(packet.values.at(MonitorMeanDiff));
    state.valid = state.timestampUs > 0 && state.ntcAdu > 0
        && std::isfinite(state.meanDiff) && state.meanDiff > 0.0f;
    if (!state.valid) return;

    bool nucChanged = false;
    if (m_lastMonitorNucCount >= 0) {
        const int previous = m_lastMonitorNucCount;
        const int current = state.nucCount;
        if (current == previous + 1) {
            nucChanged = true;
            if (state.nucDecision == 0) {
                m_cumulativeNucDg += state.nucDg;
            } else {
                if (m_monitorContinuityValid) {
                    log(tr("NUC count 已变化但 decision=%1，监控连续性失效")
                            .arg(state.nucDecision),
                        QStringLiteral("error"));
                }
                m_monitorContinuityValid = false;
            }
        } else if (current != previous) {
            nucChanged = true;
            if (m_monitorContinuityValid) {
                log(current < previous
                        ? tr("NUC count 从 %1 回退到 %2，判定设备发生 RST")
                              .arg(previous).arg(current)
                        : tr("NUC count 从 %1 跳到 %2，漏失至少一次 FFC")
                              .arg(previous).arg(current),
                    QStringLiteral("error"));
            }
            m_monitorContinuityValid = false;
        }
    }
    m_lastMonitorNucCount = state.nucCount;
    state.cumulativeNucDg = m_cumulativeNucDg;

    m_recentTelemetry.append(state);
    while (m_recentTelemetry.size() > 5) m_recentTelemetry.removeFirst();
    if (m_monitorTrace.isEmpty() || nucChanged
        || state.timestampUs - m_lastTraceTimestampUs
               >= MonitorTraceIntervalUs) {
        m_monitorTrace.append(state);
        m_lastTraceTimestampUs = state.timestampUs;
    }

    if (m_operation == Operation::Capturing && m_captureRestoring) {
        m_postCaptureTelemetry.append(state);
        if (m_postCaptureTelemetry.size() >= SnapshotFrames) {
            m_currentCapture.after = medianTelemetry(m_postCaptureTelemetry);
            finishCapture(m_captureSuccess, m_captureMessage);
        }
        return;
    }
    updateThermalStatus(state);
}

ProductionCalibrationPage::TelemetryState
ProductionCalibrationPage::medianTelemetry(
    const QVector<TelemetryState> &samples)
{
    QVector<TelemetryState> validSamples;
    validSamples.reserve(samples.size());
    for (const TelemetryState &sample : samples) {
        if (sample.valid) validSamples.append(sample);
    }
    if (validSamples.isEmpty()) return {};

    const auto median = [&validSamples](const auto getter) {
        QVector<double> values;
        values.reserve(validSamples.size());
        for (const TelemetryState &sample : validSamples) {
            const double value = double(getter(sample));
            if (std::isfinite(value)) values.append(value);
        }
        if (values.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
        std::sort(values.begin(), values.end());
        const int middle = values.size() / 2;
        return values.size() % 2 == 0
            ? (values.at(middle - 1) + values.at(middle)) / 2.0
            : values.at(middle);
    };

    TelemetryState result;
    result.timestampUs = qint64(median(
        [](const TelemetryState &value) { return value.timestampUs; }));
    result.ntcC = median(
        [](const TelemetryState &value) { return value.ntcC; });
    result.ntcAdu = quint16(std::llround(median(
        [](const TelemetryState &value) { return value.ntcAdu; })));
    result.vtempAdu = quint16(std::llround(median(
        [](const TelemetryState &value) { return value.vtempAdu; })));
    result.smoothVtempAdu = quint16(std::llround(median(
        [](const TelemetryState &value) { return value.smoothVtempAdu; })));
    result.nucDecision = quint8(std::llround(median(
        [](const TelemetryState &value) { return value.nucDecision; })));
    result.nucCount = quint8(std::llround(median(
        [](const TelemetryState &value) { return value.nucCount; })));
    result.nucDg = qint16(std::llround(median(
        [](const TelemetryState &value) { return value.nucDg; })));
    result.nucDv = qint16(std::llround(median(
        [](const TelemetryState &value) { return value.nucDv; })));
    result.nucDn = qint16(std::llround(median(
        [](const TelemetryState &value) { return value.nucDn; })));
    result.cumulativeNucDg = qint32(std::llround(median(
        [](const TelemetryState &value) { return value.cumulativeNucDg; })));
    result.bootNtcAdu = quint16(std::llround(median(
        [](const TelemetryState &value) { return value.bootNtcAdu; })));
    result.anchor = qint32(std::llround(median(
        [](const TelemetryState &value) { return value.anchor; })));
    result.effectiveAnchor = qint32(std::llround(median(
        [](const TelemetryState &value) { return value.effectiveAnchor; })));
    result.meanDiff = float(median(
        [](const TelemetryState &value) { return value.meanDiff; }));
    result.valid = result.timestampUs > 0 && result.ntcAdu > 0
        && std::isfinite(result.meanDiff) && result.meanDiff > 0.0f;
    return result;
}

int ProductionCalibrationPage::stateBFfcDelta(
    const TelemetryState &state) const
{
    if (!m_stateAReference.valid || !state.valid
        || state.nucCount < m_stateAReference.nucCount) {
        return -1;
    }
    return int(state.nucCount) - int(m_stateAReference.nucCount);
}

bool ProductionCalibrationPage::stateBReady(
    const TelemetryState &state) const
{
    return stateBGate(state).physicalReady();
}

tn160common::StateBGateResult ProductionCalibrationPage::stateBGate(
    const TelemetryState &state) const
{
    tn160common::StateBGateInput input;
    input.referenceValid = m_stateAReference.valid;
    input.telemetryValid = state.valid;
    input.continuityValid = m_monitorContinuityValid;
    input.coverageLatched = m_stateBReady;
    input.advisoryElapsed = m_cycleWarningIssued;
    input.referenceNtcAdu = m_stateAReference.ntcAdu;
    input.currentNtcAdu = state.ntcAdu;
    input.referenceNucCount = m_stateAReference.nucCount;
    input.currentNucCount = state.nucCount;
    input.minimumDeltaNtcAdu = MinimumStateBDeltaNtcAdu;
    input.minimumFfcDelta = MinimumStateBFfcDelta;
    return tn160common::evaluateStateBGate(input);
}

QString ProductionCalibrationPage::stateBGateMessage(
    const tn160common::StateBGateResult &result) const
{
    using tn160common::StateBGateDecision;
    switch (result.decision) {
    case StateBGateDecision::Ready:
        return tr("状态 B 已就绪");
    case StateBGateDecision::Waiting:
        return tr("状态 B 尚未就绪，请继续等待热状态变化");
    case StateBGateDecision::MissingReference:
        return tr("状态 A 尚未建立，请先完成第 1、2、3 步");
    case StateBGateDecision::MissingTelemetry:
        return tr("监控遥测尚未就绪，请保持连接后继续等待");
    case StateBGateDecision::ContinuityLost:
        return tr("检测到 NUC/RST 连续性失效，请从第 1 步重新开始");
    case StateBGateDecision::CoverageInsufficient:
        return tr("状态 B 尚未就绪：当前 |ΔNTC|=%1/%2 ADU，"
                  "有效 FFC 增量=%3/%4；继续等待，不判失败")
            .arg(result.deltaNtcAdu)
            .arg(MinimumStateBDeltaNtcAdu)
            .arg(result.deltaFfc)
            .arg(MinimumStateBFfcDelta);
    case StateBGateDecision::PhysicalReady:
    case StateBGateDecision::AdvisoryOverride:
        break;
    }
    return {};
}

void ProductionCalibrationPage::updateThermalStatus(
    const TelemetryState &state)
{
    if (!m_stateAReference.valid) return;
    const bool ready = stateBReady(state);
    bool refresh = ready != m_stateBReady;
    if (ready && !m_stateBReady) {
        log(tr("状态 B 已就绪：ΔNTC=%1 ADU，有效 FFC 增量=%2；之后长期保持可采")
                .arg(int(state.ntcAdu) - int(m_stateAReference.ntcAdu))
                .arg(stateBFfcDelta(state)));
        if (m_operation == Operation::Idle) {
            setStatus(QStringLiteral("connected"), tr("状态 B 已就绪"));
        }
    }
    m_stateBReady = ready;

    const qint64 elapsedUs = state.timestampUs - m_stateAReference.timestampUs;
    if (!m_cycleWarningIssued && !m_offset.complete
        && elapsedUs >= TargetCycleUs) {
        m_cycleWarningIssued = true;
        refresh = true;
        log(tr("产测节拍已超过 120 s；仅作节拍告警，继续等待或采集均不会判失败"),
            QStringLiteral("warning"));
        if (m_operation == Operation::Idle) {
            setStatus(QStringLiteral("connected"),
                      tr("节拍 >120 s · 仍可继续，不判失败"));
        }
    }
    if (refresh) {
        updateControls();
        updatePreviews();
    }
}

void ProductionCalibrationPage::handleRawFrame(const QByteArray &frame,
                                                const double mean,
                                                const quint16 vtemp)
{
    if (m_operation != Operation::Capturing || m_captureRestoring
        || frame.size() != RawFrameSize) return;
    if (m_captureGapActive) {
        const qint64 silenceMs = m_captureSilence.isValid()
            ? m_captureSilence.elapsed() : 0;
        const TelemetryState before = m_currentCapture.before;
        const int requiredFrames = m_currentCapture.requiredFrames;
        m_currentCapture = {};
        m_currentCapture.before = before;
        m_currentCapture.requiredFrames = requiredFrames;
        m_frameFilter.reset();
        m_captureGapActive = false;
        log(tr("%1原始流从疑似 FFC 空窗恢复（%2 ms）；"
               "已丢弃空窗前帧并从 0 重新采集")
                .arg(captureName(m_captureKind)).arg(silenceMs));
    }

    QVector<quint16> pixels;
    double decodedMean = 0.0;
    quint16 decodedVtemp = 0;
    if (!decodeRawFrame(frame, &pixels, &decodedMean, &decodedVtemp)) {
        log(tr("%1收到无法复核的完整 RAW 帧，已忽略")
                .arg(captureName(m_captureKind)),
            QStringLiteral("warning"));
        return;
    }
    if (std::abs(decodedMean - mean) > 0.01 || decodedVtemp != vtemp) {
        log(tr("%1 RAW 元数据复核不一致，使用帧内数据")
                .arg(captureName(m_captureKind)),
            QStringLiteral("warning"));
    }
    const auto result = m_frameFilter.evaluate(
        std::span<const std::uint16_t>(pixels.constData(),
                                       std::size_t(pixels.size())),
        decodedMean);
    const bool accepted =
        result.decision == Tn160ProductionFrameFilter::Decision::Accept;
    QString rejectionReason;
    switch (result.decision) {
    case Tn160ProductionFrameFilter::Decision::Accept:
        break;
    case Tn160ProductionFrameFilter::Decision::StartupDiscard:
        rejectionReason = QStringLiteral("startup");
        log(tr("%1启动保护：0xAA 后第一个完整 RAW 帧已预丢弃，不计入 32 帧")
                .arg(captureName(m_captureKind)));
        break;
    case Tn160ProductionFrameFilter::Decision::MeanSpanOutlier:
        rejectionReason = QStringLiteral("mean-span");
        log(tr("%1拒绝均值异常帧：加入后跨度 %2 ADU > %3 ADU")
                .arg(captureName(m_captureKind))
                .arg(result.meanSpanAdu, 0, 'f', 2)
                .arg(Tn160ProductionFrameFilter::MeanSpanLimitAdu, 0, 'f', 0),
            QStringLiteral("warning"));
        break;
    case Tn160ProductionFrameFilter::Decision::SpatialOutlier:
        rejectionReason = QStringLiteral("spatial");
        log(tr("%1拒绝空间异常帧：去均值 RMSE %2 ADU > 自适应上限 %3 ADU")
                .arg(captureName(m_captureKind))
                .arg(result.spatialRmseAdu, 0, 'f', 2)
                .arg(result.spatialLimitAdu, 0, 'f', 2),
            QStringLiteral("warning"));
        break;
    case Tn160ProductionFrameFilter::Decision::InvalidFrame:
        rejectionReason = QStringLiteral("invalid");
        log(tr("%1拒绝无效空间帧").arg(captureName(m_captureKind)),
            QStringLiteral("warning"));
        break;
    }
    const int frameIndex = m_currentCapture.frames.size();
    m_currentCapture.frames.append(frame);
    m_currentCapture.frameInfo.append(
        {decodedMean, decodedVtemp, accepted, rejectionReason});
    if (accepted) m_currentCapture.acceptedIndices.append(frameIndex);
    m_captureSilence.restart();
    m_timeout.start(FfcSoftSilenceMs);

    const int acceptedCount = m_currentCapture.acceptedIndices.size();
    const int rejectedCount = m_currentCapture.frames.size() - acceptedCount;
    const int requiredFrames = m_currentCapture.requiredFrames;
    if (acceptedCount == 1 || acceptedCount % 8 == 0
        || acceptedCount == requiredFrames || !accepted) {
        setStatus(QStringLiteral("raw-capturing"),
                  tr("%1 %2/%3 · 拒绝 %4")
                      .arg(captureName(m_captureKind))
                      .arg(acceptedCount)
                      .arg(requiredFrames)
                      .arg(rejectedCount));
    }
    if (acceptedCount >= requiredFrames) {
        stopCapture(true, tr("已取得 %1 个接受帧").arg(requiredFrames));
        return;
    }
    if (m_currentCapture.frames.size() >= MaximumFrames) {
        stopCapture(false,
                    tr("最多 %1 帧仍不足 %2 个稳定帧")
                        .arg(MaximumFrames).arg(requiredFrames));
    }
}

bool ProductionCalibrationPage::decodeRawFrame(const QByteArray &frame,
                                                QVector<quint16> *pixels,
                                                double *mean,
                                                quint16 *vtemp)
{
    if (frame.size() != RawFrameSize || frame.at(0) != char(0xff)
        || frame.at(1) != char(0xff) || pixels == nullptr
        || mean == nullptr || vtemp == nullptr) {
        return false;
    }
    const auto *raw = reinterpret_cast<const uchar *>(frame.constData());
    for (int offset = 2; offset + 1 < frame.size(); offset += 2) {
        if (qFromBigEndian<quint16>(raw + offset) > 0x3fffu) return false;
    }
    pixels->resize(PixelCount);
    quint64 sum = 0;
    for (int y = 0; y < FrameHeight; ++y) {
        const int rowOffset = 2 + y * RawWordsPerRow * 2;
        for (int x = 0; x < FrameWidth; ++x) {
            const quint16 value = qFromBigEndian<quint16>(raw + rowOffset + x * 2);
            (*pixels)[y * FrameWidth + x] = value;
            sum += value;
        }
    }
    *mean = double(sum) / PixelCount;
    *vtemp = qFromBigEndian<quint16>(raw + RawFrameSize - 2);
    return true;
}

void ProductionCalibrationPage::stopCapture(const bool success,
                                             const QString &message)
{
    if (m_operation != Operation::Capturing || m_captureRestoring) return;
    m_timeout.stop();
    m_captureGapActive = false;
    m_captureSilence.invalidate();
    m_captureRestoring = true;
    m_captureSuccess = success;
    m_captureMessage = message;
    m_transport.resetDecoder();
    if (m_transport.isActive()) {
        QString engineError;
        if (!m_transport.replaceEngine(std::make_unique<Tn160Engine>(),
                                       &engineError)) {
            finishCapture(false,
                          message + tr("；恢复监控解析失败：%1")
                                        .arg(engineError));
            return;
        }
        m_postCaptureTelemetry.clear();
        m_transport.write(QByteArray(1, char(MonitorCommand)));
        setStatus(QStringLiteral("raw-restoring"),
                  tr("恢复监控模式 · 等待 %1 帧遥测")
                      .arg(SnapshotFrames));
        m_timeout.start(RawHardSilenceMs);
    } else {
        finishCapture(false, message);
    }
}

void ProductionCalibrationPage::finishCapture(const bool success,
                                               const QString &message)
{
    if (m_operation != Operation::Capturing) return;
    m_timeout.stop();
    m_captureRestoring = false;
    m_captureGapActive = false;
    m_captureSilence.invalidate();
    bool finalSuccess = success;
    QString finalMessage = message;
    if (finalSuccess) {
        QString error;
        if (!calculateMeanImage(&m_currentCapture, &error)) {
            finalSuccess = false;
            finalMessage = error;
        }
    }
    m_currentCapture.complete = finalSuccess;
    if (finalSuccess && !m_stateAReference.valid
        && (m_captureKind == CaptureKind::Low
            || m_captureKind == CaptureKind::High)) {
        m_stateAReference = m_currentCapture.before;
        m_stateBReady = false;
        m_cycleWarningIssued = false;
        log(tr("已锁定状态 A：NTC=%1 ADU，NUC count=%2；"
               "状态 B 由相对热状态判定，不由固定等待时间判定")
                .arg(m_stateAReference.ntcAdu)
                .arg(m_stateAReference.nucCount));
    }
    CaptureData *existing = captureFor(m_captureKind);
    const bool keepExisting = !finalSuccess && existing && !existing->frames.isEmpty();
    QList<QPair<QString, CaptureData>> capturesToPreserve;
    if (keepExisting) {
        capturesToPreserve = failedCaptures();
    } else {
        storeCurrentCapture();
    }

    if (finalSuccess && m_captureKind == CaptureKind::Offset) {
        QString error;
        if (!applySinglePointOffset(&error)) {
            finalSuccess = false;
            finalMessage = error;
        } else {
            finalMessage += tr("；已替换最终 V1 背景 offset，保留 Sensor Gain");
        }
    }

    m_operation = Operation::Idle;
    m_captureKind = CaptureKind::None;
    m_currentCapture = {};
    setStatus(QStringLiteral("connecting"), tr("保存生产记录"));
    updatePreviews();
    savePackageAsync(
        m_sessionPath, std::move(capturesToPreserve),
        [this, finalSuccess, finalMessage](const bool saved,
                                          const QString &saveError) mutable {
            if (!saved) {
                finalSuccess = false;
                finalMessage += tr("；保存失败：%1").arg(saveError);
            }
            log((finalSuccess ? tr("完成：") : tr("失败：")) + finalMessage,
                finalSuccess ? QStringLiteral("info") : QStringLiteral("error"));
            setStatus(finalSuccess ? QStringLiteral("connected")
                                   : QStringLiteral("error"),
                      finalSuccess ? tr("采集完成") : tr("采集失败"));
            updatePreviews();
        });
}

void ProductionCalibrationPage::storeCurrentCapture()
{
    CaptureData *target = captureFor(m_captureKind);
    if (target) *target = m_currentCapture;
}

ProductionCalibrationPage::CaptureData *
ProductionCalibrationPage::captureFor(const CaptureKind kind)
{
    if (kind == CaptureKind::Low) return &m_low;
    if (kind == CaptureKind::High) return &m_high;
    if (kind == CaptureKind::HighB) return &m_highB;
    if (kind == CaptureKind::Offset) return &m_offset;
    return nullptr;
}

const ProductionCalibrationPage::CaptureData *
ProductionCalibrationPage::captureFor(const CaptureKind kind) const
{
    if (kind == CaptureKind::Low) return &m_low;
    if (kind == CaptureKind::High) return &m_high;
    if (kind == CaptureKind::HighB) return &m_highB;
    if (kind == CaptureKind::Offset) return &m_offset;
    return nullptr;
}

bool ProductionCalibrationPage::calculateMeanImage(CaptureData *capture,
                                                   QString *error)
{
    if (capture == nullptr || capture->requiredFrames <= 0
        || capture->acceptedIndices.size() < capture->requiredFrames) {
        if (error) *error = tr("稳定帧数量不足");
        return false;
    }
    QVector<quint64> sums(PixelCount, 0);
    QVector<quint16> pixels;
    for (int accepted = 0; accepted < capture->requiredFrames; ++accepted) {
        const int frameIndex = capture->acceptedIndices.at(accepted);
        double ignoredMean = 0.0;
        quint16 ignoredVtemp = 0;
        if (frameIndex < 0 || frameIndex >= capture->frames.size()
            || !decodeRawFrame(capture->frames.at(frameIndex), &pixels,
                               &ignoredMean, &ignoredVtemp)) {
            if (error) *error = tr("接受帧 %1 解码失败").arg(accepted + 1);
            return false;
        }
        for (int pixel = 0; pixel < PixelCount; ++pixel) sums[pixel] += pixels[pixel];
    }
    capture->meanPixels.resize(PixelCount);
    for (int pixel = 0; pixel < PixelCount; ++pixel) {
        capture->meanPixels[pixel] = quint16(
            (sums[pixel] + capture->requiredFrames / 2)
            / capture->requiredFrames);
    }
    return true;
}

void ProductionCalibrationPage::computeTwoPoint()
{
    if (operationActive()) return;
    QString error;
    if (!calculateCalibration(&error)) {
        failOperation(error);
        return;
    }
    if (!ensureSessionDirectory(&error)) {
        failOperation(tr("标定已计算，但创建保存目录失败：%1").arg(error));
        return;
    }
    const float meanDiff = m_calibration.meanDiff;
    const int badPointCount = m_calibration.badPointCount;
    setStatus(QStringLiteral("connecting"), tr("保存两点标定"));
    updatePreviews();
    savePackageAsync(m_sessionPath, {},
                     [this, meanDiff, badPointCount](const bool saved,
                                                     const QString &saveError) {
        if (!saved) {
            setStatus(QStringLiteral("error"), tr("标定保存失败"));
            log(tr("标定已计算，但保存失败：%1").arg(saveError),
                QStringLiteral("error"));
            return;
        }
        log(tr("两点计算完成：mean_diff=%1 ADU，坏点=%2")
                .arg(meanDiff, 0, 'f', 3).arg(badPointCount));
        setStatus(QStringLiteral("connected"), tr("两点 Gain 已计算"));
    });
}

bool ProductionCalibrationPage::calculateCalibration(QString *error)
{
    if (!m_low.complete || !m_high.complete
        || m_low.meanPixels.size() != PixelCount
        || m_high.meanPixels.size() != PixelCount) {
        if (error) *error = tr("请先完成低温和高温各 32 个稳定帧");
        return false;
    }

    QVector<float> gain(PixelCount, -1.0f);
    double diffSum = 0.0;
    int diffCount = 0;
    for (int pixel = 0; pixel < PixelCount; ++pixel) {
        const quint16 high = m_high.meanPixels[pixel];
        const quint16 low = m_low.meanPixels[pixel];
        if (high > 15000u || low > 15000u) continue;
        int diff = int(high) - int(low);
        if (diff <= 0) diff = 1;
        gain[pixel] = float(diff);
        diffSum += diff;
        ++diffCount;
    }
    if (diffCount == 0) {
        if (error) *error = tr("没有可用于计算 Gain 的有效像元");
        return false;
    }
    const float meanDiff = float(diffSum / diffCount);
    QVector<QPoint> badPoints;
    const auto addBadPoint = [&badPoints, error](const int pixel) {
        const QPoint point(pixel % FrameWidth, pixel / FrameWidth);
        if (!badPoints.contains(point)) badPoints.append(point);
        if (badPoints.size() > BadPointBytes / 2) {
            if (error) {
                *error = QObject::tr("坏点超过 V1 上限 5：第 6 个为 (%1,%2)")
                             .arg(point.x()).arg(point.y());
            }
            return false;
        }
        return true;
    };

    for (int pixel = 0; pixel < PixelCount; ++pixel) {
        const float diff = gain[pixel];
        if (diff <= 0.0f) {
            gain[pixel] = 1.0f;
            if (!addBadPoint(pixel)) return false;
            continue;
        }
        const float value = meanDiff / diff;
        if (!std::isfinite(value) || value < 0.2f || value > 4.0f) {
            gain[pixel] = 1.0f;
            if (!addBadPoint(pixel)) return false;
            continue;
        }
        gain[pixel] = value;
    }

    constexpr float MaximumSpatialGainDifference = 0.4f;
    for (int y = 0; y < FrameHeight; ++y) {
        for (int x = 0; x < FrameWidth; ++x) {
            const int pixel = y * FrameWidth + x;
            const float value = gain[pixel];
            if (value == 1.0f) continue;
            double neighborSum = 0.0;
            int neighborCount = 0;
            for (int ny = std::max(0, y - 1);
                 ny <= std::min(FrameHeight - 1, y + 1); ++ny) {
                for (int nx = std::max(0, x - 1);
                     nx <= std::min(FrameWidth - 1, x + 1); ++nx) {
                    if (nx == x && ny == y) continue;
                    const float neighbor = gain[ny * FrameWidth + nx];
                    if (neighbor > 0.0f && neighbor != 1.0f) {
                        neighborSum += neighbor;
                        ++neighborCount;
                    }
                }
            }
            if (neighborCount > 0
                && std::abs(value - float(neighborSum / neighborCount))
                       > MaximumSpatialGainDifference) {
                gain[pixel] = 1.0f;
                if (!addBadPoint(pixel)) return false;
            }
        }
    }

    for (int i = 0; i < badPoints.size(); ++i) {
        for (int j = i + 1; j < badPoints.size(); ++j) {
            if (std::abs(badPoints[i].x() - badPoints[j].x()) <= 1
                && std::abs(badPoints[i].y() - badPoints[j].y()) <= 1) {
                if (error) {
                    *error = tr("相邻坏点不允许出货：(%1,%2) 与 (%3,%4)")
                                 .arg(badPoints[i].x()).arg(badPoints[i].y())
                                 .arg(badPoints[j].x()).arg(badPoints[j].y());
                }
                return false;
            }
        }
    }

    QByteArray gainBytes;
    gainBytes.reserve(GainBytes);
    for (const float value : gain) appendLeFloat(gainBytes, value);
    QByteArray badPointBytes(BadPointBytes, char(0xff));
    for (int index = 0; index < badPoints.size(); ++index) {
        badPointBytes[index * 2] = char(badPoints[index].y());
        badPointBytes[index * 2 + 1] = char(badPoints[index].x());
    }

    m_calibration = {};
    m_calibration.background = meanImageBytes(m_low.meanPixels);
    m_calibration.gain = gainBytes;
    m_calibration.badPoints = badPointBytes;
    m_calibration.meanDiff = meanDiff;
    m_calibration.tRefLow = ReferenceLowC;
    m_calibration.badPointCount = badPoints.size();
    m_calibration.valid = true;
    prepareWritePacket();
    return true;
}

bool ProductionCalibrationPage::applySinglePointOffset(QString *error)
{
    if (!m_calibration.valid || !m_offset.complete
        || m_offset.meanPixels.size() != PixelCount) {
        if (error) *error = tr("单点 offset 输入不完整");
        return false;
    }
    m_calibration.background = meanImageBytes(m_offset.meanPixels);
    m_calibration.singlePointOffsetApplied = true;
    m_calibration.writeVerified = false;
    prepareWritePacket();
    return true;
}

void ProductionCalibrationPage::prepareWritePacket()
{
    if (!m_calibration.valid
        || m_calibration.background.size() != ImageBytes
        || m_calibration.gain.size() != GainBytes
        || m_calibration.badPoints.size() != BadPointBytes) {
        m_writeHeader.clear();
        m_calibration.packet.clear();
        return;
    }
    const quint32 imageCrc = crc32(m_calibration.background);
    const quint32 gainCrc = crc32(m_calibration.gain);
    const quint32 badPointCrc = crc32(m_calibration.badPoints);
    QByteArray payload = m_calibration.background + m_calibration.gain
                         + m_calibration.badPoints;
    m_writePayloadCrc = crc32(payload);

    QByteArray header;
    header.reserve(WriteHeaderBytes);
    header.append("TNC1", 4);
    appendLe16(header, 1);
    appendLe16(header, WriteHeaderBytes);
    appendLe16(header, FrameWidth);
    appendLe16(header, FrameHeight);
    appendLe16(header, BadPointBytes / 2);
    appendLe16(header, 0);
    appendLe32(header, ImageBytes);
    appendLe32(header, GainBytes);
    appendLe32(header, BadPointBytes);
    appendLeFloat(header, m_calibration.meanDiff);
    appendLeFloat(header, m_calibration.tRefLow);
    appendLe32(header, imageCrc);
    appendLe32(header, gainCrc);
    appendLe32(header, badPointCrc);
    appendLe32(header, m_writePayloadCrc);
    header.append(QByteArray(8, char(0)));
    if (header.size() != WriteHeaderBytes - 4) {
        m_writeHeader.clear();
        m_calibration.packet.clear();
        return;
    }
    m_writeHeaderCrc = crc32(header);
    appendLe32(header, m_writeHeaderCrc);
    m_writeHeader = header;
    m_calibration.packet = header + payload;

    QByteArray flashHeader;
    flashHeader.reserve(64);
    flashHeader.append(char(0x78));
    flashHeader.append(QByteArray(3, char(0)));
    appendLe32(flashHeader, 0x002000u);
    appendLe32(flashHeader, ImageBytes);
    appendLe32(flashHeader, 0x00e000u);
    appendLe32(flashHeader, BadPointBytes);
    appendLe32(flashHeader, BadPointBytes / 2);
    appendLe32(flashHeader, 0x01a000u);
    appendLe32(flashHeader, GainBytes);
    appendLeFloat(flashHeader, m_calibration.meanDiff);
    appendLeFloat(flashHeader, m_calibration.tRefLow);
    flashHeader.append(QByteArray(24, char(0)));
    m_expectedFlashHeaderCrc = flashHeader.size() == 64
        ? crc32(flashHeader) : 0;
}

void ProductionCalibrationPage::writeAndVerify()
{
    if (operationActive()) return;
    if (!m_transport.isActive()) {
        failOperation(tr("请先连接 TN160"));
        return;
    }
    if (!m_calibration.valid) {
        failOperation(tr("没有可写入的标定数据"));
        return;
    }
    if (!m_calibration.singlePointOffsetApplied) {
        failOperation(tr("请先在最终状态完成 B2 0°C Offset；"
                         "B1 50°C 为可选诊断，但 B2 不能跳过"));
        return;
    }
    prepareWritePacket();
    if (m_writeHeader.size() != WriteHeaderBytes
        || m_calibration.packet.size()
               != WriteHeaderBytes + ImageBytes + GainBytes + BadPointBytes) {
        failOperation(tr("内部 V1 标定包长度异常"));
        return;
    }
    QString error;
    if (!ensureSessionDirectory(&error)) {
        failOperation(tr("写入前创建生产记录失败：%1").arg(error));
        return;
    }
    setStatus(QStringLiteral("connecting"), tr("写入前保存生产记录"));
    savePackageAsync(m_sessionPath, {},
                     [this](const bool saved, const QString &saveError) {
        if (!saved) {
            setStatus(QStringLiteral("error"), tr("写入前保存失败"));
            log(tr("写入前保存生产记录失败：%1").arg(saveError),
                QStringLiteral("error"));
            return;
        }
        beginWriteAfterPersist();
    });
}

void ProductionCalibrationPage::beginWriteAfterPersist()
{
    if (m_shuttingDown) return;
    if (!m_transport.isActive()) {
        failOperation(tr("保存期间串口已断开"));
        return;
    }
    QString error;
    if (!m_transport.replaceEngine(std::make_unique<Tn160WriteReplyEngine>(),
                                   &error)) {
        failOperation(tr("无法启动 0xEC 后台回复解析：%1").arg(error));
        return;
    }

    m_calibration.writeVerified = false;
    m_operation = Operation::Writing;
    m_writeStage = WriteStage::AwaitHeaderReady;
    m_transport.write(QByteArray(1, char(WriteCommand)));
    m_timeout.start(5000);
    setStatus(QStringLiteral("connecting"), tr("等待 0xEC 握手"));
    log(tr("发送 0xEC；设备将在各分区间握手，V1 header 最后提交"));
    updateControls();
}

void ProductionCalibrationPage::handleWriteReply(const quint8 status,
                                                  const quint8 detail,
                                                  const quint32 payloadCrc,
                                                  const quint32 headerCrc,
                                                  const quint32 flashHeaderCrc)
{
    if (status >= 0x80u) {
        failOperation(tr("设备拒绝 V1 写入：status=0x%1，阶段=%2；采集文件已保留")
                          .arg(status, 2, 16, QLatin1Char('0')).arg(detail));
        return;
    }
    if (status == WriteReadyHeader
        && m_writeStage == WriteStage::AwaitHeaderReady) {
        m_transport.write(m_writeHeader);
        m_writeStage = WriteStage::AwaitImageReady;
        m_timeout.start(10000);
        setStatus(QStringLiteral("connecting"), tr("发送 V1 header"));
        return;
    }
    if (status == WriteReadyImage
        && m_writeStage == WriteStage::AwaitImageReady) {
        m_transport.write(m_calibration.background);
        m_writeStage = WriteStage::AwaitGainReady;
        m_timeout.start(15000);
        setStatus(QStringLiteral("connecting"), tr("写入背景 offset"));
        return;
    }
    if (status == WriteReadyGain
        && m_writeStage == WriteStage::AwaitGainReady) {
        m_transport.write(m_calibration.gain);
        m_writeStage = WriteStage::AwaitBadPointsReady;
        m_timeout.start(15000);
        setStatus(QStringLiteral("connecting"), tr("写入 Sensor Gain"));
        return;
    }
    if (status == WriteReadyBadPoints
        && m_writeStage == WriteStage::AwaitBadPointsReady) {
        m_transport.write(m_calibration.badPoints);
        m_writeStage = WriteStage::AwaitFinal;
        m_timeout.start(10000);
        setStatus(QStringLiteral("connecting"), tr("提交 V1 header"));
        return;
    }
    if (status == WriteOk && m_writeStage == WriteStage::AwaitFinal) {
        if (payloadCrc != m_writePayloadCrc || headerCrc != m_writeHeaderCrc
            || flashHeaderCrc != m_expectedFlashHeaderCrc) {
            failOperation(tr("设备写入回复 CRC 不一致"));
            return;
        }
        m_timeout.stop();
        log(tr("设备内部 Flash CRC/回读通过；等待重启后执行旧 0xDD 全量回读"));
        m_portForVerify = m_port->currentText();
        m_waitingCloseForVerify = true;
        m_writeStage = WriteStage::Idle;
        m_transport.close();
        return;
    }
    failOperation(tr("设备 0xEC 回复顺序异常：status=0x%1，阶段=%2")
                      .arg(status, 2, 16, QLatin1Char('0')).arg(detail));
}

void ProductionCalibrationPage::receiveVerifyPayload(const int index,
                                                      const QByteArray &payload)
{
    while (m_verifyPayloads.size() <= index) m_verifyPayloads.append(QByteArray{});
    m_verifyPayloads[index] = payload;
}

void ProductionCalibrationPage::verifyCompleted(const bool success,
                                                 const QString &message)
{
    if (m_operation != Operation::Verifying) return;
    bool verified = success && m_verifyPayloads.size() == 3;
    QString result = message;
    if (verified) {
        verified = m_verifyPayloads[0] == m_calibration.background
                   && m_verifyPayloads[1] == m_calibration.gain
                   && m_verifyPayloads[2] == m_calibration.badPoints;
        if (!verified) result = tr("0xDD 回读内容与写入包不一致");
    }
    m_calibration.writeVerified = verified;
    m_operation = Operation::Idle;
    m_writeStage = WriteStage::Idle;
    setStatus(QStringLiteral("connecting"), tr("保存回读验证记录"));
    updatePreviews();
    savePackageAsync(
        m_sessionPath, {},
        [this, verified, result](const bool saved,
                                 const QString &saveError) mutable {
            if (!saved) {
                verified = false;
                result += tr("；保存验证记录失败：%1").arg(saveError);
            }
            setStatus(verified ? QStringLiteral("connected")
                               : QStringLiteral("error"),
                      verified ? tr("PASS · 写入回读一致")
                               : tr("FAIL · 回读不一致"));
            log(verified
                    ? tr("PASS：0xEC 内部校验 + 0xDD 三段 payload 逐字节一致")
                    : tr("FAIL：%1；原始帧和标定包均已保留").arg(result),
                verified ? QStringLiteral("info") : QStringLiteral("error"));
            updatePreviews();
        });
}

void ProductionCalibrationPage::handleTransportError(const QString &message)
{
    if (m_waitingCloseForVerify) return;
    if (m_operation == Operation::Capturing && !m_captureRestoring) {
        stopCapture(false, message);
        return;
    }
    failOperation(message);
}

void ProductionCalibrationPage::handleTimeout()
{
    if (m_operation == Operation::Capturing) {
        if (m_captureRestoring) {
            finishCapture(false, tr("恢复监控模式超时：%1").arg(m_captureMessage));
        } else {
            const qint64 silenceMs = m_captureSilence.isValid()
                ? m_captureSilence.elapsed() : RawHardSilenceMs;
            if (silenceMs < FfcSoftSilenceMs) {
                m_timeout.start(int(FfcSoftSilenceMs - silenceMs));
                return;
            }
            if (!m_captureGapActive) {
                m_captureGapActive = true;
                m_transport.resetDecoder();
                setStatus(QStringLiteral("raw-capturing"),
                          tr("%1 · 疑似 FFC，等待恢复后重新计数")
                              .arg(captureName(m_captureKind)));
                log(tr("%1原始流暂停，串口保持连接；按 FFC 空窗等待恢复")
                        .arg(captureName(m_captureKind)),
                    QStringLiteral("warning"));
            }
            if (silenceMs < RawHardSilenceMs) {
                m_timeout.start(int(RawHardSilenceMs - silenceMs));
                return;
            }
            stopCapture(false, tr("连续 10 秒未收到完整 RAW 帧"));
        }
        return;
    }
    if (m_operation == Operation::Writing) {
        failOperation(tr("等待 0xEC 写入握手超时；标定包已保留"));
    }
}

void ProductionCalibrationPage::failOperation(const QString &message)
{
    if (m_operation == Operation::Capturing && !m_captureRestoring) {
        stopCapture(false, message);
        return;
    }
    m_timeout.stop();
    m_captureGapActive = false;
    m_captureSilence.invalidate();
    if (m_operation == Operation::Verifying) m_verifyTransfer.cancel();
    m_operation = Operation::Idle;
    m_writeStage = WriteStage::Idle;
    m_waitingCloseForVerify = false;
    setStatus(QStringLiteral("error"), tr("失败"));
    log(message, QStringLiteral("error"));
    if (m_host) m_host->setStatus(tr("TN160 · 生产标定失败：%1").arg(message));
    updateControls();
    if (!m_sessionPath.isEmpty() && !m_saveActive && !m_shuttingDown) {
        savePackageAsync(m_sessionPath, {},
                         [this](const bool saved, const QString &saveError) {
            if (!saved) {
                log(tr("失败状态保存失败：%1").arg(saveError),
                    QStringLiteral("error"));
            }
        });
    }
}

void ProductionCalibrationPage::importCalibration()
{
    if (operationActive()) return;
    const QString fileName = QFileDialog::getOpenFileName(
        this, tr("导入 Sensor 标定包"), QString(),
        tr("TN160 V1 标定包 (calibration_v1.bin);;二进制文件 (*.bin)"));
    if (fileName.isEmpty()) return;
    QString error;
    if (!loadPackage(fileName, &error)) {
        failOperation(error);
        return;
    }
    if (m_sensorId->text().trimmed().isEmpty()) {
        m_sensorId->setText(QFileInfo(fileName).dir().dirName());
    }
    m_sessionPath.clear();
    if (!ensureSessionDirectory(&error)) {
        failOperation(tr("导入成功，但创建最终装配记录失败：%1").arg(error));
        return;
    }
    setStatus(QStringLiteral("connecting"), tr("保存导入记录"));
    updatePreviews();
    savePackageAsync(m_sessionPath, {},
                     [this](const bool saved, const QString &saveError) {
        if (!saved) {
            setStatus(QStringLiteral("error"), tr("导入记录保存失败"));
            log(tr("导入成功，但保存最终装配记录失败：%1").arg(saveError),
                QStringLiteral("error"));
            return;
        }
        log(tr("已导入逐 Sensor Gain；下一步在最终 PCB/镜头状态下采集 0°C 单点 Offset"));
        setStatus(QStringLiteral("connected"), tr("Sensor 包已导入"));
    });
}

void ProductionCalibrationPage::exportCalibration()
{
    if (operationActive() || !m_calibration.valid) return;
    if (m_boardId->text().trimmed().isEmpty()
        || m_sensorId->text().trimmed().isEmpty()) {
        failOperation(tr("请先填写 PCB 标签和 Sensor 标签"));
        return;
    }
    const QString parent = QFileDialog::getExistingDirectory(
        this, tr("选择导出目录"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (parent.isEmpty()) return;
    const QString folder = QStringLiteral("TN160_%1_%2_%3")
                               .arg(safeLabel(m_boardId->text()),
                                    safeLabel(m_sensorId->text()),
                                    QDateTime::currentDateTime().toString(
                                        QStringLiteral("yyyyMMdd_HHmmss")));
    const QString destination = QDir(parent).filePath(folder);
    setStatus(QStringLiteral("connecting"), tr("导出生产标定包"));
    savePackageAsync(destination, {},
                     [this, destination](const bool saved,
                                         const QString &saveError) {
        if (!saved) {
            setStatus(QStringLiteral("error"), tr("导出失败"));
            log(tr("导出失败：%1").arg(saveError), QStringLiteral("error"));
            return;
        }
        log(tr("完整生产标定包已导出：%1").arg(destination));
        m_session->setText(tr("自动记录：%1\n导出副本：%2")
                               .arg(m_sessionPath, destination));
        setStatus(QStringLiteral("connected"), tr("导出完成"));
    });
}

bool ProductionCalibrationPage::loadPackage(const QString &fileName,
                                             QString *error)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("无法打开标定包：%1").arg(file.errorString());
        return false;
    }
    const QByteArray packet = file.readAll();
    const int expectedSize = WriteHeaderBytes + ImageBytes + GainBytes + BadPointBytes;
    if (packet.size() != expectedSize || packet.first(4) != QByteArrayLiteral("TNC1")) {
        if (error) *error = tr("标定包 magic 或长度错误");
        return false;
    }
    const QByteArray header = packet.first(WriteHeaderBytes);
    if (readLe16(header, 4) != 1 || readLe16(header, 6) != WriteHeaderBytes
        || readLe16(header, 8) != FrameWidth || readLe16(header, 10) != FrameHeight
        || readLe16(header, 12) != BadPointBytes / 2 || readLe16(header, 14) != 0
        || readLe32(header, 16) != ImageBytes || readLe32(header, 20) != GainBytes
        || readLe32(header, 24) != BadPointBytes
        || crc32(header.first(WriteHeaderBytes - 4)) != readLe32(header, 60)) {
        if (error) *error = tr("标定包 header 字段或 CRC 错误");
        return false;
    }
    for (int index = 52; index < 60; ++index) {
        if (header[index] != char(0)) {
            if (error) *error = tr("标定包 reserved 字段非零");
            return false;
        }
    }
    const float meanDiff = readLeFloat(header, 28);
    const float tRefLow = readLeFloat(header, 32);
    if (!std::isfinite(meanDiff) || meanDiff <= 0.0f || meanDiff > 16383.0f
        || !std::isfinite(tRefLow) || tRefLow <= -273.15f
        || tRefLow >= ReferenceHighC) {
        if (error) *error = tr("标定包温度或 mean_diff 语义错误");
        return false;
    }
    const QByteArray background = packet.mid(WriteHeaderBytes, ImageBytes);
    const QByteArray gain = packet.mid(WriteHeaderBytes + ImageBytes, GainBytes);
    const QByteArray badPoints = packet.right(BadPointBytes);
    const QByteArray payload = packet.mid(WriteHeaderBytes);
    if (crc32(background) != readLe32(header, 36)
        || crc32(gain) != readLe32(header, 40)
        || crc32(badPoints) != readLe32(header, 44)
        || crc32(payload) != readLe32(header, 48)) {
        if (error) *error = tr("标定包 payload CRC 错误");
        return false;
    }
    for (int pixel = 0; pixel < PixelCount; ++pixel) {
        if (readLe16(background, pixel * 2) > 0x3fffu) {
            if (error) *error = tr("标定包背景像元越界");
            return false;
        }
        const float value = readLeFloat(gain, pixel * 4);
        if (!std::isfinite(value) || value < 0.2f || value > 4.0f) {
            if (error) *error = tr("标定包 Gain 越界");
            return false;
        }
    }
    QVector<QPoint> points;
    bool sawEmpty = false;
    for (int index = 0; index < BadPointBytes; index += 2) {
        const int y = quint8(badPoints[index]);
        const int x = quint8(badPoints[index + 1]);
        const bool empty = x == 0xff && y == 0xff;
        if (empty) {
            sawEmpty = true;
            continue;
        }
        if (sawEmpty || x >= FrameWidth || y >= FrameHeight) {
            if (error) *error = tr("标定包坏点表语义错误");
            return false;
        }
        const QPoint point(x, y);
        for (const QPoint &previous : points) {
            if (previous == point
                || (std::abs(previous.x() - x) <= 1
                    && std::abs(previous.y() - y) <= 1)) {
                if (error) *error = tr("标定包包含重复或相邻坏点");
                return false;
            }
        }
        points.append(point);
    }

    m_calibration = {};
    m_calibration.background = background;
    m_calibration.gain = gain;
    m_calibration.badPoints = badPoints;
    m_calibration.packet = packet;
    m_calibration.meanDiff = meanDiff;
    m_calibration.tRefLow = tRefLow;
    m_calibration.badPointCount = points.size();
    m_calibration.valid = true;
    prepareWritePacket();
    return true;
}

bool ProductionCalibrationPage::ensureSessionDirectory(QString *error)
{
    const QString boardId = m_boardId->text().trimmed();
    const QString sensorId = m_sensorId->text().trimmed();
    if (boardId.isEmpty() || sensorId.isEmpty()) {
        if (error) *error = tr("请先填写 PCB 标签和 Sensor 标签");
        return false;
    }
    if (!m_sessionPath.isEmpty()) return true;
    const QString rootPath = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (rootPath.isEmpty()) {
        if (error) *error = tr("系统没有可用的应用数据目录");
        return false;
    }
    const QString relative = QStringLiteral("production_calibration/%1_%2_%3")
                                 .arg(QDateTime::currentDateTime().toString(
                                          QStringLiteral("yyyyMMdd_HHmmss_zzz")),
                                      safeLabel(m_boardId->text()),
                                      safeLabel(m_sensorId->text()));
    QDir root(rootPath);
    if (!root.mkpath(relative)) {
        if (error) *error = tr("无法创建生产标定目录");
        return false;
    }
    m_sessionPath = root.filePath(relative);
    m_session->setText(tr("自动记录：%1").arg(m_sessionPath));
    return true;
}

ProductionCalibrationPage::ArchiveSnapshot
ProductionCalibrationPage::archiveSnapshot() const
{
    ArchiveSnapshot snapshot;
    snapshot.boardId = m_boardId->text().trimmed();
    snapshot.sensorId = m_sensorId->text().trimmed();
    snapshot.low = m_low;
    snapshot.high = m_high;
    snapshot.highB = m_highB;
    snapshot.offset = m_offset;
    snapshot.monitorTrace = m_monitorTrace;
    snapshot.monitorContinuityValid = m_monitorContinuityValid;
    snapshot.stateAReference = m_stateAReference;
    snapshot.cycleWarningIssued = m_cycleWarningIssued;
    snapshot.calibration = m_calibration;
    snapshot.writePayloadCrc = m_writePayloadCrc;
    snapshot.writeHeaderCrc = m_writeHeaderCrc;
    snapshot.expectedFlashHeaderCrc = m_expectedFlashHeaderCrc;
    return snapshot;
}

QList<QPair<QString, ProductionCalibrationPage::CaptureData>>
ProductionCalibrationPage::failedCaptures() const
{
    if (m_sessionPath.isEmpty() || m_currentCapture.frames.isEmpty()) return {};
    const QString phase = m_captureKind == CaptureKind::Low
        ? QStringLiteral("low")
        : m_captureKind == CaptureKind::High
            ? QStringLiteral("high")
            : m_captureKind == CaptureKind::HighB
                ? QStringLiteral("state_b_high")
                : QStringLiteral("offset");
    const QString prefix = QStringLiteral("%1_failed_%2")
                               .arg(phase, QDateTime::currentDateTime().toString(
                                               QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    return {{prefix, m_currentCapture}};
}

void ProductionCalibrationPage::savePackageAsync(
    const QString &directory,
    QList<QPair<QString, CaptureData>> failedCaptures,
    SaveCompletion completion)
{
    if (directory.isEmpty()) {
        if (completion) completion(true, {});
        return;
    }
    if (m_shuttingDown || m_saveActive) {
        if (completion) completion(false, tr("已有保存任务正在运行或页面正在关闭"));
        return;
    }

    ArchiveSnapshot snapshot = archiveSnapshot();
    auto result = std::make_shared<QPair<bool, QString>>();
    auto completionHolder = std::make_shared<SaveCompletion>(
        std::move(completion));
    QPointer<ProductionCalibrationPage> page(this);
    m_saveActive = true;
    updateControls();
    const bool accepted = m_ioTasks.run(
        [page, snapshot = std::move(snapshot), directory,
         failedCaptures = std::move(failedCaptures), result,
         completionHolder]() mutable {
            result->first = writePackageSnapshot(snapshot, directory,
                                                 failedCaptures,
                                                 &result->second);
            if (!page) return;
            QMetaObject::invokeMethod(
                page,
                [page, result, completionHolder]() mutable {
                    if (!page) return;
                    page->m_saveActive = false;
                    if (*completionHolder) {
                        (*completionHolder)(result->first, result->second);
                    }
                    page->updateControls();
                },
                Qt::QueuedConnection);
        });
    if (!accepted) {
        m_saveActive = false;
        if (*completionHolder) {
            (*completionHolder)(false, tr("后台 I/O 队列已满"));
        }
        updateControls();
    }
}

bool ProductionCalibrationPage::writePackageSnapshot(
    const ArchiveSnapshot &snapshot, const QString &directory,
    const QList<QPair<QString, CaptureData>> &failedCaptures,
    QString *error)
{
    if (!QDir().mkpath(directory)) {
        if (error) *error = tr("无法创建目录：%1").arg(directory);
        return false;
    }
    for (const auto &[prefix, capture] : failedCaptures) {
        if (!writeAtomic(QDir(directory).filePath(
                             prefix + QStringLiteral("_raw.bin")),
                         captureBytes(capture), error)
            || !writeAtomic(QDir(directory).filePath(
                                prefix + QStringLiteral("_acceptance.csv")),
                            acceptanceCsv(capture), error)) {
            return false;
        }
    }
    QJsonObject files;
    const auto save = [&](const QString &name, const QByteArray &data) {
        if (data.isEmpty()) return true;
        if (!writeAtomic(QDir(directory).filePath(name), data, error)) return false;
        QJsonObject item;
        item.insert(QStringLiteral("bytes"), data.size());
        item.insert(QStringLiteral("sha256"), sha256(data));
        files.insert(name, item);
        return true;
    };
    if (!save(QStringLiteral("low_raw.bin"), captureBytes(snapshot.low))
        || !save(QStringLiteral("low_acceptance.csv"), acceptanceCsv(snapshot.low))
        || !save(QStringLiteral("high_raw.bin"), captureBytes(snapshot.high))
        || !save(QStringLiteral("high_acceptance.csv"), acceptanceCsv(snapshot.high))
        || !save(QStringLiteral("state_b_high_raw.bin"),
                 captureBytes(snapshot.highB))
        || !save(QStringLiteral("state_b_high_acceptance.csv"),
                 acceptanceCsv(snapshot.highB))
        || !save(QStringLiteral("offset_raw.bin"), captureBytes(snapshot.offset))
        || !save(QStringLiteral("offset_acceptance.csv"), acceptanceCsv(snapshot.offset))
        || !save(QStringLiteral("low_mean_u16le.bin"), meanImageBytes(snapshot.low.meanPixels))
        || !save(QStringLiteral("high_mean_u16le.bin"), meanImageBytes(snapshot.high.meanPixels))
        || !save(QStringLiteral("state_b_high_mean_u16le.bin"),
                 meanImageBytes(snapshot.highB.meanPixels))
        || !save(QStringLiteral("monitor_trace.csv"),
                 monitorTraceCsv(snapshot.monitorTrace))
        || !save(QStringLiteral("offset_mean_u16le.bin"), meanImageBytes(snapshot.offset.meanPixels))) {
        return false;
    }
    if (snapshot.calibration.valid) {
        if (!save(QStringLiteral("gain_f32le.bin"), snapshot.calibration.gain)
            || !save(QStringLiteral("bad_points.bin"), snapshot.calibration.badPoints)
            || !save(QStringLiteral("calibration_v1.bin"), snapshot.calibration.packet)) {
            return false;
        }
    }

    QByteArray summary("metric,value\n");
    summary += "board_id," + snapshot.boardId.toUtf8().replace(',', '_') + "\n";
    summary += "sensor_id," + snapshot.sensorId.toUtf8().replace(',', '_') + "\n";
    summary += "firmware_sha256," + QByteArray(ProductionFirmwareSha256) + "\n";
    summary += "low_received," + QByteArray::number(snapshot.low.frames.size()) + "\n";
    summary += "low_accepted," + QByteArray::number(snapshot.low.acceptedIndices.size()) + "\n";
    summary += "high_received," + QByteArray::number(snapshot.high.frames.size()) + "\n";
    summary += "high_accepted," + QByteArray::number(snapshot.high.acceptedIndices.size()) + "\n";
    summary += "state_b_high_received," + QByteArray::number(snapshot.highB.frames.size()) + "\n";
    summary += "state_b_high_accepted," + QByteArray::number(snapshot.highB.acceptedIndices.size()) + "\n";
    summary += "offset_received," + QByteArray::number(snapshot.offset.frames.size()) + "\n";
    summary += "offset_accepted," + QByteArray::number(snapshot.offset.acceptedIndices.size()) + "\n";
    summary += "low_mean_span_adu," + QByteArray::number(acceptedMeanSpan(snapshot.low), 'f', 6) + "\n";
    summary += "low_vtemp_span_adu," + QByteArray::number(acceptedVtempSpan(snapshot.low)) + "\n";
    summary += "high_mean_span_adu," + QByteArray::number(acceptedMeanSpan(snapshot.high), 'f', 6) + "\n";
    summary += "high_vtemp_span_adu," + QByteArray::number(acceptedVtempSpan(snapshot.high)) + "\n";
    summary += "state_b_high_mean_span_adu," + QByteArray::number(acceptedMeanSpan(snapshot.highB), 'f', 6) + "\n";
    summary += "state_b_high_vtemp_span_adu," + QByteArray::number(acceptedVtempSpan(snapshot.highB)) + "\n";
    summary += "offset_mean_span_adu," + QByteArray::number(acceptedMeanSpan(snapshot.offset), 'f', 6) + "\n";
    summary += "offset_vtemp_span_adu," + QByteArray::number(acceptedVtempSpan(snapshot.offset)) + "\n";
    summary += "mean_diff," + QByteArray::number(snapshot.calibration.meanDiff, 'f', 6) + "\n";
    summary += "bad_point_count," + QByteArray::number(snapshot.calibration.badPointCount) + "\n";
    summary += "single_point_offset," + QByteArray(snapshot.calibration.singlePointOffsetApplied ? "true\n" : "false\n");
    summary += "write_readback_verified," + QByteArray(snapshot.calibration.writeVerified ? "true\n" : "false\n");
    summary += "monitor_continuity_valid," + QByteArray(snapshot.monitorContinuityValid ? "true\n" : "false\n");
    summary += "cycle_over_120s_warning," + QByteArray(snapshot.cycleWarningIssued ? "true\n" : "false\n");
    if (!save(QStringLiteral("summary.csv"), summary)) return false;

    const auto telemetryJson = [](const TelemetryState &state) {
        QJsonObject object;
        object.insert(QStringLiteral("valid"), state.valid);
        if (!state.valid) return object;
        object.insert(QStringLiteral("timestamp_us"), state.timestampUs);
        if (std::isfinite(state.ntcC)) {
            object.insert(QStringLiteral("ntc_c"), state.ntcC);
        }
        object.insert(QStringLiteral("ntc_adu"), state.ntcAdu);
        object.insert(QStringLiteral("vtemp_adu"), state.vtempAdu);
        object.insert(QStringLiteral("smooth_vtemp_adu"),
                      state.smoothVtempAdu);
        object.insert(QStringLiteral("nuc_decision"), state.nucDecision);
        object.insert(QStringLiteral("nuc_count"), state.nucCount);
        object.insert(QStringLiteral("nuc_dg"), state.nucDg);
        object.insert(QStringLiteral("nuc_dv"), state.nucDv);
        object.insert(QStringLiteral("nuc_dn"), state.nucDn);
        object.insert(QStringLiteral("cumulative_nuc_dg"),
                      state.cumulativeNucDg);
        object.insert(QStringLiteral("boot_ntc_adu"), state.bootNtcAdu);
        object.insert(QStringLiteral("anchor"), state.anchor);
        object.insert(QStringLiteral("effective_anchor"),
                      state.effectiveAnchor);
        object.insert(QStringLiteral("mean_diff"), state.meanDiff);
        return object;
    };
    const auto captureJson = [&telemetryJson](const CaptureData &capture) {
        QJsonObject object;
        object.insert(QStringLiteral("required_frames"), capture.requiredFrames);
        object.insert(QStringLiteral("received_frames"), capture.frames.size());
        object.insert(QStringLiteral("accepted_frames"), capture.acceptedIndices.size());
        object.insert(QStringLiteral("rejected_frames"),
                      capture.frames.size() - capture.acceptedIndices.size());
        object.insert(QStringLiteral("complete"), capture.complete);
        object.insert(QStringLiteral("accepted_mean_span_adu"),
                      ProductionCalibrationPage::acceptedMeanSpan(capture));
        object.insert(QStringLiteral("before"), telemetryJson(capture.before));
        object.insert(QStringLiteral("after"), telemetryJson(capture.after));
        QJsonObject rejectionCounts;
        for (const FrameInfo &info : capture.frameInfo) {
            if (!info.accepted && !info.rejectionReason.isEmpty()) {
                rejectionCounts.insert(
                    info.rejectionReason,
                    rejectionCounts.value(info.rejectionReason).toInt() + 1);
            }
        }
        object.insert(QStringLiteral("rejection_counts"), rejectionCounts);
        QJsonArray accepted;
        double vtempSum = 0.0;
        int vtempMin = std::numeric_limits<int>::max();
        int vtempMax = 0;
        for (const int index : capture.acceptedIndices) {
            accepted.append(index);
            const int value = capture.frameInfo.value(index).vtemp;
            vtempSum += value;
            vtempMin = std::min(vtempMin, value);
            vtempMax = std::max(vtempMax, value);
        }
        object.insert(QStringLiteral("accepted_indices"), accepted);
        if (!capture.acceptedIndices.isEmpty()) {
            QJsonObject vtemp;
            vtemp.insert(QStringLiteral("min_adu"), vtempMin);
            vtemp.insert(QStringLiteral("mean_adu"),
                         vtempSum / capture.acceptedIndices.size());
            vtemp.insert(QStringLiteral("max_adu"), vtempMax);
            vtemp.insert(QStringLiteral("span_adu"), vtempMax - vtempMin);
            object.insert(QStringLiteral("vtemp"), vtemp);
        }
        return object;
    };

    QJsonObject configs;
    const auto addConfig = [&configs](const QString &name, const char *hex) {
        QJsonObject config;
        const QByteArray text(hex);
        config.insert(QStringLiteral("hex"), QString::fromLatin1(text));
        config.insert(QStringLiteral("sha256"),
                      ProductionCalibrationPage::sha256(QByteArray::fromHex(text)));
        configs.insert(name, config);
    };
    addConfig(QStringLiteral("reset_primary"), ResetConfig);
    addConfig(QStringLiteral("reset_alternate"), ResetAlternateConfig);
    addConfig(QStringLiteral("init"), InitConfig);

    QJsonObject calibration;
    calibration.insert(QStringLiteral("valid"), snapshot.calibration.valid);
    calibration.insert(QStringLiteral("mean_diff_adu"), snapshot.calibration.meanDiff);
    calibration.insert(QStringLiteral("t_ref_low_c"), snapshot.calibration.tRefLow);
    calibration.insert(QStringLiteral("t_ref_high_c"), ReferenceHighC);
    calibration.insert(QStringLiteral("bad_point_count"), snapshot.calibration.badPointCount);
    calibration.insert(QStringLiteral("single_point_offset_applied"),
                       snapshot.calibration.singlePointOffsetApplied);
    calibration.insert(QStringLiteral("write_readback_verified"),
                       snapshot.calibration.writeVerified);
    calibration.insert(QStringLiteral("payload_crc32"),
                       QStringLiteral("%1").arg(snapshot.writePayloadCrc, 8, 16,
                                                QLatin1Char('0')));
    calibration.insert(QStringLiteral("header_crc32"),
                       QStringLiteral("%1").arg(snapshot.writeHeaderCrc, 8, 16,
                                                QLatin1Char('0')));
    calibration.insert(QStringLiteral("flash_header_crc32"),
                       QStringLiteral("%1").arg(snapshot.expectedFlashHeaderCrc,
                                                8, 16, QLatin1Char('0')));

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"), QStringLiteral("tn160.production-calibration.v1"));
    manifest.insert(QStringLiteral("data_contract_version"), 2);
    manifest.insert(QStringLiteral("algorithm"),
                    QStringLiteral("cali_ref_stream_v5_pc_avg32_spatial_guard"));
    manifest.insert(QStringLiteral("algorithm_version"), 3);
    manifest.insert(QStringLiteral("saved_at"),
                    QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    manifest.insert(QStringLiteral("sensor_id"), snapshot.sensorId);
    manifest.insert(QStringLiteral("board_id"), snapshot.boardId);
    manifest.insert(QStringLiteral("firmware_sha256"),
                    QString::fromLatin1(ProductionFirmwareSha256));
    manifest.insert(QStringLiteral("raw_baud"), RawBaud);
    manifest.insert(QStringLiteral("raw_frame_bytes"), RawFrameSize);
    manifest.insert(QStringLiteral("required_accepted_frames"), RequiredFrames);
    manifest.insert(QStringLiteral("state_b_high_required_accepted_frames"),
                    StateBHighFrames);
    manifest.insert(QStringLiteral("state_b_min_delta_ntc_adu"),
                    MinimumStateBDeltaNtcAdu);
    manifest.insert(QStringLiteral("state_b_min_accepted_ffc_delta"),
                    MinimumStateBFfcDelta);
    manifest.insert(QStringLiteral("target_cycle_seconds"),
                    double(TargetCycleUs) / 1000000.0);
    manifest.insert(QStringLiteral("target_cycle_timeout_is_failure"), false);
    manifest.insert(QStringLiteral("cycle_over_target_warning"),
                    snapshot.cycleWarningIssued);
    manifest.insert(QStringLiteral("monitor_continuity_valid"),
                    snapshot.monitorContinuityValid);
    manifest.insert(QStringLiteral("state_a_reference"),
                    telemetryJson(snapshot.stateAReference));
    manifest.insert(QStringLiteral("startup_discard_frames"),
                    Tn160ProductionFrameFilter::StartupDiscardFrames);
    manifest.insert(QStringLiteral("frame_mean_reject_adu"),
                    Tn160ProductionFrameFilter::MeanSpanLimitAdu);
    manifest.insert(QStringLiteral("accepted_mean_span_limit_adu"),
                    Tn160ProductionFrameFilter::MeanSpanLimitAdu);
    QJsonObject spatialGate;
    spatialGate.insert(QStringLiteral("global_mean_removed"), true);
    spatialGate.insert(QStringLiteral("history_frames"),
                       int(Tn160ProductionFrameFilter::SpatialHistoryFrames));
    spatialGate.insert(QStringLiteral("median_scale"),
                       Tn160ProductionFrameFilter::SpatialMedianScale);
    spatialGate.insert(QStringLiteral("mad_scale"),
                       Tn160ProductionFrameFilter::SpatialMadScale);
    spatialGate.insert(QStringLiteral("quantization_floor_adu"),
                       Tn160ProductionFrameFilter::SpatialQuantizationFloorAdu);
    manifest.insert(QStringLiteral("spatial_consistency_gate"), spatialGate);
    manifest.insert(QStringLiteral("sensor_320bit_config"), configs);
    QJsonObject captures;
    captures.insert(QStringLiteral("low"), captureJson(snapshot.low));
    captures.insert(QStringLiteral("high"), captureJson(snapshot.high));
    captures.insert(QStringLiteral("state_b_high"),
                    captureJson(snapshot.highB));
    captures.insert(QStringLiteral("offset"), captureJson(snapshot.offset));
    manifest.insert(QStringLiteral("captures"), captures);
    manifest.insert(QStringLiteral("calibration"), calibration);
    manifest.insert(QStringLiteral("files"), files);
    const QByteArray json = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    return writeAtomic(QDir(directory).filePath(QStringLiteral("manifest.json")),
                       json, error);
}

bool ProductionCalibrationPage::writeAtomic(const QString &fileName,
                                            const QByteArray &data,
                                            QString *error)
{
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = tr("写入 %1 失败：%2").arg(fileName, file.errorString());
        return false;
    }
    return true;
}

QByteArray ProductionCalibrationPage::captureBytes(const CaptureData &capture)
{
    QByteArray bytes;
    bytes.reserve(capture.frames.size() * RawFrameSize);
    for (const QByteArray &frame : capture.frames) bytes.append(frame);
    return bytes;
}

QByteArray ProductionCalibrationPage::acceptanceCsv(const CaptureData &capture)
{
    if (capture.frames.isEmpty()) return {};
    QByteArray csv("frame_index,accepted,mean_adu,vtemp_adu\n");
    for (int index = 0; index < capture.frameInfo.size(); ++index) {
        const FrameInfo &info = capture.frameInfo[index];
        csv += QByteArray::number(index) + ',';
        csv += info.accepted ? "1," : "0,";
        csv += QByteArray::number(info.mean, 'f', 6) + ',';
        csv += QByteArray::number(info.vtemp) + '\n';
    }
    return csv;
}

QByteArray ProductionCalibrationPage::monitorTraceCsv(
    const QVector<TelemetryState> &trace)
{
    if (trace.isEmpty()) return {};
    QByteArray csv(
        "timestamp_us,elapsed_s,ntc_c,ntc_adu,vtemp_adu,smooth_vtemp_adu,"
        "nuc_decision,nuc_count,nuc_dg,nuc_dv,nuc_dn,cumulative_nuc_dg,"
        "boot_ntc_adu,mean_diff,anchor,effective_anchor\n");
    const qint64 originUs = trace.constFirst().timestampUs;
    for (const TelemetryState &state : trace) {
        if (!state.valid) continue;
        csv += QByteArray::number(state.timestampUs) + ',';
        csv += QByteArray::number(
                   double(state.timestampUs - originUs) / 1000000.0,
                   'f', 6)
            + ',';
        if (std::isfinite(state.ntcC)) {
            csv += QByteArray::number(state.ntcC, 'f', 9);
        }
        csv += ',' + QByteArray::number(state.ntcAdu) + ',';
        csv += QByteArray::number(state.vtempAdu) + ',';
        csv += QByteArray::number(state.smoothVtempAdu) + ',';
        csv += QByteArray::number(int(state.nucDecision)) + ',';
        csv += QByteArray::number(int(state.nucCount)) + ',';
        csv += QByteArray::number(state.nucDg) + ',';
        csv += QByteArray::number(state.nucDv) + ',';
        csv += QByteArray::number(state.nucDn) + ',';
        csv += QByteArray::number(state.cumulativeNucDg) + ',';
        csv += QByteArray::number(state.bootNtcAdu) + ',';
        csv += QByteArray::number(state.meanDiff, 'f', 6) + ',';
        csv += QByteArray::number(state.anchor) + ',';
        csv += QByteArray::number(state.effectiveAnchor) + '\n';
    }
    return csv;
}

double ProductionCalibrationPage::acceptedMeanSpan(const CaptureData &capture)
{
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (const int index : capture.acceptedIndices) {
        if (index < 0 || index >= capture.frameInfo.size()) continue;
        const double value = capture.frameInfo[index].mean;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    return std::isfinite(minimum) && std::isfinite(maximum)
        ? maximum - minimum : 0.0;
}

int ProductionCalibrationPage::acceptedVtempSpan(const CaptureData &capture)
{
    int minimum = std::numeric_limits<int>::max();
    int maximum = std::numeric_limits<int>::min();
    for (const int index : capture.acceptedIndices) {
        if (index < 0 || index >= capture.frameInfo.size()) continue;
        const int value = capture.frameInfo[index].vtemp;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    return minimum <= maximum ? maximum - minimum : 0;
}

QString ProductionCalibrationPage::badPointCoordinates(
    const QByteArray &badPoints)
{
    QStringList coordinates;
    for (int offset = 0; offset + 1 < badPoints.size(); offset += 2) {
        const int y = quint8(badPoints[offset]);
        const int x = quint8(badPoints[offset + 1]);
        if (x == 0xff && y == 0xff) break;
        coordinates.append(QStringLiteral("(%1,%2)").arg(x).arg(y));
    }
    return coordinates.isEmpty() ? QObject::tr("无")
                                 : coordinates.join(QStringLiteral("、"));
}

QByteArray ProductionCalibrationPage::meanImageBytes(
    const QVector<quint16> &pixels)
{
    if (pixels.size() != PixelCount) return {};
    QByteArray bytes;
    bytes.reserve(ImageBytes);
    for (const quint16 value : pixels) appendLe16(bytes, value);
    return bytes;
}

QByteArray ProductionCalibrationPage::mapToGray(const QVector<quint16> &values)
{
    if (values.size() != PixelCount) return {};
    QVector<quint16> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const double minimum = sorted[sorted.size() / 100];
    const double maximum = sorted[sorted.size() * 99 / 100];
    const double range = std::max(1.0, maximum - minimum);
    QByteArray gray(values.size(), char(0));
    for (int index = 0; index < values.size(); ++index) {
        gray[index] = char(std::clamp(
            int((values[index] - minimum) * 255.0 / range), 0, 255));
    }
    return gray;
}

QByteArray ProductionCalibrationPage::gainToGray(const QByteArray &gainBytes)
{
    if (gainBytes.size() != GainBytes) return {};
    QVector<float> gain(PixelCount);
    QVector<float> sorted(PixelCount);
    for (int pixel = 0; pixel < PixelCount; ++pixel) {
        gain[pixel] = readLeFloat(gainBytes, pixel * 4);
        sorted[pixel] = gain[pixel];
    }
    std::sort(sorted.begin(), sorted.end());
    const double minimum = sorted[sorted.size() / 100];
    const double maximum = sorted[sorted.size() * 99 / 100];
    const double range = std::max(1e-9, maximum - minimum);
    QByteArray gray(PixelCount, char(0));
    for (int pixel = 0; pixel < PixelCount; ++pixel) {
        gray[pixel] = char(std::clamp(
            int((gain[pixel] - minimum) * 255.0 / range), 0, 255));
    }
    return gray;
}

quint32 ProductionCalibrationPage::crc32(const QByteArray &data)
{
    quint32 crc = 0xffffffffu;
    for (const char byte : data) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 mask = quint32(-qint32(crc & 1u));
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return crc ^ 0xffffffffu;
}

QString ProductionCalibrationPage::sha256(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString ProductionCalibrationPage::safeLabel(const QString &label)
{
    QString result = label.trimmed();
    result.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),
                   QStringLiteral("_"));
    result = result.left(64);
    return result.isEmpty() ? QStringLiteral("unlabeled") : result;
}

void ProductionCalibrationPage::updatePreviews()
{
    if (m_low.meanPixels.size() == PixelCount) {
        m_lowView->setFrame(mapToGray(m_low.meanPixels));
    } else {
        m_lowView->clear();
    }
    if (m_high.meanPixels.size() == PixelCount) {
        m_highView->setFrame(mapToGray(m_high.meanPixels));
    } else {
        m_highView->clear();
    }

    if (m_low.meanPixels.size() == PixelCount
        && m_high.meanPixels.size() == PixelCount) {
        QVector<quint16> difference(PixelCount);
        for (int pixel = 0; pixel < PixelCount; ++pixel) {
            difference[pixel] = quint16(std::max(
                0, int(m_high.meanPixels[pixel]) - int(m_low.meanPixels[pixel])));
        }
        m_diffView->setFrame(mapToGray(difference));
    } else {
        m_diffView->clear();
    }

    QVector<quint16> background;
    if (m_calibration.valid && m_calibration.background.size() == ImageBytes) {
        background.resize(PixelCount);
        for (int pixel = 0; pixel < PixelCount; ++pixel) {
            background[pixel] = readLe16(m_calibration.background, pixel * 2);
        }
    }
    if (background.size() == PixelCount) {
        m_backgroundView->setFrame(mapToGray(background));
    } else {
        m_backgroundView->clear();
    }
    if (m_calibration.valid) m_gainView->setFrame(gainToGray(m_calibration.gain));
    else m_gainView->clear();

    const auto captureQuality = [](const QString &name,
                                   const CaptureData &capture) {
        if (capture.frames.isEmpty()) return QObject::tr("%1：未采集").arg(name);
        return QObject::tr("%1：接受 %2/%3 · 均值跨度 %4 ADU · VTEMP 跨度 %5 ADU")
            .arg(name)
            .arg(capture.acceptedIndices.size())
            .arg(capture.frames.size())
            .arg(ProductionCalibrationPage::acceptedMeanSpan(capture), 0, 'f', 2)
            .arg(ProductionCalibrationPage::acceptedVtempSpan(capture));
    };
    QStringList qualityLines{
        captureQuality(tr("低温"), m_low),
        captureQuality(tr("高温"), m_high),
        captureQuality(tr("状态 B 高温"), m_highB),
        captureQuality(tr("Offset"), m_offset),
    };
    if (m_stateAReference.valid && !m_recentTelemetry.isEmpty()) {
        const TelemetryState &current = m_recentTelemetry.constLast();
        qualityLines.prepend(
            tr("状态 A/B：ΔNTC %1 ADU（门槛 |Δ|≥%2）· "
               "有效 FFC 增量 %3（门槛 ≥%4）· %5")
                .arg(int(current.ntcAdu) - int(m_stateAReference.ntcAdu))
                .arg(MinimumStateBDeltaNtcAdu)
                .arg(stateBFfcDelta(current))
                .arg(MinimumStateBFfcDelta)
                .arg(stateBReady(current) ? tr("B 已就绪")
                                          : tr("B 未就绪，继续等待")));
        if (m_cycleWarningIssued) {
            qualityLines.prepend(
                tr("节拍：已超过 120 s（仅告警，不判失败）"));
        }
    }
    qualityLines.append(
        tr("监控连续性：%1")
            .arg(m_monitorContinuityValid ? QStringLiteral("PASS")
                                          : QStringLiteral("FAIL")));
    if (m_calibration.valid) {
        qualityLines.append(
            tr("坏点 %1/5：%2")
                .arg(m_calibration.badPointCount)
                .arg(badPointCoordinates(m_calibration.badPoints)));
        qualityLines.append(
            tr("写入回读：%1")
                .arg(m_calibration.writeVerified ? QStringLiteral("PASS")
                                                 : tr("待执行")));
    } else {
        qualityLines.append(tr("坏点：尚未计算"));
    }
    m_quality->setText(qualityLines.join(QLatin1Char('\n')));

    if (m_calibration.valid) {
        m_summary->setText(
            tr("mean_diff %1 ADU · 坏点 %2/5 · 单点 offset %3 · 写入回读 %4 · "
               "120 s 为节拍目标，超出不判失败")
                .arg(m_calibration.meanDiff, 0, 'f', 3)
                .arg(m_calibration.badPointCount)
                .arg(m_calibration.singlePointOffsetApplied ? tr("已应用") : tr("未应用"))
                .arg(m_calibration.writeVerified ? QStringLiteral("PASS") : tr("待执行")));
    }
}

void ProductionCalibrationPage::updateControls()
{
    const bool idle = !operationActive();
    const bool connected = idle && m_transport.isActive();
    const bool labelsEditable = idle && m_sessionPath.isEmpty();
    m_port->setEnabled(idle && !m_transport.isActive());
    if (m_connectionButton) {
        m_connectionButton->setEnabled(idle);
        m_connectionButton->setText(m_transport.isActive() ? tr("断开") : tr("连接"));
        m_connectionButton->setProperty("kind", m_transport.isActive()
                                                   ? QStringLiteral("warning")
                                                   : QStringLiteral("primary"));
        m_connectionButton->style()->unpolish(m_connectionButton);
        m_connectionButton->style()->polish(m_connectionButton);
    }
    m_sensorId->setEnabled(labelsEditable);
    m_boardId->setEnabled(labelsEditable);
    m_newSensor->setEnabled(idle);
    m_lowButton->setEnabled(connected);
    m_highButton->setEnabled(connected);
    m_computeButton->setEnabled(idle && m_low.complete && m_high.complete);
    m_importButton->setEnabled(idle);
    /* State B is a data-quality gate, not a UI reachability gate. The
     * handlers provide the precise coverage/continuity reason and retain the
     * 120 s advisory override semantics. */
    m_highBButton->setEnabled(connected && m_calibration.valid);
    m_offsetButton->setEnabled(connected && m_calibration.valid);
    m_writeButton->setEnabled(connected && m_calibration.valid
                              && m_calibration.singlePointOffsetApplied);
    m_exportButton->setEnabled(idle && m_calibration.valid);
}

void ProductionCalibrationPage::setStatus(const QString &state,
                                          const QString &text)
{
    m_status->setText(text);
    m_status->setProperty("state", state);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
    emit transferStateChanged(state, text);
    if (m_host) m_host->setStatus(tr("TN160 · 生产标定 · %1").arg(text));
}

void ProductionCalibrationPage::log(const QString &message,
                                    const QString &level)
{
    if (m_host) m_host->log(level, QStringLiteral("TN160 PROD · %1").arg(message));
}

} // namespace serein::scope::tn160
