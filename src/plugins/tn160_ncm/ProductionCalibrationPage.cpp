#include "ProductionCalibrationPage.h"

#include "Tn160ProductionTelemetry.h"
#include "Tn160NcmProtocolUtils.h"

#include "ThermalView.h"
#include "Tn160ProtocolEngines.h"

#include <serein/scope/WorkspaceCanvas.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QEvent>
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
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace serein::scope::tn160ncm {
namespace {

constexpr int PixelCount = FrameWidth * FrameHeight;
constexpr int RawWordsPerRow = FrameWidth + 2;
constexpr int RawFrameSize = 2 + FrameHeight * RawWordsPerRow * 2 + 2;
constexpr int FfcSoftSilenceMs = 700;
constexpr int RawHardSilenceMs = 10000;
constexpr int RequiredFrames = 32;
constexpr int MaximumFrames = 64;
constexpr int SnapshotFrames = 3;
constexpr qint64 MonitorTraceIntervalUs = 1000000;
constexpr int ContinuousAdc16IntervalMs = 500;
constexpr qint64 TelemetryPairMaxDeltaMs = 1000;
constexpr int NetworkTransitionTimeoutMs = 30000;
constexpr int FirmwareReconnectDelayMs = 400;
constexpr int ImageBytes = PixelCount * int(sizeof(quint16));
constexpr int ProductSegment = 17;
constexpr int GainBytes = PixelCount * int(sizeof(float));
constexpr int BadPointBytes = 10;
constexpr int WriteHeaderBytes = 64;
constexpr quint16 NtcModelTag = 0x4e54u;
constexpr quint16 ColdStartModelTag = 0x4354u;
constexpr quint16 RowRefModelTag = 0x5252u;
constexpr bool EnableRowRefProduction = false;
constexpr quint32 PostWriteValidationEarliestMs = 6000u;
constexpr quint32 PostWriteValidationWindowMs = 10000u;
constexpr qsizetype PostWriteValidationMinimumSamples = 10;
constexpr quint32 ColdStartRecordEndMs = ColdStartCaptureEndMs + 2000u;
constexpr qsizetype MaximumColdStartRecords = 256;
constexpr quint8 RawCommand = 0xaa;
constexpr quint8 MonitorCommand = 0x11;
constexpr float ReferenceLowC = 0.0f;
constexpr float ReferenceHighC = 50.0f;
constexpr float MinimumMeanDiffAdu = 100.0f;

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
    case CaptureKind::Low: return tr("0°C");
    case CaptureKind::High: return tr("50°C");
    case CaptureKind::None: break;
    }
    return tr("未知");
}

int ProductionCalibrationPage::requiredFramesFor(const CaptureKind kind)
{
    return kind == CaptureKind::None ? 0 : RequiredFrames;
}

ProductionCalibrationPage::ProductionCalibrationPage(IHostServices *host,
                                                       QWidget *parent)
    : QWidget(parent), m_host(host),
      m_transport(std::make_unique<Tn160Engine>(), this),
      m_verifyTransfer(this)
{
    setObjectName(QStringLiteral("tn160ProductionCalibrationPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_contextPanel = new QWidget(this);
    m_contextPanel->setObjectName(QStringLiteral("workspaceContextContent"));
    auto *contextLayout = new QVBoxLayout(m_contextPanel);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    contextLayout->setSpacing(0);
    m_productionLog = new QPlainTextEdit(m_contextPanel);
    m_productionLog->setObjectName(QStringLiteral("tn160ProductionLog"));
    m_productionLog->setReadOnly(true);
    m_productionLog->setMaximumBlockCount(300);
    m_productionLog->setPlaceholderText(tr("连接设备后显示生成记录"));
    contextLayout->addWidget(m_productionLog, 1);

    auto *rowSurface = new ComponentSurface(QString{}, this);
    m_rowFrame = rowSurface;
    m_rowFrame->setProperty("selected", false);
    m_rowFrame->setMinimumHeight(94);
    m_rowFrame->setMaximumHeight(106);
    m_rowFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_rowFrame->installEventFilter(this);
    auto *workflowLayout = new QHBoxLayout(rowSurface->contentHost());
    workflowLayout->setContentsMargins(10, 8, 10, 8);
    workflowLayout->setSpacing(7);

    auto *identity = new QWidget(m_rowFrame);
    identity->setObjectName(QStringLiteral("tn160ProductionIdentity"));
    identity->setAttribute(Qt::WA_TransparentForMouseEvents);
    identity->setMinimumWidth(166);
    identity->setMaximumWidth(198);
    auto *identityLayout = new QVBoxLayout(identity);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(2);
    m_stationTitle = new QLabel(tr("工位"), identity);
    m_stationTitle->setObjectName(QStringLiteral("componentTitle"));
    m_stationDevice = new QLabel(tr("等待设备"), identity);
    m_stationDevice->setObjectName(QStringLiteral("pageSubtitle"));
    m_stationDevice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rowStatus = new QLabel(tr("等待设备"), identity);
    m_rowStatus->setObjectName(QStringLiteral("statusPill"));
    m_ntcStatus = new QLabel(
        EnableRowRefProduction ? tr("RR参数：等待50°C采集")
                               : tr("温漂模式：通用补偿"),
        identity);
    m_ntcStatus->setObjectName(QStringLiteral("tn160ProductionNtcStatus"));
    m_ntcStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    identityLayout->addWidget(m_stationTitle);
    identityLayout->addWidget(m_stationDevice);
    identityLayout->addWidget(m_rowStatus);
    identityLayout->addWidget(m_ntcStatus);
    workflowLayout->addWidget(identity);

    auto *stepsPanel = new QWidget(m_rowFrame);
    stepsPanel->setObjectName(QStringLiteral("tn160ProductionSteps"));
    stepsPanel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    stepsPanel->setMinimumWidth(220);
    stepsPanel->setMaximumWidth(250);
    auto *stepsLayout = new QGridLayout(stepsPanel);
    stepsLayout->setContentsMargins(0, 0, 0, 0);
    stepsLayout->setHorizontalSpacing(6);
    stepsLayout->setVerticalSpacing(6);
    stepsLayout->setColumnStretch(0, 1);
    stepsLayout->setColumnStretch(1, 1);

    m_lowButton = new QPushButton(tr("1、0°C"), stepsPanel);
    m_lowButton->setToolTip(tr("1、对准 0°C"));
    m_highButton = new QPushButton(tr("2、50°C"), stepsPanel);
    m_highButton->setToolTip(tr("2、对准 50°C"));
    const QList<QPushButton *> steps{
        m_lowButton, m_highButton,
    };
    for (int index = 0; index < steps.size(); ++index) {
        QPushButton *button = steps.at(index);
        button->setObjectName(
            QStringLiteral("tn160ProductionStep%1").arg(index + 1));
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        button->setMinimumWidth(104);
        button->setMinimumHeight(28);
        button->setMaximumHeight(32);
        stepsLayout->addWidget(button, index / 2, index % 2);
        connect(button, &QPushButton::pressed, this, [this] {
            emit workspaceActivated(this);
        });
    }
    workflowLayout->addWidget(stepsPanel, 0, Qt::AlignVCenter);
    workflowLayout->addStretch(1);

    auto *actions = new QWidget(m_rowFrame);
    actions->setObjectName(QStringLiteral("tn160ProductionActions"));
    actions->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    actions->setMinimumWidth(104);
    actions->setMaximumWidth(118);
    auto *actionsLayout = new QVBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(6);
    m_recalibrateButton = new QPushButton(tr("重新标定"), actions);
    m_recalibrateButton->setObjectName(
        QStringLiteral("tn160ProductionRecalibrate"));
    m_recalibrateButton->setProperty("kind", QStringLiteral("warning"));
    m_recalibrateButton->hide();
    m_importButton = new QPushButton(tr("导入标定包"), actions);
    m_importButton->setObjectName(QStringLiteral("tn160ProductionImport"));
    m_exportButton = new QPushButton(tr("导出标定包"), actions);
    m_exportButton->setObjectName(QStringLiteral("tn160ProductionExport"));
    m_restartButton = new QPushButton(tr("重新开始本轮"), actions);
    m_restartButton->setObjectName(QStringLiteral("tn160ProductionRestart"));
    m_restartButton->setProperty("kind", QStringLiteral("warning"));
    m_writeButton = new QPushButton(tr("3、写入并验证"), actions);
    m_writeButton->setObjectName(QStringLiteral("tn160ProductionWrite"));
    m_writeButton->setProperty("kind", QStringLiteral("primary"));
    m_testWriteButton = new QPushButton(tr("写入测试件"), actions);
    m_testWriteButton->setObjectName(QStringLiteral("tn160ProductionWriteTest"));
    m_retryVerifyButton = new QPushButton(tr("重新连接并回读"), actions);
    m_retryVerifyButton->setObjectName(
        QStringLiteral("tn160ProductionRetryVerification"));
    actionsLayout->addWidget(m_recalibrateButton);
    actionsLayout->addWidget(m_importButton);
    actionsLayout->addWidget(m_exportButton);
    actionsLayout->addWidget(m_restartButton);
    actionsLayout->addWidget(m_writeButton);
    actionsLayout->addWidget(m_testWriteButton);
    actionsLayout->addWidget(m_retryVerifyButton);
    workflowLayout->addWidget(actions, 0, Qt::AlignVCenter);
    root->addWidget(m_rowFrame);

    m_outputPanel = new QWidget(this);
    m_outputPanel->setObjectName(QStringLiteral("tn160ProductionResultsPanel"));
    auto *previews = new QVBoxLayout(m_outputPanel);
    previews->setContentsMargins(0, 0, 0, 0);
    previews->setSpacing(10);
    const auto makePreview = [](const QString &title,
                                ThermalView::PaletteMode mode,
                                ThermalView **view) {
        const PreviewPanelChrome chrome = makePreviewPanel(title);
        chrome.panel->setMinimumSize(0, 0);
        chrome.panel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        *view = new ThermalView(chrome.panel);
        (*view)->setPaletteMode(mode);
        (*view)->setCrosshairVisible(false);
        (*view)->setMinimumSize(0, 0);
        (*view)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        chrome.bodyLayout->addWidget(*view, 1);
        return chrome.panel;
    };
    auto *upperRowPanel = new QWidget(m_outputPanel);
    upperRowPanel->setMinimumSize(0, 0);
    upperRowPanel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *upperRow = new QHBoxLayout(upperRowPanel);
    upperRow->setContentsMargins(0, 0, 0, 0);
    upperRow->setSpacing(10);
    upperRow->addWidget(makePreview(tr("0°C"),
                                    ThermalView::PaletteMode::Grayscale,
                                    &m_lowView), 1);
    upperRow->addWidget(makePreview(tr("50°C"),
                                    ThermalView::PaletteMode::Grayscale,
                                    &m_highView), 1);
    upperRow->addWidget(makePreview(tr("温差"),
                                    ThermalView::PaletteMode::Grayscale,
                                    &m_diffView), 1);
    previews->addWidget(upperRowPanel, 1);

    auto *lowerRowPanel = new QWidget(m_outputPanel);
    lowerRowPanel->setMinimumSize(0, 0);
    lowerRowPanel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *lowerRow = new QHBoxLayout(lowerRowPanel);
    lowerRow->setContentsMargins(0, 0, 0, 0);
    lowerRow->setSpacing(10);
    lowerRow->addWidget(makePreview(tr("Offset"),
                                    ThermalView::PaletteMode::Grayscale,
                                    &m_backgroundView), 1);
    lowerRow->addWidget(makePreview(tr("Gain"),
                                    ThermalView::PaletteMode::Gain,
                                    &m_gainView), 1);
    const PreviewPanelChrome qualityChrome = makePreviewPanel(tr("采集质量"));
    qualityChrome.panel->setMinimumSize(0, 0);
    qualityChrome.panel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_quality = new QLabel(tr("等待生产标定数据"), qualityChrome.panel);
    m_quality->setObjectName(QStringLiteral("tn160ProductionQuality"));
    m_quality->setWordWrap(true);
    m_quality->setMinimumSize(0, 0);
    m_quality->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_quality->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_quality->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    qualityChrome.bodyLayout->addWidget(m_quality, 1);
    lowerRow->addWidget(qualityChrome.panel, 1);
    previews->addWidget(lowerRowPanel, 1);
    m_lowView->setObjectName(QStringLiteral("tn160ProductionLowView"));
    m_highView->setObjectName(QStringLiteral("tn160ProductionHighView"));
    m_diffView->setObjectName(QStringLiteral("tn160ProductionDiffView"));
    m_backgroundView->setObjectName(QStringLiteral("tn160ProductionBackgroundView"));
    m_gainView->setObjectName(QStringLiteral("tn160ProductionGainView"));
    m_timeout.setSingleShot(true);
    m_monitorFrameTimeout.setSingleShot(true);
    connect(m_recalibrateButton, &QPushButton::clicked,
            this, &ProductionCalibrationPage::requestProductRecalibration);
    connect(m_lowButton, &QPushButton::clicked, this, &ProductionCalibrationPage::captureLow);
    connect(m_highButton, &QPushButton::clicked, this, &ProductionCalibrationPage::captureHigh);
    connect(m_importButton, &QPushButton::clicked, this, &ProductionCalibrationPage::importCalibration);
    connect(m_exportButton, &QPushButton::clicked, this, &ProductionCalibrationPage::exportCalibration);
    connect(m_restartButton, &QPushButton::clicked,
            this, &ProductionCalibrationPage::restartCurrentRun);
    connect(m_writeButton, &QPushButton::clicked,
            this, &ProductionCalibrationPage::writeAndVerify);
    connect(m_testWriteButton, &QPushButton::clicked,
            this, &ProductionCalibrationPage::writeTestAndVerify);
    connect(m_retryVerifyButton, &QPushButton::clicked,
            this, &ProductionCalibrationPage::retryWriteVerification);
    connect(&m_transport, &Tn160NcmTransport::stateChanged,
            this, &ProductionCalibrationPage::handleTransportState);
    connect(&m_transport, &Tn160NcmTransport::packetsDecoded,
            this, &ProductionCalibrationPage::handleDecodedPackets);
    connect(&m_transport, &Tn160NcmTransport::monitorFrameMetadata,
            this, &ProductionCalibrationPage::handleMonitorFrameMetadata);
    connect(&m_transport, &Tn160NcmTransport::continuousAdc16Record,
            this, &ProductionCalibrationPage::handleContinuousAdc16Record);
    connect(&m_transport, &Tn160NcmTransport::errorOccurred,
            this, &ProductionCalibrationPage::handleTransportError);
    connect(&m_transport, &Tn160NcmTransport::calibrationUploadFinished,
            this, [this](const bool success, const QVariantMap &result,
                         const QString &error) {
                if (m_operation != Operation::Writing) return;
                if (!success) {
                    m_writeCommitAttempted = false;
                    m_writeAwaitingVerification = false;
                    m_timeout.stop();
                    m_productAddressCommitted = true;
                    m_finalizationStage = FinalizationStage::None;
                    const QString message =
                        tr("最终事务结果不确定：%1").arg(error);
                    failOperation(message);
                    emit finalizationFailed(this, message);
                    return;
                }
                const auto parseCrc = [](const QVariant &value) {
                    bool ok = false;
                    const quint32 result = value.toString().toUInt(&ok, 16);
                    return ok ? result : quint32(0);
                };
                completeNcmWrite(
                    parseCrc(result.value(QStringLiteral("payload_crc32"))),
                    parseCrc(result.value(QStringLiteral("package_header_crc32"))),
                    parseCrc(result.value(QStringLiteral("flash_header_crc32"))));
            });
    connect(&m_transport, &Tn160NcmTransport::networkSegmentChangeFinished,
            this, [this](const bool success, const int segment,
                         const QString &error) {
                if (m_finalizationStage
                    != FinalizationStage::SwitchingToProduct) {
                    return;
                }
                if (!success || segment != 17) {
                    m_finalizationStage = FinalizationStage::None;
                    const QString message =
                        tr("最终地址设置失败：%1").arg(error);
                    failOperation(message);
                    emit finalizationFailed(this, message);
                    return;
                }
                m_productAddressCommitted = true;
                setPort(QStringLiteral("http://192.168.17.1"));
                m_finalizationStage =
                    FinalizationStage::ReconnectingProduct;
                setStatus(QStringLiteral("connecting"),
                          tr("正在确认最终地址"));
                QTimer::singleShot(2500, this, [this] {
                    if (!m_shuttingDown
                        && m_finalizationStage
                               == FinalizationStage::ReconnectingProduct) {
                        m_transport.openWhenAvailable(
                            QStringLiteral("http://192.168.17.1"),
                            NetworkTransitionTimeoutMs);
                    }
                });
            });
    connect(&m_timeout, &QTimer::timeout,
            this, &ProductionCalibrationPage::handleTimeout);
    connect(&m_monitorFrameTimeout, &QTimer::timeout, this, [this] {
        if (!m_transport.isActive()) return;
        const bool finalMonitorConfirmation =
            m_finalizationStage == FinalizationStage::AwaitingProductMonitor
            || m_finalizationStage == FinalizationStage::ValidatingProduct;
        if (m_monitorReadyAnnounced && !finalMonitorConfirmation) return;
        const QString message = finalMonitorConfirmation
            ? tr("30 秒内未完成RR模型回读和50°C验收")
            : tr("30 秒内未收到合法 SITHMETA/CRC 监控帧");
        if (finalMonitorConfirmation) {
            if (m_finalizationStage == FinalizationStage::ValidatingProduct) {
                finishPostWriteValidation(false, message);
                return;
            }
            m_finalizationStage = m_singleDeviceMode
                ? FinalizationStage::None
                : FinalizationStage::AwaitingRemoval;
            if (m_singleDeviceMode) {
                m_workflowState = QStringLiteral("verification-failed");
            }
            setStatus(QStringLiteral("error"),
                      m_singleDeviceMode
                          ? tr("回读后首帧异常")
                          : tr("最终首帧异常 · 请拔出"));
            log(m_singleDeviceMode
                    ? tr("%1；标定已写入并回读，但运行首帧未确认")
                          .arg(message)
                    : tr("%1；最终地址已提交，请拔出设备后处理")
                          .arg(message),
                QStringLiteral("error"));
            emit finalizationFailed(this, message);
            updateControls();
            return;
        }
        m_transport.close();
        setStatus(QStringLiteral("error"), tr("首帧验证超时"));
        log(tr("%1，设备未接入").arg(message), QStringLiteral("error"));
    });
    connect(&m_verifyTransfer, &CalibrationTransfer::payloadReady,
            this, &ProductionCalibrationPage::receiveVerifyPayload);
    connect(&m_verifyTransfer, &CalibrationTransfer::headerReady,
            this, &ProductionCalibrationPage::receiveVerifyHeader);
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

QWidget *ProductionCalibrationPage::outputPanel() const noexcept
{
    return m_outputPanel;
}

bool ProductionCalibrationPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_rowFrame
        && event->type() == QEvent::MouseButtonPress) {
        emit workspaceActivated(this);
    }
    return QWidget::eventFilter(watched, event);
}

void ProductionCalibrationPage::setStationPresentation(
    const int station, const QString &serial, const QString &state)
{
    if (m_stationTitle) {
        m_stationTitle->setText(tr("工位 %1").arg(station));
    }
    if (m_stationDevice) {
        const QString address = m_stationSegment >= 11 && m_stationSegment <= 17
            ? QStringLiteral("192.168.%1.1").arg(m_stationSegment)
            : tr("地址待分配");
        m_stationDevice->setText(
            serial.trimmed().isEmpty()
                ? address
                : tr("%1 · %2").arg(serial.trimmed(), address));
    }
    if (m_rowStatus && !state.isEmpty()) {
        m_rowStatus->setText(state);
    }
}

void ProductionCalibrationPage::setStationSelected(const bool selected)
{
    if (!m_rowFrame
        || m_rowFrame->property("selected").toBool() == selected) {
        return;
    }
    m_rowFrame->setProperty("selected", selected);
    m_rowFrame->style()->unpolish(m_rowFrame);
    m_rowFrame->style()->polish(m_rowFrame);
}

void ProductionCalibrationPage::shutdown()
{
    if (m_shuttingDown) return;
    m_timeout.stop();
    m_monitorFrameTimeout.stop();
    m_captureGapActive = false;
    m_captureSilence.invalidate();
    stopContinuousRecording();
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
    m_transport.shutdown();
    m_operation = Operation::Idle;
    m_ioTasks.closeAndWait();
    m_saveActive = false;
}

void ProductionCalibrationPage::refreshPorts()
{
    if (m_endpoint.trimmed().isEmpty()) {
        m_endpoint = m_singleDeviceMode
            ? QStringLiteral("http://192.168.17.1")
            : QStringLiteral("http://192.168.10.1");
    }
}

void ProductionCalibrationPage::setPort(const QString &port)
{
    const QString normalized = port.trimmed();
    if (normalized.isEmpty()) return;
    m_endpoint = normalized;
}

void ProductionCalibrationPage::resumeAutomaticConnection()
{
    m_autoConnectPaused = false;
}

void ProductionCalibrationPage::connectAutomatically()
{
    if (m_autoConnectPaused || operationActive() || m_transport.isActive()
        || m_transport.isConnecting()) {
        return;
    }
    m_transport.openWhenAvailable(m_endpoint,
                                  NetworkTransitionTimeoutMs);
    setStatus(QStringLiteral("connecting"), tr("等待 USB 网络恢复"));
    updateControls();
}

void ProductionCalibrationPage::scanAndConnect()
{
    const ProductionWorkflowActions actions = actionAvailability();
    if (!actions.scan) return;
    m_autoConnectPaused = false;
    if (m_transport.isConnecting()) m_transport.close();
    connectAutomatically();
}

QString ProductionCalibrationPage::deviceSerial() const
{
    return m_deviceSerial;
}

int ProductionCalibrationPage::stationSegment() const noexcept
{
    return m_stationSegment;
}

bool ProductionCalibrationPage::finalProductReady() const noexcept
{
    return m_finalizationStage == FinalizationStage::AwaitingRemoval;
}

bool ProductionCalibrationPage::productAddressCommitted() const noexcept
{
    return m_productAddressCommitted;
}

bool ProductionCalibrationPage::productionWriteAvailable() const noexcept
{
    return m_calibration.valid && m_low.complete && m_high.complete
           && (!EnableRowRefProduction || m_calibration.rowRefModel.valid);
}

bool ProductionCalibrationPage::testWriteAvailable() const noexcept
{
    return m_calibration.valid;
}

double ProductionCalibrationPage::ntcCoverageC() const noexcept
{
    return m_connectionReference.valid
        ? std::max(0.0, m_ntcMaximumC - m_ntcMinimumC) : 0.0;
}

QString ProductionCalibrationPage::workflowState() const
{
    return m_workflowState;
}

QString ProductionCalibrationPage::sessionPath() const
{
    return m_sessionPath;
}

void ProductionCalibrationPage::prepareForStation(
    const int segment, const QString &expectedSerial)
{
    if (segment < 11 || segment > 16 || operationActive()) return;
    if (m_transport.isActive() || m_transport.isConnecting()) {
        m_transport.close();
    }
    m_autoConnectPaused = false;
    m_singleDeviceMode = false;
    m_stationSegment = segment;
    m_expectedSerial = expectedSerial.trimmed();
    m_deviceSerial.clear();
    m_monitorReadyAnnounced = false;
    m_productAddressCommitted = false;
    m_finalizationStage = FinalizationStage::None;
    m_verifyPhase = VerifyPhase::None;
    resetCalibrationRun();
    setPort(QStringLiteral("http://192.168.%1.1").arg(segment));
    setStatus(QStringLiteral("disconnected"), tr("等待设备"));
}

void ProductionCalibrationPage::prepareForSingleDevice()
{
    if (operationActive()) return;
    if (m_transport.isActive() || m_transport.isConnecting()) {
        m_transport.close();
    }
    stopContinuousRecording();
    m_singleDeviceMode = true;
    m_autoConnectPaused = false;
    m_stationSegment = ProductSegment;
    m_expectedSerial.clear();
    m_deviceSerial.clear();
    m_monitorReadyAnnounced = false;
    m_productAddressCommitted = true;
    m_finalizationStage = FinalizationStage::None;
    m_verifyPhase = VerifyPhase::None;
    resetCalibrationRun();
    setPort(QStringLiteral("http://192.168.17.1"));
    setStatus(QStringLiteral("disconnected"), tr("等待固件或设备"));
    m_workflowState = QStringLiteral("waiting-device");
    emitWorkflowState();
}

void ProductionCalibrationPage::notifyFirmwareFlashed(
    const QString &firmwarePath, const QString &firmwareSha256,
    const QString &bootselRoot)
{
    if (m_shuttingDown || operationActive()
        || m_finalizationStage != FinalizationStage::None
        || m_transport.isConnecting()
        || (m_transport.isActive()
            && m_workflowState != QStringLiteral("waiting-device")
            && m_workflowState != QStringLiteral("disconnected"))) {
        log(tr("忽略新的 UF2 完成事件：当前设备流程尚未结束"),
            QStringLiteral("error"));
        return;
    }
    // Every successful UF2 write starts a new physical-device cycle.  Reset
    // the previous device identity, auto-connect pause and calibration data
    // before waiting for the newly flashed device to enumerate.
    prepareForSingleDevice();
    m_firmwarePath = firmwarePath;
    m_firmwareSha256 = firmwareSha256;
    m_bootselRoot = bootselRoot;
    m_workflowState = QStringLiteral("waiting-reboot");
    log(tr("UF2 已写入，等待设备在 192.168.17.1 重启；SHA-256=%1")
            .arg(firmwareSha256));
    emitWorkflowState();
    QTimer::singleShot(FirmwareReconnectDelayMs, this, [this] {
        if (!m_shuttingDown
            && m_workflowState == QStringLiteral("waiting-reboot")) {
            connectAutomatically();
        }
    });
}

void ProductionCalibrationPage::prepareForProductRecalibration(
    const QString &serial)
{
    if (operationActive()) return;
    if (m_transport.isActive() || m_transport.isConnecting()) {
        m_transport.close();
    }
    m_autoConnectPaused = false;
    m_singleDeviceMode = false;
    m_stationSegment = 17;
    m_expectedSerial = serial;
    m_deviceSerial.clear();
    m_monitorReadyAnnounced = false;
    m_productAddressCommitted = true;
    m_finalizationStage = FinalizationStage::None;
    m_verifyPhase = VerifyPhase::None;
    resetCalibrationRun();
    setPort(QStringLiteral("http://192.168.17.1"));
    setStatus(QStringLiteral("disconnected"),
              tr("已标定 · 可重新标定"));
}

void ProductionCalibrationPage::grantFinalization()
{
    if (m_finalizationStage != FinalizationStage::WaitingGrant
        || m_portForVerify.isEmpty()) {
        return;
    }
    if (!m_transport.isActive()) {
        m_finalizationStage = FinalizationStage::None;
        const QString message = tr("最终提交前设备已断开");
        failOperation(message);
        emit finalizationFailed(this, message);
        return;
    }
    m_finalizationStage = FinalizationStage::SwitchingToProduct;
    m_timeout.start(60000);
    setStatus(QStringLiteral("connecting"),
              tr("正在提交标定和最终地址"));
    log(tr("一次提交标定与 192.168.17.1；设备只重启一次"));
    m_transport.uploadCalibration(m_calibration.packet, true);
    updateControls();
}

bool ProductionCalibrationPage::operationActive() const noexcept
{
    return m_operation != Operation::Idle || m_verifyTransfer.isActive()
        || m_saveActive;
}

bool ProductionCalibrationPage::connectionActive() const noexcept
{
    return m_transport.isActive() || m_transport.isConnecting();
}

int ProductionCalibrationPage::baudRate() const noexcept
{
    return 0;
}

void ProductionCalibrationPage::toggleConnection()
{
    if (operationActive()) return;
    if (m_transport.isActive() || m_transport.isConnecting()) {
        m_autoConnectPaused = true;
        m_transport.close();
        return;
    }
    if (m_endpoint.isEmpty()) {
        failOperation(tr("请先填写 TN160 USB NCM 地址"));
        return;
    }
    m_autoConnectPaused = false;
    m_transport.openWhenAvailable(m_endpoint,
                                  NetworkTransitionTimeoutMs);
    setStatus(QStringLiteral("connecting"), tr("连接中"));
    updateControls();
}

void ProductionCalibrationPage::requestProductRecalibration()
{
    if (m_stationSegment != ProductSegment || operationActive()
        || m_finalizationStage != FinalizationStage::None
        || !m_transport.isActive() || !m_monitorReadyAnnounced
        || m_deviceSerial.isEmpty()) {
        return;
    }
    emit workspaceActivated(this);
    emit productRecalibrationRequested(this);
}

void ProductionCalibrationPage::resetCalibrationRun()
{
    if (operationActive()) return;
    const bool hadRunData = !m_low.frames.isEmpty()
                            || !m_high.frames.isEmpty()
                            || !m_coldStartRecords.isEmpty()
                            || m_calibration.valid
                            || !m_sessionPath.isEmpty();
    m_low = {};
    m_high = {};
    m_coldStartRecords.clear();
    m_coldStartSamples.clear();
    m_coldStartPairRejected = 0;
    m_coldStartWindowMissed = false;
    m_calibration = {};
    m_currentCapture = {};
    m_frameFilter.reset();
    stopContinuousRecording();
    m_recentTelemetry.clear();
    m_postCaptureTelemetry.clear();
    m_postWriteValidation.clear();
    m_monitorTrace.clear();
    m_telemetryHistory.clear();
    m_connectionReference = {};
    m_ntcMinimumC = 0.0;
    m_ntcMaximumC = 0.0;
    m_ntcMinimumAdu = 0;
    m_ntcMaximumAdu = 0;
    m_lastTraceTimestampUs = 0;
    m_lastGateUiSecond = -1;
    m_lastMonitorNucCount = -1;
    m_cumulativeNucDg = 0;
    m_monitorContinuityValid = true;
    m_disconnectCleanupPending = false;
    m_disconnectWasReady = false;
    m_disconnectCleanupSerial.clear();
    m_writeCommitAttempted = false;
    m_writeAwaitingVerification = false;
    m_sessionPath.clear();
    m_verifyPayloads.clear();
    m_verifyHeader.clear();
    m_verifyPhase = VerifyPhase::None;
    if (m_ntcStatus) {
        m_ntcStatus->setText(
            EnableRowRefProduction ? tr("RR参数：等待50°C采集")
                                   : tr("温漂模式：通用补偿"));
    }
    updatePreviews();
    if (hadRunData) {
        log(tr("本轮数据已清空，请从第 1 步重新开始"));
    }
    updateControls();
    emitWorkflowState();
}

void ProductionCalibrationPage::restartCurrentRun()
{
    if (!actionAvailability().restartRun) return;
    resetCalibrationRun();
    const bool connected = m_transport.isActive() && m_monitorReadyAnnounced;
    m_workflowState = connected ? QStringLiteral("ready-a1")
                                : QStringLiteral("disconnected");
    setStatus(connected ? QStringLiteral("connected")
                        : QStringLiteral("disconnected"),
              connected ? tr("本轮已重新开始 · 请冷机重启以记录启动")
                        : tr("本轮已重新开始 · 等待设备连接"));
    log(tr("本轮内存数据已清空；请冷机重启并保持固定目标 60 秒"));
    updateControls();
}

void ProductionCalibrationPage::clearWriteAttemptState()
{
    m_writeCommitAttempted = false;
    m_writeAwaitingVerification = false;
    m_waitingCloseForVerify = false;
    m_verifyPhase = VerifyPhase::None;
    m_verifyHeader.clear();
    m_verifyPayloads.clear();
    m_writeHeader.clear();
    m_writePayloadCrc = 0;
    m_writeHeaderCrc = 0;
    m_writeFlashHeaderCrc = 0;
    m_calibration.packet.clear();
    m_calibration.writeVerified = false;
    m_calibration.testWrite = false;
}

void ProductionCalibrationPage::invalidateDownstreamAfterCapture(
    const CaptureKind kind)
{
    switch (kind) {
    case CaptureKind::Low:
        m_high = {};
        m_calibration = {};
        if (m_ntcStatus) {
            m_ntcStatus->setText(
                EnableRowRefProduction ? tr("RR参数：等待50°C采集")
                                       : tr("温漂模式：通用补偿"));
        }
        clearWriteAttemptState();
        break;
    case CaptureKind::High:
        m_calibration = {};
        if (m_ntcStatus) {
            m_ntcStatus->setText(
                EnableRowRefProduction ? tr("RR参数：正在读取50°C行参考")
                                       : tr("温漂模式：通用补偿"));
        }
        clearWriteAttemptState();
        break;
    case CaptureKind::None:
        break;
    }
}

void ProductionCalibrationPage::updateWorkflowAfterCapture(
    const CaptureKind kind)
{
    switch (kind) {
    case CaptureKind::Low:
        m_workflowState = QStringLiteral("ready-a2");
        break;
    case CaptureKind::High:
        m_workflowState = QStringLiteral("calculating-a");
        break;
    case CaptureKind::None:
        break;
    }
}

void ProductionCalibrationPage::captureLow()
{
    requestCapture(CaptureKind::Low);
}

void ProductionCalibrationPage::captureHigh()
{
    requestCapture(CaptureKind::High);
}

void ProductionCalibrationPage::requestCapture(const CaptureKind kind)
{
    const ProductionWorkflowActions actions = actionAvailability();
    const bool allowed = kind == CaptureKind::Low
        ? actions.captureLow
        : kind == CaptureKind::High
            ? actions.captureHigh : false;
    if (!allowed) {
        const QString reason = captureBlockedReason(kind);
        if (!reason.isEmpty()) {
            setStatus(QStringLiteral("error"), tr("当前不能采集"));
            log(reason, QStringLiteral("warning"));
        }
        return;
    }
    beginCapture(kind);
}

QString ProductionCalibrationPage::captureBlockedReason(
    const CaptureKind kind) const
{
    if (operationActive() || m_finalizationStage != FinalizationStage::None) {
        return tr("当前操作尚未结束，不能开始新的采集");
    }
    if (m_writeAwaitingVerification && !m_calibration.writeVerified) {
        return tr("写入结果尚未确认，请先重新连接并回读");
    }
    if (!m_transport.isActive() || !m_monitorReadyAnnounced) {
        return tr("请先连接设备并等待首个合法热图帧");
    }
    if (kind == CaptureKind::High && !m_low.complete) {
        return tr("请先完成 A1 0°C");
    }
    return tr("当前状态不允许执行该采集");
}

void ProductionCalibrationPage::beginCapture(const CaptureKind kind)
{
    if (operationActive()) return;
    if (!m_transport.isActive()) {
        failOperation(tr("请先连接 TN160"));
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
    switch (kind) {
    case CaptureKind::Low:
        m_workflowState = QStringLiteral("capturing-a1");
        break;
    case CaptureKind::High:
        m_workflowState = QStringLiteral("capturing-a2");
        break;
    case CaptureKind::None:
        break;
    }
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
    log(tr("切换到 HTTP RAW_U16，开始%1 %2 帧采集")
            .arg(captureName(kind)).arg(m_currentCapture.requiredFrames));
    updateControls();
    emitWorkflowState();
}

void ProductionCalibrationPage::handleTransportState(const QString &state)
{
    if (state == QStringLiteral("connected")) {
        if (m_finalizationStage == FinalizationStage::ReconnectingTemporary
            || m_finalizationStage == FinalizationStage::ReconnectingProduct
            || m_finalizationStage == FinalizationStage::AwaitingProductMonitor
            || m_finalizationStage == FinalizationStage::ValidatingProduct) {
            m_lastMonitorNucCount = -1;
            m_cumulativeNucDg = 0;
        }
        m_monitorReadyAnnounced = false;
        m_deviceFirmwareIdentity = m_transport.deviceIdentity();
        m_deviceSerial = m_transport.deviceSerial();
        m_workflowState = QStringLiteral("verifying-first-frame");
        emitWorkflowState();
        const int connectedSegment = m_transport.networkSegment();
        if (m_expectedSerial.isEmpty()) {
            m_expectedSerial = m_deviceSerial;
        } else if (!m_deviceSerial.isEmpty()
                   && m_deviceSerial != m_expectedSerial) {
            const QString message = tr("地址对应设备已变化，请重新分配工位");
            m_transport.close();
            failOperation(message);
            emit finalizationFailed(this, message);
            return;
        }
        const int expectedSegment =
            m_finalizationStage == FinalizationStage::ReconnectingProduct
                    || m_finalizationStage
                           == FinalizationStage::AwaitingProductMonitor
                    || m_finalizationStage
                           == FinalizationStage::ValidatingProduct
                    || m_finalizationStage
                           == FinalizationStage::SavingValidation
                ? ProductSegment
                : m_stationSegment;
        if (expectedSegment >= 11 && connectedSegment != expectedSegment) {
            const QString message =
                tr("设备网段不匹配：期望 %1，实际 %2")
                    .arg(expectedSegment)
                    .arg(connectedSegment);
            m_transport.close();
            failOperation(message);
            emit finalizationFailed(this, message);
            return;
        }
        emit deviceIdentified(this, m_deviceSerial, connectedSegment);
        if (m_finalizationStage
            == FinalizationStage::ReconnectingTemporary) {
            m_finalizationStage = FinalizationStage::SwitchingToProduct;
            setStatus(QStringLiteral("connecting"), tr("正在设置最终地址"));
            m_transport.setNetworkSegment(17);
            return;
        }
        if (m_finalizationStage
            == FinalizationStage::ReconnectingProduct) {
            if (connectedSegment != ProductSegment) {
                const QString message =
                    tr("最终设备仍在错误网段 %1").arg(connectedSegment);
                m_transport.close();
                m_finalizationStage = FinalizationStage::None;
                failOperation(message);
                emit finalizationFailed(this, message);
                return;
            }
            m_transport.close();
            m_operation = Operation::Verifying;
            m_verifyPhase = VerifyPhase::ProductAddress;
            m_finalizationStage = FinalizationStage::VerifyingProduct;
            m_verifyHeader.clear();
            m_verifyPayloads.clear();
            setStatus(QStringLiteral("connecting"),
                      tr("正在验证最终设备"));
            QTimer::singleShot(100, this, [this] {
                if (m_operation == Operation::Verifying
                    && m_verifyPhase == VerifyPhase::ProductAddress) {
                    m_verifyTransfer.start(
                        CalibrationTransfer::Mode::Dump,
                        QStringLiteral("http://192.168.17.1"));
                }
            });
            return;
        }
        if (m_finalizationStage
                == FinalizationStage::AwaitingProductMonitor
            || m_finalizationStage
                == FinalizationStage::ValidatingProduct) {
            if (m_finalizationStage == FinalizationStage::ValidatingProduct) {
                return;
            }
            m_monitorFrameTimeout.start(NetworkTransitionTimeoutMs);
            m_transport.write(QByteArray(1, char(MonitorCommand)));
            setStatus(QStringLiteral("connecting"),
                      tr("最终回读通过 · 确认首帧"));
            log(tr("最终地址身份已确认，等待首个合法监控帧"));
            updateControls();
            return;
        }
        if (m_operation == Operation::Idle) {
            m_monitorFrameTimeout.start(NetworkTransitionTimeoutMs);
            m_transport.write(QByteArray(1, char(MonitorCommand)));
            setStatus(QStringLiteral("connecting"), tr("设备已识别 · 确认首帧"));
            log(tr("USB NCM 设备身份已确认，等待首个合法监控帧"));
        }
    } else if (state == QStringLiteral("disconnected")) {
        m_monitorFrameTimeout.stop();
        const bool wasReady = m_monitorReadyAnnounced;
        m_monitorReadyAnnounced = false;
        if (m_finalizationStage
            == FinalizationStage::AwaitingRemoval) {
            const QString serial = m_expectedSerial;
            m_finalizationStage = FinalizationStage::None;
            emit finalProductRemoved(this, serial);
            return;
        }
        if (m_finalizationStage == FinalizationStage::ValidatingProduct) {
            const QString message = tr("10秒RR验收期间设备断开");
            m_calibration.postWriteValidationAttempted = true;
            m_calibration.postWriteValidationPassed = false;
            m_finalizationStage = FinalizationStage::None;
            m_workflowState = QStringLiteral("validation-failed");
            setStatus(QStringLiteral("error"), tr("RR温漂验收失败"));
            log(message, QStringLiteral("error"));
            emit finalizationFailed(this, message);
            if (!m_singleDeviceMode) {
                emit finalProductRemoved(this, m_expectedSerial);
            }
            updateControls();
            emitWorkflowState();
            return;
        }
        if (m_finalizationStage == FinalizationStage::SavingValidation) {
            const QString message = tr("保存RR验收结果前设备被拔出");
            m_finalizationStage = FinalizationStage::None;
            m_calibration.postWriteValidationPassed = false;
            m_workflowState = QStringLiteral("validation-failed");
            setStatus(QStringLiteral("error"), tr("RR验收未完成"));
            log(message, QStringLiteral("error"));
            emit finalizationFailed(this, message);
            if (!m_singleDeviceMode) {
                emit finalProductRemoved(this, m_expectedSerial);
            }
            updateControls();
            emitWorkflowState();
            return;
        }
        if (m_finalizationStage
                == FinalizationStage::ReconnectingProduct
            || m_finalizationStage
                   == FinalizationStage::VerifyingProduct) {
            return;
        }
        if (m_waitingCloseForVerify) {
            m_waitingCloseForVerify = false;
            m_operation = Operation::Verifying;
            m_verifyPhase = VerifyPhase::TemporaryAddress;
            m_verifyHeader.clear();
            m_verifyPayloads.clear();
            setStatus(QStringLiteral("connecting"), tr("等待重启并回读"));
            QTimer::singleShot(2500, this, [this] {
                if (m_operation == Operation::Verifying) {
                    m_verifyTransfer.start(CalibrationTransfer::Mode::Dump,
                                           m_portForVerify);
                }
            });
        } else if (m_operation == Operation::Capturing) {
            m_disconnectCleanupPending = true;
            m_disconnectWasReady = wasReady;
            m_disconnectCleanupSerial = m_deviceSerial;
            m_expectedSerial.clear();
            m_deviceSerial.clear();
            stopContinuousRecording();
            if (m_captureRestoring) {
                finishCapture(false, tr("采集期间 USB NCM 断开"));
            } else {
                stopCapture(false, tr("采集期间 USB NCM 断开"));
            }
        } else if (m_operation == Operation::Writing) {
            m_finalizationStage = FinalizationStage::None;
            failOperation(tr("写入期间 USB NCM 断开；结果未知，需重新连接并回读"));
        } else if (m_disconnectCleanupPending) {
            setStatus(QStringLiteral("disconnected"), tr("设备已断开"));
            m_workflowState = QStringLiteral("disconnected");
            emitWorkflowState();
        } else if (m_operation == Operation::Idle) {
            if (m_singleDeviceMode && m_writeAwaitingVerification) {
                setStatus(QStringLiteral("error"),
                          tr("写入结果待确认 · 请重新连接并回读"));
                m_workflowState = QStringLiteral("verification-failed");
                emitWorkflowState();
                updateControls();
                return;
            }
            setStatus(QStringLiteral("disconnected"), tr("未连接"));
            if (m_singleDeviceMode) {
                const QString disconnectedSerial = m_deviceSerial;
                stopContinuousRecording();
                resetCalibrationRun();
                m_expectedSerial.clear();
                m_deviceSerial.clear();
                m_monitorReadyAnnounced = false;
                setStatus(QStringLiteral("disconnected"), tr("设备已断开"));
                m_workflowState = QStringLiteral("disconnected");
                emitWorkflowState();
                if (wasReady) {
                    emit deviceDisconnected(this, disconnectedSerial,
                                            ProductSegment);
                }
            } else if (m_finalizationStage == FinalizationStage::None) {
                const QString serial = m_expectedSerial;
                resetCalibrationRun();
                if (wasReady) {
                    emit deviceDisconnected(this, serial, m_stationSegment);
                }
            }
        }
    } else if (state == QStringLiteral("connecting")) {
        m_monitorReadyAnnounced = false;
        setStatus(QStringLiteral("connecting"), tr("连接中"));
        m_workflowState = QStringLiteral("connecting");
        emitWorkflowState();
    } else if (state == QStringLiteral("reconnecting")) {
        setStatus(QStringLiteral("connecting"),
                  tr("USB 网络重枚举中 · 自动重试"));
        m_workflowState = QStringLiteral("waiting-reboot");
        emitWorkflowState();
    } else if (state == QStringLiteral("disconnecting")) {
        setStatus(QStringLiteral("disconnecting"), tr("断开中"));
    }
    updateControls();
}

void ProductionCalibrationPage::handleDecodedPackets(const PacketList &packets)
{
    emit livePacketsDecoded(packets);
    handleMonitorPackets(packets);
    for (const Packet &packet : packets) {
        const auto *image = std::get_if<ImagePacket>(&packet);
        if (image && image->source == QString::fromLatin1(Tn160MonitorSource)
            && image->width == FrameWidth && image->height == FrameHeight
            && image->pixelFormat == QStringLiteral("gray8")
            && image->pixels.size() == FrameWidth * FrameHeight) {
            confirmMonitorConnection();
            break;
        }
    }
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
    }
}

void ProductionCalibrationPage::confirmMonitorConnection()
{
    if (!m_transport.isActive()) return;
    const bool firstMonitorFrame = !m_monitorReadyAnnounced;
    if (firstMonitorFrame) {
        m_monitorFrameTimeout.stop();
        m_monitorReadyAnnounced = true;
        QString error;
        if (!ensureSessionDirectory(&error)) {
            failOperation(tr("无法创建连续记录：%1").arg(error));
            return;
        }
        emit deviceConnected(this, m_deviceSerial, m_transport.networkSegment());
    }
    if (m_finalizationStage == FinalizationStage::AwaitingProductMonitor) {
        if (m_recentTelemetry.isEmpty()) {
            m_monitorFrameTimeout.start(NetworkTransitionTimeoutMs);
            setStatus(QStringLiteral("connecting"),
                      tr("最终首帧正常 · 等待新诊断"));
            return;
        }
        const TelemetryState &latest = m_recentTelemetry.constLast();
        const bool modelReported = m_calibration.rowRefModel.valid
            ? latest.rowRefModelReported : latest.ntcModelReported;
        if (!modelReported) {
            m_monitorFrameTimeout.start(NetworkTransitionTimeoutMs);
            setStatus(QStringLiteral("connecting"),
                      tr("最终首帧正常 · 等待新诊断"));
            return;
        }
        m_monitorFrameTimeout.stop();
        QString modelError;
        if (!reportedNtcModelMatches(latest, &modelError)) {
            m_finalizationStage = FinalizationStage::None;
            m_writeAwaitingVerification = true;
            m_calibration.writeVerified = false;
            m_workflowState = QStringLiteral("verification-failed");
            setStatus(QStringLiteral("error"), tr("写入后运行验证失败"));
            log(modelError, QStringLiteral("error"));
            emit finalizationFailed(this, modelError);
            updateControls();
            return;
        }
        if (!EnableRowRefProduction || !m_calibration.rowRefModel.valid) {
            m_writeAwaitingVerification = false;
            const bool productionPassed = !m_calibration.testWrite
                && m_calibration.valid && m_low.complete && m_high.complete
                && m_calibration.writeVerified;
            m_workflowState = productionPassed
                ? QStringLiteral("completed")
                : QStringLiteral("test-completed");
            if (m_singleDeviceMode) {
                m_finalizationStage = FinalizationStage::None;
                setStatus(QStringLiteral("connected"),
                          productionPassed
                              ? tr("通用基础V1写入并回读完成")
                              : tr("基础V1测试件写入并回读完成"));
                emit calibrationCompleted(m_deviceSerial, productionPassed,
                                          m_sessionPath);
            } else {
                m_finalizationStage = FinalizationStage::AwaitingRemoval;
                setStatus(productionPassed ? QStringLiteral("connected")
                                           : QStringLiteral("error"),
                          productionPassed
                              ? tr("通用标定完成 · 请拔出")
                              : tr("基础V1测试件非量产PASS · 请拔出"));
                if (productionPassed) {
                    emit finalProductReadyForRemoval(this, m_deviceSerial);
                } else {
                    emit finalizationFailed(
                        this, tr("导入测试件不具备本工位0°C/50°C证据"));
                }
            }
            log(productionPassed
                    ? tr("PASS：通用基础V1、同serial和全量回读一致")
                    : tr("基础V1仅作为测试/维修件完成回读，不标记量产PASS"));
            updateControls();
            emitWorkflowState();
            return;
        }
        beginPostWriteValidation(latest);
    } else if (firstMonitorFrame && m_operation == Operation::Idle
               && m_finalizationStage == FinalizationStage::None) {
        setStatus(QStringLiteral("connected"),
                  tr("设备已连接 · 可开始 0°C 标定"));
        log(EnableRowRefProduction
                ? tr("首帧已确认，可立即进行0°C/50°C标定；RR参数将在50°C采集后自动生成")
                : tr("首帧已确认，可立即进行0°C/50°C通用标定"));
        m_workflowState = QStringLiteral("ready-low");
        emitWorkflowState();
    }
    updateControls();
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

void ProductionCalibrationPage::handleMonitorFrameMetadata(
    const qint64 hostTimestampUs, const quint32 frameNumber,
    const quint32 deviceTimestampMs, const quint32 calibrationId,
    const quint16 ffcCount)
{
    Q_UNUSED(ffcCount)
    m_pendingMonitorHostTimestampUs = hostTimestampUs;
    m_pendingMonitorFrameNumber = frameNumber;
    m_pendingMonitorDeviceTimestampMs = deviceTimestampMs;
    m_pendingMonitorCalibrationId = calibrationId;
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
    if (packet.timestampUs == m_pendingMonitorHostTimestampUs) {
        state.frameNumber = m_pendingMonitorFrameNumber;
        state.deviceTimestampMs = m_pendingMonitorDeviceTimestampMs;
        state.calibrationId = m_pendingMonitorCalibrationId;
    }
    state.centerRoiC = packet.values.at(MonitorCenterRoiC);
    state.centerRoiAdu = quint16(
        boundedInteger(MonitorCenterRoiAdu, 0, 65535));
    state.ntcC = packet.values.at(MonitorNtcC);
    state.ntcAdu = quint16(boundedInteger(MonitorNtcAdu, 0, 65535));
    state.vtempAdu = quint16(boundedInteger(MonitorVtemp, 0, 65535));
    state.smoothVtempAdu = quint16(
        boundedInteger(MonitorSmoothVtempAdu, 0, 65535));
    state.nucDecision = quint8(
        boundedInteger(MonitorNucDecision, 0, 255));
    state.nucCount = quint16(boundedInteger(MonitorNucCount, 0, 65535));
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
    state.ntcModelReported =
        packet.values.at(MonitorNtcModelReported) >= 0.0;
    state.ntcModelValid =
        packet.values.at(MonitorNtcModelValid) >= 0.5;
    state.ntcModelReferenceAdu = quint16(
        boundedInteger(MonitorNtcModelReferenceAdu, 0, 65535));
    state.ntcModelKQ8 = qint16(
        boundedInteger(MonitorNtcModelKQ8, -32768, 32767));
    state.ntcModelLimitAdu = quint16(
        boundedInteger(MonitorNtcModelLimitAdu, 0, 65535));
    state.startupModelValid =
        packet.values.at(MonitorStartupModelValid) >= 0.5;
    state.startupModelActive =
        packet.values.at(MonitorStartupModelActive) >= 0.5;
    state.startupModelNtcThresholdAdu = quint16(
        boundedInteger(MonitorStartupModelNtcThresholdAdu, 0, 65535));
    state.startupModelAmplitudeAdu = qint16(
        boundedInteger(MonitorStartupModelAmplitudeAdu, -32768, 32767));
    state.startupModelTauDs = quint16(
        boundedInteger(MonitorStartupModelTauDs, 0, 65535));
    state.ffcState = quint8(boundedInteger(MonitorFfcState, 0, 255));
    state.rowRefMeanAdu = quint16(
        boundedInteger(MonitorRowRefMeanAdu, 0, 65535));
    state.rowRefSpanAdu = quint16(
        boundedInteger(MonitorRowRefSpanAdu, 0, 65535));
    state.shutterMeanAdu = quint16(
        boundedInteger(MonitorShutterMeanAdu, 0, 65535));
    state.rowRefModelReported =
        packet.values.at(MonitorRowRefModelReported) >= 0.0;
    state.rowRefModelValid =
        packet.values.at(MonitorRowRefModelValid) >= 0.5;
    state.rowRefModelActive =
        packet.values.at(MonitorRowRefModelActive) >= 0.5;
    state.rowRefSampleValid =
        packet.values.at(MonitorRowRefSampleValid) >= 0.5;
    state.rowRefModelReferenceAdu = quint16(
        boundedInteger(MonitorRowRefModelReferenceAdu, 0, 65535));
    state.rowRefModelKQ8 = qint16(
        boundedInteger(MonitorRowRefModelKQ8, -32768, 32767));
    state.rowRefModelLimitAdu = quint16(
        boundedInteger(MonitorRowRefModelLimitAdu, 0, 65535));
    state.rowRefCompAdu = qint16(
        boundedInteger(MonitorRowRefCompAdu, -32768, 32767));
    state.valid = validProductionMonitorTelemetry(
        state.timestampUs, state.ntcAdu, state.vtempAdu, state.meanDiff);
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
    updateNtcCoverage(state);
    m_recentTelemetry.append(state);
    while (m_recentTelemetry.size() > 5) m_recentTelemetry.removeFirst();
    m_telemetryHistory.append(state);
    while (m_telemetryHistory.size() > 2048) m_telemetryHistory.removeFirst();
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
    if (m_monitorReadyAnnounced
        && m_finalizationStage == FinalizationStage::ValidatingProduct) {
        consumePostWriteValidation(state);
        return;
    }
    if (m_monitorReadyAnnounced
        && m_finalizationStage == FinalizationStage::AwaitingProductMonitor) {
        confirmMonitorConnection();
    }
}

void ProductionCalibrationPage::updateNtcCoverage(const TelemetryState &state)
{
    if (!state.valid || !std::isfinite(state.ntcC)) return;
    if (!m_connectionReference.valid) {
        m_connectionReference = state;
        m_ntcMinimumC = state.ntcC;
        m_ntcMaximumC = state.ntcC;
        m_ntcMinimumAdu = state.ntcAdu;
        m_ntcMaximumAdu = state.ntcAdu;
    } else {
        m_ntcMinimumC = std::min(m_ntcMinimumC, state.ntcC);
        m_ntcMaximumC = std::max(m_ntcMaximumC, state.ntcC);
        m_ntcMinimumAdu = std::min(m_ntcMinimumAdu, state.ntcAdu);
        m_ntcMaximumAdu = std::max(m_ntcMaximumAdu, state.ntcAdu);
    }
}

void ProductionCalibrationPage::startContinuousRecording()
{
    if (m_sessionPath.isEmpty()
        || (m_continuousAdcFile && m_continuousAdcFile->isOpen())) {
        return;
    }
    const QDir session(m_sessionPath);
    m_continuousAdcFile = std::make_unique<QFile>(
        session.filePath(QStringLiteral("continuous_adc16.bin")));
    m_continuousIndexFile = std::make_unique<QFile>(
        session.filePath(QStringLiteral("continuous_index.csv")));
    const bool adcOpen = m_continuousAdcFile->open(QIODevice::WriteOnly
                                                   | QIODevice::Append);
    const bool indexOpen = m_continuousIndexFile->open(QIODevice::WriteOnly
                                                       | QIODevice::Append);
    if (!adcOpen || !indexOpen) {
        const QString error = !adcOpen ? m_continuousAdcFile->errorString()
                                       : m_continuousIndexFile->errorString();
        stopContinuousRecording();
        log(tr("连续ADC16记录启动失败：%1").arg(error),
            QStringLiteral("error"));
        return;
    }
    if (m_continuousIndexFile->size() == 0) {
        m_continuousIndexFile->write(
            "record_index,byte_offset,host_timestamp_us,source_frame_number,"
            "device_timestamp_ms,calibration_id,ffc_count,pair_valid,"
            "pair_delta_ms,telemetry_frame_number,ntc_c,ntc_adu,vtemp_adu,"
            "smooth_vtemp_adu,nuc_count,cumulative_nuc_dg,anchor,"
            "effective_anchor,mean_diff\n");
    }
    constexpr qint64 ContinuousRecordBytes = 160 * 120 * 2 + 120;
    m_continuousRecordCount = quint64(
        m_continuousAdcFile->size() / ContinuousRecordBytes);
    m_transport.setContinuousAdc16Enabled(true, ContinuousAdc16IntervalMs);
    emit continuousRecordingChanged(true, m_continuousAdcFile->fileName());
    log(tr("连续记录已启动：遥测约1 Hz，ADC16约2 Hz"));
}

void ProductionCalibrationPage::stopContinuousRecording()
{
    m_transport.setContinuousAdc16Enabled(false);
    const bool wasActive = (m_continuousAdcFile
                            && m_continuousAdcFile->isOpen())
        || (m_continuousIndexFile && m_continuousIndexFile->isOpen());
    if (m_continuousAdcFile) {
        m_continuousAdcFile->flush();
        m_continuousAdcFile->close();
    }
    if (m_continuousIndexFile) {
        m_continuousIndexFile->flush();
        m_continuousIndexFile->close();
    }
    m_continuousAdcFile.reset();
    m_continuousIndexFile.reset();
    if (wasActive) emit continuousRecordingChanged(false, m_sessionPath);
}

void ProductionCalibrationPage::handleContinuousAdc16Record(
    const qint64 hostTimestampUs, const QByteArray &record,
    const quint32 sourceFrameNumber, const quint32 deviceTimestampMs,
    const quint32 calibrationId, const quint16 ffcCount)
{
    if (!m_continuousAdcFile || !m_continuousIndexFile
        || !m_continuousAdcFile->isOpen()
        || !m_continuousIndexFile->isOpen()) {
        return;
    }

    const TelemetryState *nearest = nullptr;
    qint64 nearestDeltaMs = std::numeric_limits<qint64>::max();
    for (const TelemetryState &state : m_telemetryHistory) {
        if (!state.valid || state.deviceTimestampMs == 0) continue;
        const qint64 delta = std::abs(qint64(deviceTimestampMs)
                                      - qint64(state.deviceTimestampMs));
        if (delta < nearestDeltaMs) {
            nearest = &state;
            nearestDeltaMs = delta;
        }
    }
    const bool pairValid = nearest
        && nearestDeltaMs <= TelemetryPairMaxDeltaMs
        && nearest->calibrationId == calibrationId
        && nearest->nucCount == ffcCount;
    const bool collectColdStart = !m_low.complete && !m_high.complete
        && !m_calibration.valid
        && m_finalizationStage == FinalizationStage::None;
    if (collectColdStart && m_coldStartRecords.isEmpty()
        && deviceTimestampMs > ColdStartLatestFirstSampleMs
        && !m_coldStartWindowMissed) {
        m_coldStartWindowMissed = true;
        m_workflowState = QStringLiteral("ready-a1-default-comp");
        setStatus(QStringLiteral("connected"),
                  tr("未记录冷启动 · 可直接标定"));
        log(tr("连接时设备已运行 %1 秒；跳过逐机冷启动模型，基础标定将使用固件默认补偿")
                .arg(deviceTimestampMs / 1000u),
            QStringLiteral("warning"));
        if (m_ntcStatus) {
            m_ntcStatus->setText(
                tr("冷启动记录：未记录起点 · 使用固件默认补偿"));
        }
        updateControls();
    }
    if (collectColdStart && deviceTimestampMs <= ColdStartRecordEndMs) {
        const bool completedBefore = coldStartCaptureReady();
        if (pairValid && record.size() == ImageBytes + 120
            && m_coldStartRecords.size() < MaximumColdStartRecords) {
            m_coldStartRecords.append({record, *nearest,
                                       deviceTimestampMs});
        } else if (!pairValid) {
            ++m_coldStartPairRejected;
        }
        if (m_ntcStatus) {
            m_ntcStatus->setText(
                tr("冷启动记录：%1/60 秒 · 有效 %2")
                    .arg(std::min(deviceTimestampMs,
                                  quint32(ColdStartCaptureEndMs)) / 1000u)
                    .arg(m_coldStartRecords.size()));
        }
        if (!completedBefore && coldStartCaptureReady()) {
            log(tr("冷启动 0～60 秒 RAW 记录完成，可进行 0°C/50°C 标定"));
            setStatus(QStringLiteral("connected"),
                      tr("冷启动记录完成 · 可开始标定"));
            m_workflowState = QStringLiteral("ready-a1");
            updateControls();
        }
    }
    const qint64 offset = m_continuousAdcFile->size();
    if (m_continuousAdcFile->write(record) != record.size()) {
        log(tr("连续ADC16写入不完整，记录已停止"), QStringLiteral("error"));
        stopContinuousRecording();
        return;
    }

    QByteArray row;
    row += QByteArray::number(m_continuousRecordCount++) + ',';
    row += QByteArray::number(offset) + ',';
    row += QByteArray::number(hostTimestampUs) + ',';
    row += QByteArray::number(sourceFrameNumber) + ',';
    row += QByteArray::number(deviceTimestampMs) + ',';
    row += QByteArray::number(calibrationId) + ',';
    row += QByteArray::number(ffcCount) + ',';
    row += pairValid ? "1," : "0,";
    if (nearest) row += QByteArray::number(nearestDeltaMs);
    row += ',';
    if (nearest) {
        row += QByteArray::number(nearest->frameNumber) + ',';
        row += QByteArray::number(nearest->ntcC, 'f', 9) + ',';
        row += QByteArray::number(nearest->ntcAdu) + ',';
        row += QByteArray::number(nearest->vtempAdu) + ',';
        row += QByteArray::number(nearest->smoothVtempAdu) + ',';
        row += QByteArray::number(nearest->nucCount) + ',';
        row += QByteArray::number(nearest->cumulativeNucDg) + ',';
        row += QByteArray::number(nearest->anchor) + ',';
        row += QByteArray::number(nearest->effectiveAnchor) + ',';
        row += QByteArray::number(nearest->meanDiff, 'f', 6);
    } else {
        row += ",,,,,,,,,";
    }
    row += '\n';
    if (m_continuousIndexFile->write(row) != row.size()) {
        log(tr("连续ADC16索引写入不完整，记录已停止"),
            QStringLiteral("error"));
        stopContinuousRecording();
    }
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
    result.frameNumber = quint32(std::llround(median(
        [](const TelemetryState &value) { return value.frameNumber; })));
    result.deviceTimestampMs = quint32(std::llround(median(
        [](const TelemetryState &value) { return value.deviceTimestampMs; })));
    result.calibrationId = quint32(std::llround(median(
        [](const TelemetryState &value) { return value.calibrationId; })));
    result.centerRoiC = median(
        [](const TelemetryState &value) { return value.centerRoiC; });
    result.centerRoiAdu = quint16(std::llround(median(
        [](const TelemetryState &value) { return value.centerRoiAdu; })));
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
    result.ffcState = quint8(std::llround(median(
        [](const TelemetryState &value) { return value.ffcState; })));
    result.rowRefMeanAdu = quint16(std::llround(median(
        [](const TelemetryState &value) { return value.rowRefMeanAdu; })));
    result.rowRefSpanAdu = quint16(std::llround(median(
        [](const TelemetryState &value) { return value.rowRefSpanAdu; })));
    result.shutterMeanAdu = quint16(std::llround(median(
        [](const TelemetryState &value) { return value.shutterMeanAdu; })));
    result.rowRefModelReported = median(
        [](const TelemetryState &value) { return value.rowRefModelReported; })
        >= 0.5;
    result.rowRefModelValid = median(
        [](const TelemetryState &value) { return value.rowRefModelValid; })
        >= 0.5;
    result.rowRefModelActive = median(
        [](const TelemetryState &value) { return value.rowRefModelActive; })
        >= 0.5;
    result.rowRefSampleValid = median(
        [](const TelemetryState &value) { return value.rowRefSampleValid; })
        >= 0.5;
    result.rowRefModelReferenceAdu = quint16(std::llround(median(
        [](const TelemetryState &value) {
            return value.rowRefModelReferenceAdu;
        })));
    result.rowRefModelKQ8 = qint16(std::llround(median(
        [](const TelemetryState &value) { return value.rowRefModelKQ8; })));
    result.rowRefModelLimitAdu = quint16(std::llround(median(
        [](const TelemetryState &value) { return value.rowRefModelLimitAdu; })));
    result.rowRefCompAdu = qint16(std::llround(median(
        [](const TelemetryState &value) { return value.rowRefCompAdu; })));
    result.valid = validProductionMonitorTelemetry(
        result.timestampUs, result.ntcAdu, result.vtempAdu, result.meanDiff);
    return result;
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
    const CaptureKind completedKind = m_captureKind;
    CaptureData *existing = captureFor(m_captureKind);
    const bool keepExisting = !finalSuccess && existing && !existing->frames.isEmpty();
    QList<QPair<QString, CaptureData>> capturesToPreserve;
    if (keepExisting) {
        capturesToPreserve = failedCaptures();
    } else {
        storeCurrentCapture();
    }

    if (finalSuccess) {
        invalidateDownstreamAfterCapture(completedKind);
    }

    if (finalSuccess) {
        updateWorkflowAfterCapture(completedKind);
    }

    m_operation = Operation::Idle;
    m_captureKind = CaptureKind::None;
    m_currentCapture = {};
    setStatus(QStringLiteral("connecting"), tr("保存生产记录"));
    updatePreviews();
    savePackageAsync(
        m_sessionPath, std::move(capturesToPreserve),
        [this, completedKind, finalSuccess, finalMessage](
            const bool saved, const QString &saveError) mutable {
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
            if (m_disconnectCleanupPending) {
                const QString disconnectedSerial =
                    m_disconnectCleanupSerial;
                const bool notifyDisconnect = m_disconnectWasReady;
                m_disconnectCleanupPending = false;
                m_disconnectWasReady = false;
                m_disconnectCleanupSerial.clear();
                resetCalibrationRun();
                m_expectedSerial.clear();
                m_deviceSerial.clear();
                setStatus(QStringLiteral("disconnected"), tr("设备已断开"));
                m_workflowState = QStringLiteral("disconnected");
                emitWorkflowState();
                updateControls();
                if (notifyDisconnect) {
                    emit deviceDisconnected(this, disconnectedSerial,
                                            m_singleDeviceMode
                                                ? ProductSegment
                                                : m_stationSegment);
                }
                return;
            }
            if (!finalSuccess) {
                updateControls();
                return;
            }
            if (completedKind == CaptureKind::High) {
                setStatus(QStringLiteral("connecting"),
                          tr("正在自动计算标定参数"));
                QTimer::singleShot(
                    0, this, &ProductionCalibrationPage::computeTwoPoint);
            }
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
    return nullptr;
}

const ProductionCalibrationPage::CaptureData *
ProductionCalibrationPage::captureFor(const CaptureKind kind) const
{
    if (kind == CaptureKind::Low) return &m_low;
    if (kind == CaptureKind::High) return &m_high;
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
    const TelemetryState reference = m_recentTelemetry.isEmpty()
        ? TelemetryState{} : m_recentTelemetry.constLast();
    if (!reference.valid) {
        failOperation(tr("自动计算前缺少有效监控遥测"));
        return;
    }
    QString error;
    if (!calculateCalibration(&error)) {
        failOperation(error);
        return;
    }
    m_calibration.coldStartModelAttempted = false;
    m_calibration.coldStartModel = {};
    m_calibration.rowRefModel = EnableRowRefProduction
        ? makeRowRefModel(
              m_high.before.rowRefMeanAdu, m_high.before.rowRefSpanAdu,
              m_high.after.rowRefMeanAdu, m_high.after.rowRefSpanAdu)
        : RowRefModelResult{};
    const bool rowRefModelValid = m_calibration.rowRefModel.valid;
    prepareWritePacket();
    if (!ensureSessionDirectory(&error)) {
        failOperation(tr("标定已计算，但创建保存目录失败：%1").arg(error));
        return;
    }
    const float meanDiff = m_calibration.meanDiff;
    const int badPointCount = m_calibration.badPointCount;
    setStatus(QStringLiteral("connecting"), tr("保存自动计算结果"));
    updatePreviews();
    savePackageAsync(
        m_sessionPath, {},
        [this, reference, meanDiff, badPointCount, rowRefModelValid](
            const bool saved, const QString &saveError) {
            if (!saved) {
                setStatus(QStringLiteral("error"), tr("标定保存失败"));
                log(tr("标定已计算，但保存失败：%1").arg(saveError),
                    QStringLiteral("error"));
                return;
            }
            m_monitorContinuityValid = true;
            m_lastMonitorNucCount = reference.nucCount;
            m_lastGateUiSecond = -1;
            log(tr("基础两点标定完成：mean_diff=%1 ADU，坏点=%2，模式=%3")
                    .arg(meanDiff, 0, 'f', 3)
                    .arg(badPointCount)
                    .arg(!EnableRowRefProduction
                             ? tr("通用基础V1")
                             : rowRefModelValid
                             ? tr("已自动生成，reference=%1")
                                   .arg(m_calibration.rowRefModel.referenceAdu)
                             : tr("50°C行参考无效，仅可写入基础V1测试件")));
            m_ntcStatus->setText(
                !EnableRowRefProduction
                    ? tr("温漂模式：通用补偿")
                    : rowRefModelValid
                    ? tr("RR参数：reference %1 · K_Q8 256 · limit 600")
                          .arg(m_calibration.rowRefModel.referenceAdu)
                    : tr("RR参数：50°C行参考无效"));
            setStatus(QStringLiteral("connected"),
                      !EnableRowRefProduction
                          ? tr("通用基础V1已计算 · 可写入")
                      : rowRefModelValid
                          ? tr("标定与RR温漂参数已计算 · 可写入")
                          : tr("RR参考无效 · 仅可写入基础V1测试件"));
            m_workflowState = !EnableRowRefProduction
                ? QStringLiteral("base-v1-ready")
                : rowRefModelValid
                ? QStringLiteral("row-ref-model-ready")
                : QStringLiteral("base-v1-ready");
            updatePreviews();
            updateControls();
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
    if (!std::isfinite(meanDiff) || meanDiff < MinimumMeanDiffAdu) {
        if (error) {
            *error = tr("0°C/50°C 响应仅 %1 ADU，未形成有效两点温差；"
                        "已保留0°C数据，请重新采集50°C")
                         .arg(meanDiff, 0, 'f', 3);
        }
        return false;
    }
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

bool ProductionCalibrationPage::requiredAnchorFromAdc16(
    const QByteArray &record, double *requiredAnchor) const
{
    if (!requiredAnchor || record.size() != ImageBytes + 120
        || !m_calibration.valid
        || m_calibration.background.size() != ImageBytes
        || m_calibration.gain.size() != GainBytes
        || !std::isfinite(m_calibration.meanDiff)
        || m_calibration.meanDiff <= 0.0f) {
        return false;
    }
    double backgroundSum = 0.0;
    double correctedDeltaSum = 0.0;
    for (int pixel = 0; pixel < PixelCount; ++pixel) {
        const quint16 raw = readLe16(record, pixel * 2);
        const quint16 background = readLe16(m_calibration.background,
                                             pixel * 2);
        const float gain = readLeFloat(m_calibration.gain, pixel * 4);
        if (raw > 0x3fffu || background > 0x3fffu
            || !std::isfinite(gain) || gain < 0.2f || gain > 4.0f) {
            return false;
        }
        backgroundSum += background;
        correctedDeltaSum += gain * (double(raw) - background);
    }
    const double correctedMean =
        (backgroundSum + correctedDeltaSum) / PixelCount;
    *requiredAnchor = correctedMean - m_calibration.meanDiff;
    return std::isfinite(*requiredAnchor);
}

bool ProductionCalibrationPage::coldStartCaptureReady() const noexcept
{
    if (m_coldStartRecords.size()
            < qsizetype(MinimumColdStartSamples)) {
        return false;
    }
    const quint32 firstMs =
        m_coldStartRecords.constFirst().deviceTimestampMs;
    const quint32 lastMs =
        m_coldStartRecords.constLast().deviceTimestampMs;
    return firstMs <= ColdStartLatestFirstSampleMs
           && lastMs >= ColdStartCaptureEndMs - 2000u;
}

bool ProductionCalibrationPage::buildColdStartSamples()
{
    m_coldStartSamples.clear();
    if (!m_calibration.valid || m_coldStartRecords.isEmpty()) {
        m_calibration.coldStartModel = {};
        return false;
    }
    quint16 bootNtcAdu = 0;
    for (const ColdStartRawRecord &entry : std::as_const(m_coldStartRecords)) {
        if (entry.deviceTimestampMs > ColdStartRecordEndMs) continue;
        double requiredAnchor = 0.0;
        if (!requiredAnchorFromAdc16(entry.record, &requiredAnchor)) continue;
        m_coldStartSamples.append(
            {entry.deviceTimestampMs, requiredAnchor});
        if (bootNtcAdu == 0 && entry.telemetry.bootNtcAdu > 8u
            && entry.telemetry.bootNtcAdu < 4088u) {
            bootNtcAdu = entry.telemetry.bootNtcAdu;
        }
    }
    if (bootNtcAdu == 0 && !m_coldStartRecords.isEmpty()) {
        bootNtcAdu = m_coldStartRecords.constFirst().telemetry.ntcAdu;
    }
    m_calibration.coldStartModel = fitColdStartModel(
        std::span<const ColdStartSample>(m_coldStartSamples.constData(),
                                         std::size_t(m_coldStartSamples.size())),
        bootNtcAdu);
    return m_calibration.coldStartModel.valid;
}

QString ProductionCalibrationPage::coldStartModelFailureText(
    const ColdStartModelFailure failure)
{
    switch (failure) {
    case ColdStartModelFailure::None: return tr("已生成");
    case ColdStartModelFailure::InvalidInput: return tr("启动记录或基础标定无效");
    case ColdStartModelFailure::LateFirstSample: return tr("连接过晚，未记录冷启动起点");
    case ColdStartModelFailure::InsufficientSamples: return tr("冷启动有效 RAW 数量不足");
    case ColdStartModelFailure::IncompleteWindow: return tr("冷启动记录未覆盖 60 秒");
    case ColdStartModelFailure::FitFailed: return tr("冷启动指数拟合失败");
    case ColdStartModelFailure::AmplitudeOutOfRange: return tr("启动补偿幅度超出固件范围");
    }
    return tr("未知错误");
}

bool ProductionCalibrationPage::reportedNtcModelMatches(
    const TelemetryState &state, QString *error) const
{
    const RowRefModelResult &rowRef = m_calibration.rowRefModel;
    if (rowRef.valid) {
        if (!state.rowRefModelReported || !state.rowRefModelValid
            || !state.rowRefModelActive || !state.rowRefSampleValid) {
            if (error) *error = tr("设备尚未报告有效且已激活的RR模型");
            return false;
        }
        if (state.rowRefModelReferenceAdu != rowRef.referenceAdu
            || state.rowRefModelKQ8 != rowRef.kQ8
            || state.rowRefModelLimitAdu != rowRef.limitAdu) {
            if (error) {
                *error = tr("设备RR模型回读不一致：期望 %1/%2/%3，实际 %4/%5/%6")
                    .arg(rowRef.referenceAdu).arg(rowRef.kQ8)
                    .arg(rowRef.limitAdu)
                    .arg(state.rowRefModelReferenceAdu)
                    .arg(state.rowRefModelKQ8)
                    .arg(state.rowRefModelLimitAdu);
            }
            return false;
        }
        if (state.startupModelActive || state.ntcModelValid
            || std::abs(int(state.rowRefCompAdu)) >= rowRef.limitAdu) {
            if (error) {
                *error = state.startupModelActive || state.ntcModelValid
                    ? tr("RR生效时旧温漂模型仍在叠加")
                    : tr("RR运行补偿已经达到声明限幅");
            }
            return false;
        }
        return true;
    }
    const ColdStartModelResult &startup = m_calibration.coldStartModel;
    if (startup.valid) {
        if (!state.ntcModelReported || !state.startupModelValid) {
            if (error) *error = tr("设备尚未报告写入后的冷启动模型");
            return false;
        }
        if (state.startupModelNtcThresholdAdu
                    != startup.coldNtcThresholdAdu
            || state.startupModelAmplitudeAdu != startup.amplitudeAdu
            || state.startupModelTauDs != startup.tauDs) {
            if (error) {
                *error = tr("设备冷启动模型回读不一致：期望 %1/%2/%3，"
                            "实际 %4/%5/%6")
                    .arg(startup.coldNtcThresholdAdu)
                    .arg(startup.amplitudeAdu).arg(startup.tauDs)
                    .arg(state.startupModelNtcThresholdAdu)
                    .arg(state.startupModelAmplitudeAdu)
                    .arg(state.startupModelTauDs);
            }
            return false;
        }
        return true;
    }
    const NtcAnchorModelResult &model = m_calibration.ntcModel;
    if (!state.ntcModelReported) {
        if (error) *error = tr("设备尚未报告写入后的模型状态");
        return false;
    }
    if (!model.valid) {
        if (state.ntcModelValid || state.ntcModelReferenceAdu != 0
            || state.ntcModelKQ8 != 0 || state.ntcModelLimitAdu != 0) {
            if (error) *error = tr("基础V1写入后设备仍报告非零温漂模型");
            return false;
        }
        return true;
    }
    if (!state.ntcModelValid) {
        if (error) *error = tr("设备未加载已写入的单机NTC参数");
        return false;
    }
    if (state.ntcModelReferenceAdu != model.referenceAdu
        || state.ntcModelKQ8 != model.kQ8
        || state.ntcModelLimitAdu != model.limitAdu) {
        if (error) {
            *error = tr("设备NTC模型回读不一致：期望 %1/%2/%3，实际 %4/%5/%6")
                .arg(model.referenceAdu).arg(model.kQ8).arg(model.limitAdu)
                .arg(state.ntcModelReferenceAdu).arg(state.ntcModelKQ8)
                .arg(state.ntcModelLimitAdu);
        }
        return false;
    }
    if (std::abs(qint64(state.effectiveAnchor) - qint64(state.anchor))
        > model.limitAdu) {
        if (error) *error = tr("设备运行补偿超过声明限幅");
        return false;
    }
    return true;
}

void ProductionCalibrationPage::beginPostWriteValidation(
    const TelemetryState &state)
{
    m_postWriteValidation.clear();
    m_calibration.postWriteValidationAttempted = true;
    m_calibration.postWriteValidationPassed = false;
    m_calibration.postWriteValidationSamples = 0;
    m_calibration.postWriteMeanC = std::numeric_limits<double>::quiet_NaN();
    m_calibration.postWritePeakToPeakC =
        std::numeric_limits<double>::quiet_NaN();
    m_finalizationStage = FinalizationStage::ValidatingProduct;
    m_monitorFrameTimeout.start(NetworkTransitionTimeoutMs);
    m_workflowState = QStringLiteral("validating-row-ref");
    setStatus(QStringLiteral("connecting"),
              tr("RR已加载 · 自动验证50°C 10秒"));
    log(tr("RR参数与回读一致；从设备启动6秒后自动验证50°C均值、峰峰值和限幅"));
    consumePostWriteValidation(state);
    updateControls();
}

void ProductionCalibrationPage::consumePostWriteValidation(
    const TelemetryState &state)
{
    if (m_finalizationStage != FinalizationStage::ValidatingProduct
        || state.deviceTimestampMs < PostWriteValidationEarliestMs
        || state.ffcState != 0u) {
        return;
    }
    QString modelError;
    if (!reportedNtcModelMatches(state, &modelError)) {
        finishPostWriteValidation(false, modelError);
        return;
    }
    if (!std::isfinite(state.centerRoiC)) {
        finishPostWriteValidation(false, tr("50°C验收温度无效"));
        return;
    }
    if (!m_postWriteValidation.isEmpty()
        && state.deviceTimestampMs
               < m_postWriteValidation.constLast().deviceTimestampMs) {
        finishPostWriteValidation(false, tr("10秒验收期间设备发生RST"));
        return;
    }
    m_postWriteValidation.append(state);
    const quint32 firstMs =
        m_postWriteValidation.constFirst().deviceTimestampMs;
    if (state.deviceTimestampMs - firstMs < PostWriteValidationWindowMs) {
        return;
    }

    QVector<double> temperatures;
    temperatures.reserve(m_postWriteValidation.size());
    for (const TelemetryState &sample : std::as_const(m_postWriteValidation)) {
        temperatures.append(sample.centerRoiC);
    }
    const RowRefTemperatureValidation validation = validateRowRefTemperature(
        std::span<const double>(temperatures.constData(),
                                std::size_t(temperatures.size())));
    m_calibration.postWriteValidationSamples = m_postWriteValidation.size();
    m_calibration.postWriteMeanC = validation.meanC;
    m_calibration.postWritePeakToPeakC = validation.peakToPeakC;
    const bool passed =
        m_postWriteValidation.size() >= PostWriteValidationMinimumSamples
        && validation.valid && validation.passed;
    finishPostWriteValidation(
        passed,
        passed
            ? tr("50°C验收通过：均值%1°C，峰峰值%2°C")
                  .arg(validation.meanC, 0, 'f', 2)
                  .arg(validation.peakToPeakC, 0, 'f', 2)
            : tr("50°C验收失败：有效样本%1，均值%2°C，峰峰值%3°C；要求至少10个、49～51°C且≤1.5°C")
                  .arg(m_postWriteValidation.size())
                  .arg(validation.meanC, 0, 'f', 2)
                  .arg(validation.peakToPeakC, 0, 'f', 2));
}

void ProductionCalibrationPage::finishPostWriteValidation(
    bool passed, const QString &message)
{
    if (m_finalizationStage != FinalizationStage::ValidatingProduct) return;
    m_monitorFrameTimeout.stop();
    m_finalizationStage = FinalizationStage::SavingValidation;
    m_calibration.postWriteValidationPassed = passed;
    setStatus(QStringLiteral("connecting"), tr("保存RR验收结果"));
    log(message, passed ? QStringLiteral("info") : QStringLiteral("error"));
    savePackageAsync(
        m_sessionPath, {},
        [this, passed, resultMessage = QString(message)](
            const bool saved, const QString &saveError) mutable {
            if (m_finalizationStage != FinalizationStage::SavingValidation) {
                return;
            }
            if (!saved) {
                passed = false;
                resultMessage +=
                    tr("；保存验收记录失败：%1").arg(saveError);
            }
            m_calibration.postWriteValidationPassed = passed;
            if (!passed) {
                m_writeAwaitingVerification = false;
                m_workflowState = QStringLiteral("validation-failed");
                m_finalizationStage = m_singleDeviceMode
                    ? FinalizationStage::None
                    : FinalizationStage::AwaitingRemoval;
                setStatus(QStringLiteral("error"),
                          m_singleDeviceMode
                              ? tr("RR温漂验收失败")
                              : tr("RR温漂验收失败 · 请拔出"));
                log(resultMessage, QStringLiteral("error"));
                emit finalizationFailed(this, resultMessage);
                updateControls();
                emitWorkflowState();
                return;
            }

            m_writeAwaitingVerification = false;
            const bool productionPassed = !m_calibration.testWrite
                && m_low.complete && m_high.complete
                && m_calibration.rowRefModel.valid
                && m_calibration.writeVerified;
            m_workflowState = productionPassed
                ? QStringLiteral("completed")
                : QStringLiteral("test-completed");
            if (m_singleDeviceMode) {
                m_finalizationStage = FinalizationStage::None;
                setStatus(QStringLiteral("connected"),
                          productionPassed
                              ? tr("RR标定、回读与50°C验收通过")
                              : tr("RR测试件回读与50°C验收通过"));
                emit calibrationCompleted(m_deviceSerial, productionPassed,
                                          m_sessionPath);
            } else {
                m_finalizationStage = FinalizationStage::AwaitingRemoval;
                setStatus(QStringLiteral("connected"),
                          productionPassed
                              ? tr("标定与RR验收完成 · 请拔出")
                              : tr("RR测试件非量产PASS · 请拔出"));
                if (productionPassed) {
                    emit finalProductReadyForRemoval(this, m_deviceSerial);
                } else {
                    emit finalizationFailed(
                        this, tr("RR测试件不具备本工位完整0°C/50°C证据"));
                }
            }
            log(productionPassed
                    ? tr("PASS：RR模型、同serial全量回读和10秒50°C验收均通过")
                    : tr("测试通过：RR回读和10秒50°C验收通过；不标记量产PASS"));
            updateControls();
            emitWorkflowState();
        });
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
    if (!EnableRowRefProduction) {
        header.append(QByteArray(8, char(0)));
    } else if (m_calibration.rowRefModel.valid) {
        appendLe16(header, RowRefModelTag);
        appendLe16(header, m_calibration.rowRefModel.referenceAdu);
        appendLe16(header, quint16(m_calibration.rowRefModel.kQ8));
        appendLe16(header, m_calibration.rowRefModel.limitAdu);
    } else if (m_calibration.coldStartModel.valid) {
        appendLe16(header, ColdStartModelTag);
        appendLe16(header,
                   m_calibration.coldStartModel.coldNtcThresholdAdu);
        appendLe16(header,
                   quint16(m_calibration.coldStartModel.amplitudeAdu));
        appendLe16(header, m_calibration.coldStartModel.tauDs);
    } else if (m_calibration.ntcModel.valid) {
        appendLe16(header, NtcModelTag);
        appendLe16(header, m_calibration.ntcModel.referenceAdu);
        appendLe16(header, quint16(m_calibration.ntcModel.kQ8));
        appendLe16(header, m_calibration.ntcModel.limitAdu);
    } else {
        header.append(QByteArray(8, char(0)));
    }
    if (header.size() != WriteHeaderBytes - 4) {
        m_writeHeader.clear();
        m_calibration.packet.clear();
        return;
    }
    m_writeHeaderCrc = crc32(header);
    appendLe32(header, m_writeHeaderCrc);
    m_writeHeader = header;
    m_calibration.packet = header + payload;

    /* The device chooses the inactive Flash slot, so its FlashDataInfo CRC
     * cannot be predicted on the host.  Preserve the CRC returned by the
     * verified commit response instead. */
    m_writeFlashHeaderCrc = 0;
}

void ProductionCalibrationPage::writeAndVerify()
{
    beginWriteAndVerify(false);
}

void ProductionCalibrationPage::writeTestAndVerify()
{
    beginWriteAndVerify(true);
}

void ProductionCalibrationPage::retryWriteVerification()
{
    const ProductionWorkflowActions actions = actionAvailability();
    if (!actions.retryVerification) return;
    if (!m_transport.isActive()) {
        m_finalizationStage = FinalizationStage::ReconnectingProduct;
        setPort(QStringLiteral("http://192.168.17.1"));
        setStatus(QStringLiteral("connecting"), tr("重新连接设备并回读"));
        m_workflowState = QStringLiteral("retrying-verification");
        m_transport.openWhenAvailable(QStringLiteral("http://192.168.17.1"),
                                       NetworkTransitionTimeoutMs);
        updateControls();
        return;
    }
    m_finalizationStage = FinalizationStage::ReconnectingProduct;
    m_transport.close();
    QTimer::singleShot(0, this, [this] {
        if (!m_shuttingDown
            && m_finalizationStage == FinalizationStage::ReconnectingProduct) {
            m_transport.openWhenAvailable(QStringLiteral("http://192.168.17.1"),
                                          NetworkTransitionTimeoutMs);
        }
    });
    setStatus(QStringLiteral("connecting"), tr("重新连接设备并回读"));
    m_workflowState = QStringLiteral("retrying-verification");
    updateControls();
}

void ProductionCalibrationPage::beginWriteAndVerify(const bool testWrite)
{
    if (operationActive()) return;
    const ProductionWorkflowActions actions = actionAvailability();
    if (testWrite && !actions.writeTest) return;
    if (!testWrite && !actions.writeProduction) return;
    if (!m_singleDeviceMode && m_stationSegment == ProductSegment) {
        failOperation(tr("请先点击“重新标定”切换到临时工位"));
        return;
    }
    if (!m_transport.isActive()) {
        failOperation(tr("请先连接 TN160"));
        return;
    }
    if (!m_calibration.valid) {
        failOperation(tr("没有可写入的标定数据"));
        return;
    }
    m_calibration.testWrite = testWrite;
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
    /* computeTwoPoint() already persists the complete package before it marks
     * the result writable.  Do not write the same large raw capture set again
     * on the operator's click; the verified post-reboot archive remains. */
    beginWriteUpload();
}

void ProductionCalibrationPage::beginWriteUpload()
{
    if (m_shuttingDown) return;
    if (!m_transport.isActive()) {
        failOperation(tr("写入前 USB NCM 已断开"));
        return;
    }
    m_calibration.writeVerified = false;
    m_operation = Operation::Writing;
    m_writeCommitAttempted = true;
    m_writeAwaitingVerification = false;
    m_portForVerify = m_endpoint;
    if (m_singleDeviceMode) {
        const bool baseV1Only =
            !EnableRowRefProduction || !m_calibration.rowRefModel.valid;
        m_finalizationStage = FinalizationStage::SwitchingToProduct;
        m_timeout.start(60000);
        m_workflowState = QStringLiteral("writing-calibration");
        setStatus(QStringLiteral("connecting"),
                  baseV1Only
                      ? tr("正在写入通用基础V1并等待重启")
                  : m_calibration.testWrite
                      ? tr("正在写入RR测试件并等待重启")
                      : tr("正在写入带RR温漂补偿的标定并等待重启"));
        log(baseV1Only
                ? tr("写入通用基础V1：模型扩展区清零")
                : m_calibration.testWrite
                    ? tr("显式写入RR测试件；固定地址保持192.168.17.1")
                    : tr("RR参数已计算；写入标定，固定地址保持192.168.17.1"));
        emitWorkflowState();
        m_transport.uploadCalibration(m_calibration.packet, false);
        updateControls();
        return;
    }
    m_finalizationStage = FinalizationStage::WaitingGrant;
    setStatus(QStringLiteral("connecting"),
              tr("等待 192.168.17.1 独占权"));
    log(tr("第 3 步完成，等待最终地址空闲后自动提交"));
    emit finalizationRequested(this);
    updateControls();
}

void ProductionCalibrationPage::completeNcmWrite(const quint32 payloadCrc,
                                                  const quint32 headerCrc,
                                                  const quint32 flashHeaderCrc)
{
    if (payloadCrc != m_writePayloadCrc || headerCrc != m_writeHeaderCrc
            || flashHeaderCrc == 0u) {
        m_finalizationStage = FinalizationStage::None;
        const QString message = tr("设备写入回复 CRC 不一致");
        failOperation(message);
        emit finalizationFailed(this, message);
        return;
    }
    m_timeout.stop();
    m_writeFlashHeaderCrc = flashHeaderCrc;
    m_writeAwaitingVerification = true;
    m_productAddressCommitted = true;
    m_operation = Operation::Idle;
    setPort(QStringLiteral("http://192.168.17.1"));
    m_finalizationStage = FinalizationStage::ReconnectingProduct;
    setStatus(QStringLiteral("connecting"),
              tr("设备重启中 · 等待 192.168.17.1"));
    log(tr("标定与最终地址已提交；等待单次重启后最终回读"));
    m_transport.close();
    QTimer::singleShot(0, this, [this] {
        if (!m_shuttingDown
            && m_finalizationStage == FinalizationStage::ReconnectingProduct) {
            m_transport.openWhenAvailable(
                QStringLiteral("http://192.168.17.1"),
                NetworkTransitionTimeoutMs);
        }
    });
}

void ProductionCalibrationPage::receiveVerifyPayload(const int index,
                                                      const QByteArray &payload)
{
    while (m_verifyPayloads.size() <= index) m_verifyPayloads.append(QByteArray{});
    m_verifyPayloads[index] = payload;
}

void ProductionCalibrationPage::receiveVerifyHeader(const QByteArray &header)
{
    m_verifyHeader = header;
}

void ProductionCalibrationPage::verifyCompleted(const bool success,
                                                 const QString &message)
{
    if (m_operation != Operation::Verifying) return;
    const VerifyPhase phase = m_verifyPhase;
    m_verifyPhase = VerifyPhase::None;
    bool verified = success && m_verifyHeader == m_writeHeader
                    && m_verifyPayloads.size() == 3;
    QString result = message;
    if (verified) {
        verified = m_verifyPayloads[0] == m_calibration.background
                   && m_verifyPayloads[1] == m_calibration.gain
                   && m_verifyPayloads[2] == m_calibration.badPoints;
        if (!verified) result = tr("HTTP 回读内容与写入包不一致");
    }
    m_calibration.writeVerified = verified;
    if (verified) m_writeAwaitingVerification = false;
    m_operation = Operation::Idle;
    setStatus(QStringLiteral("connecting"), tr("保存回读验证记录"));
    updatePreviews();
    savePackageAsync(
        m_sessionPath, {},
        [this, phase, verified, result](const bool saved,
                                 const QString &saveError) mutable {
            if (!saved) {
                verified = false;
                result += tr("；保存验证记录失败：%1").arg(saveError);
            }
            if (verified && phase == VerifyPhase::TemporaryAddress) {
                m_finalizationStage = FinalizationStage::WaitingGrant;
                setStatus(QStringLiteral("connecting"),
                          tr("等待设置最终地址"));
                log(tr("临时地址回读通过，等待切换最终地址"));
                emit finalizationRequested(this);
                updatePreviews();
                updateControls();
                return;
            }
            if (verified && phase == VerifyPhase::ProductAddress) {
                /* The write transaction intentionally rebooted the device.
                 * Start a new NUC/telemetry epoch before accepting post-write
                 * diagnostics; a lower count here is expected, not a fault. */
                m_lastMonitorNucCount = -1;
                m_cumulativeNucDg = 0;
                m_monitorContinuityValid = true;
                m_recentTelemetry.clear();
                m_telemetryHistory.clear();
                m_lastTraceTimestampUs = 0;
                m_finalizationStage =
                    FinalizationStage::AwaitingProductMonitor;
                setStatus(QStringLiteral("connecting"),
                          tr("最终回读通过 · 确认设备在线"));
                log(tr("最终地址回读一致，正在完成最后一次设备核对"));
                m_transport.openWhenAvailable(
                    QStringLiteral("http://192.168.17.1"),
                    NetworkTransitionTimeoutMs);
                updatePreviews();
                updateControls();
                return;
            }
            m_finalizationStage = FinalizationStage::None;
            setStatus(QStringLiteral("error"), tr("回读不一致"));
            log(verified
                    ? tr("PASS：设备内部校验 + HTTP header/三段 payload 逐字节一致")
                    : tr("FAIL：%1；原始帧和标定包均已保留").arg(result),
                verified ? QStringLiteral("info") : QStringLiteral("error"));
            if (!verified) emit finalizationFailed(this, result);
            updatePreviews();
        });
}

void ProductionCalibrationPage::handleTransportError(const QString &message)
{
    if (m_waitingCloseForVerify) return;
    if (m_finalizationStage == FinalizationStage::AwaitingRemoval) return;
    if ((m_finalizationStage == FinalizationStage::AwaitingProductMonitor
         || m_finalizationStage == FinalizationStage::ValidatingProduct)
        && m_productAddressCommitted) {
        if (m_singleDeviceMode) {
            m_monitorFrameTimeout.stop();
            m_finalizationStage = FinalizationStage::None;
            m_calibration.postWriteValidationPassed = false;
            m_workflowState = QStringLiteral("verification-failed");
            setStatus(QStringLiteral("error"), tr("最终首帧异常 · 可重新回读"));
            log(tr("最终地址首帧确认失败：%1；请重新连接并回读")
                    .arg(message),
                QStringLiteral("error"));
            updateControls();
            emit finalizationFailed(this, message);
            return;
        }
        // The final .17 address is already committed.  Preserve the same
        // removal gate used by the monitor-frame watchdog so an early HTTP
        // or transport error cannot leave the Hub's finalization queue locked.
        m_monitorFrameTimeout.stop();
        m_finalizationStage = FinalizationStage::AwaitingRemoval;
        m_calibration.postWriteValidationPassed = false;
        setStatus(QStringLiteral("error"), tr("最终首帧异常 · 请拔出"));
        log(tr("最终地址首帧确认失败：%1；请拔出设备后继续")
                .arg(message),
            QStringLiteral("error"));
        if (m_host) {
            m_host->setStatus(
                tr("TN160 · 最终首帧异常：请拔出设备后继续"));
        }
        updateControls();
        emit finalizationFailed(this, message);
        return;
    }
    const bool finalizing =
        m_finalizationStage == FinalizationStage::ReconnectingTemporary
        || m_finalizationStage == FinalizationStage::SwitchingToProduct
        || m_finalizationStage == FinalizationStage::ReconnectingProduct
        || m_finalizationStage == FinalizationStage::VerifyingProduct
        || m_finalizationStage == FinalizationStage::AwaitingProductMonitor
        || m_finalizationStage == FinalizationStage::ValidatingProduct
        || m_finalizationStage == FinalizationStage::SavingValidation;
    if (m_operation == Operation::Idle && !m_transport.isActive()
        && !finalizing) {
        log(message, QStringLiteral("error"));
        return;
    }
    if (m_operation == Operation::Capturing) {
        if (!m_transport.isActive() && !m_disconnectCleanupPending) {
            m_disconnectCleanupPending = true;
            m_disconnectWasReady = m_monitorReadyAnnounced;
            m_disconnectCleanupSerial = m_deviceSerial;
            m_expectedSerial.clear();
            m_deviceSerial.clear();
            stopContinuousRecording();
        }
        if (m_captureRestoring) {
            finishCapture(false, message);
        } else {
            stopCapture(false, message);
        }
        return;
    }
    failOperation(message);
    if (finalizing) {
        m_finalizationStage = FinalizationStage::None;
        emit finalizationFailed(this, message);
        updateControls();
    }
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
                log(tr("%1原始流暂停，USB NCM 保持连接；按 FFC 空窗等待恢复")
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
        failOperation(tr("等待 NCM V1 写入完成超时；标定包已保留"));
    }
}

void ProductionCalibrationPage::failOperation(const QString &message)
{
    if (m_operation == Operation::Capturing && !m_captureRestoring) {
        stopCapture(false, message);
        return;
    }
    if (m_operation == Operation::Writing && m_writeCommitAttempted) {
        m_writeAwaitingVerification = true;
        m_workflowState = QStringLiteral("write-uncertain");
    }
    m_timeout.stop();
    m_captureGapActive = false;
    m_captureSilence.invalidate();
    if (m_operation == Operation::Verifying) m_verifyTransfer.cancel();
    m_operation = Operation::Idle;
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
        this, tr("导入标定包"), QString(),
        tr("TN160 V1 标定包 (calibration_v1.bin);;二进制文件 (*.bin)"));
    if (fileName.isEmpty()) return;
    QString error;
    if (!loadPackage(fileName, &error)) {
        failOperation(error);
        return;
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
        log(tr("标定包已导入；可作为测试件写入，量产件需重新完成0°C/50°C采集"));
        setStatus(QStringLiteral("connected"), tr("标定包已导入"));
    });
}

void ProductionCalibrationPage::exportCalibration()
{
    if (operationActive() || !m_calibration.valid) return;
    const QString parent = QFileDialog::getExistingDirectory(
        this, tr("选择导出目录"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (parent.isEmpty()) return;
    const QString folder =
        QStringLiteral("TN160_%1").arg(QDateTime::currentDateTime().toString(
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
    const detail::NtcModelHeader modelHeader =
        detail::parseNtcModelHeader(header);
    if (modelHeader.status == detail::NtcModelHeaderStatus::Invalid) {
        if (error) *error = tr("标定包温漂模型字段无效");
        return false;
    }
    const float meanDiff = readLeFloat(header, 28);
    const float tRefLow = readLeFloat(header, 32);
    if (!std::isfinite(meanDiff) || meanDiff < MinimumMeanDiffAdu
        || meanDiff > 16383.0f
        || !std::isfinite(tRefLow) || tRefLow <= -273.15f
        || tRefLow >= ReferenceHighC) {
        if (error) {
            *error = tr("标定包温度或 mean_diff 语义错误（mean_diff 必须至少为 %1 ADU）")
                         .arg(MinimumMeanDiffAdu, 0, 'f', 0);
        }
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
    if (modelHeader.status == detail::NtcModelHeaderStatus::RowReference) {
        m_calibration.rowRefModel.valid = true;
        m_calibration.rowRefModel.referenceAdu = modelHeader.referenceAdu;
        m_calibration.rowRefModel.kQ8 = modelHeader.kQ8;
        m_calibration.rowRefModel.limitAdu = modelHeader.limitAdu;
    } else if (modelHeader.status == detail::NtcModelHeaderStatus::ColdStart) {
        m_calibration.coldStartModel.valid = true;
        m_calibration.coldStartModel.failure = ColdStartModelFailure::None;
        m_calibration.coldStartModel.coldNtcThresholdAdu =
            modelHeader.coldNtcThresholdAdu;
        m_calibration.coldStartModel.amplitudeAdu =
            modelHeader.startupAmplitudeAdu;
        m_calibration.coldStartModel.tauDs = modelHeader.startupTauDs;
    } else if (modelHeader.status == detail::NtcModelHeaderStatus::Valid) {
        m_calibration.ntcModel.valid = true;
        m_calibration.ntcModel.referenceAdu = modelHeader.referenceAdu;
        m_calibration.ntcModel.kQ8 = modelHeader.kQ8;
        m_calibration.ntcModel.limitAdu = modelHeader.limitAdu;
    }
    /* A standalone TNC1 lacks this station's 0°C/50°C evidence. Preserve and
     * display its model, but never promote it to a production PASS. */
    m_calibration.coldStartModelAttempted = false;
    prepareWritePacket();
    return true;
}

bool ProductionCalibrationPage::ensureSessionDirectory(QString *error)
{
    if (!m_sessionPath.isEmpty()) return true;
    const QString rootPath = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (rootPath.isEmpty()) {
        if (error) *error = tr("系统没有可用的应用数据目录");
        return false;
    }
    const QString relative =
        QStringLiteral("production_calibration/%1")
            .arg(QDateTime::currentDateTime().toString(
                QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    QDir root(rootPath);
    if (!root.mkpath(relative)) {
        if (error) *error = tr("无法创建生产标定目录");
        return false;
    }
    m_sessionPath = root.filePath(relative);
    log(tr("自动记录：%1").arg(m_sessionPath));
    return true;
}

ProductionCalibrationPage::ArchiveSnapshot
ProductionCalibrationPage::archiveSnapshot() const
{
    ArchiveSnapshot snapshot;
    snapshot.singleDeviceMode = m_singleDeviceMode;
    snapshot.boardId.clear();
    snapshot.sensorId.clear();
    snapshot.firmwareIdentity = m_deviceFirmwareIdentity;
    snapshot.firmwarePath = m_firmwarePath;
    snapshot.firmwareSha256 = m_firmwareSha256;
    snapshot.bootselRoot = m_bootselRoot;
    snapshot.low = m_low;
    snapshot.high = m_high;
    snapshot.coldStartSamples = m_coldStartSamples;
    snapshot.coldStartPairRejected = m_coldStartPairRejected;
    snapshot.monitorTrace = m_monitorTrace;
    snapshot.monitorContinuityValid = m_monitorContinuityValid;
    snapshot.connectionReference = m_connectionReference;
    snapshot.ntcMinimumC = m_ntcMinimumC;
    snapshot.ntcMaximumC = m_ntcMaximumC;
    snapshot.ntcMinimumAdu = m_ntcMinimumAdu;
    snapshot.ntcMaximumAdu = m_ntcMaximumAdu;
    snapshot.calibration = m_calibration;
    snapshot.writePayloadCrc = m_writePayloadCrc;
    snapshot.writeHeaderCrc = m_writeHeaderCrc;
    snapshot.writeFlashHeaderCrc = m_writeFlashHeaderCrc;
    return snapshot;
}

QList<QPair<QString, ProductionCalibrationPage::CaptureData>>
ProductionCalibrationPage::failedCaptures() const
{
    if (m_sessionPath.isEmpty() || m_currentCapture.frames.isEmpty()) return {};
    const QString phase = m_captureKind == CaptureKind::Low
        ? QStringLiteral("low")
        : QStringLiteral("high");
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
        || !save(QStringLiteral("low_mean_u16le.bin"), meanImageBytes(snapshot.low.meanPixels))
        || !save(QStringLiteral("high_mean_u16le.bin"), meanImageBytes(snapshot.high.meanPixels))
        || !save(QStringLiteral("monitor_trace.csv"),
                 monitorTraceCsv(snapshot.monitorTrace))) {
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
    const QByteArray firmwareIdentity = snapshot.firmwareIdentity.isEmpty()
        ? QByteArrayLiteral("not-reported-by-device")
        : snapshot.firmwareIdentity.toUtf8().replace(',', '_');
    summary += "firmware_identity," + firmwareIdentity + "\n";
    summary += "firmware_path," + snapshot.firmwarePath.toUtf8().replace(',', '_') + "\n";
    summary += "firmware_sha256," + snapshot.firmwareSha256.toUtf8().replace(',', '_') + "\n";
    summary += "bootsel_root," + snapshot.bootselRoot.toUtf8().replace(',', '_') + "\n";
    summary += "low_received," + QByteArray::number(snapshot.low.frames.size()) + "\n";
    summary += "low_accepted," + QByteArray::number(snapshot.low.acceptedIndices.size()) + "\n";
    summary += "high_received," + QByteArray::number(snapshot.high.frames.size()) + "\n";
    summary += "high_accepted," + QByteArray::number(snapshot.high.acceptedIndices.size()) + "\n";
    summary += "low_mean_span_adu," + QByteArray::number(acceptedMeanSpan(snapshot.low), 'f', 6) + "\n";
    summary += "low_vtemp_span_adu," + QByteArray::number(acceptedVtempSpan(snapshot.low)) + "\n";
    summary += "high_mean_span_adu," + QByteArray::number(acceptedMeanSpan(snapshot.high), 'f', 6) + "\n";
    summary += "high_vtemp_span_adu," + QByteArray::number(acceptedVtempSpan(snapshot.high)) + "\n";
    summary += "mean_diff," + QByteArray::number(snapshot.calibration.meanDiff, 'f', 6) + "\n";
    summary += "bad_point_count," + QByteArray::number(snapshot.calibration.badPointCount) + "\n";
    summary += "ntc_model_valid," + QByteArray(snapshot.calibration.ntcModel.valid ? "true\n" : "false\n");
    summary += "ntc_reference_adu," + QByteArray::number(snapshot.calibration.ntcModel.referenceAdu) + "\n";
    summary += "ntc_k_q8," + QByteArray::number(snapshot.calibration.ntcModel.kQ8) + "\n";
    summary += "ntc_limit_adu," + QByteArray::number(snapshot.calibration.ntcModel.limitAdu) + "\n";
    summary += "write_readback_verified," + QByteArray(snapshot.calibration.writeVerified ? "true\n" : "false\n");
    summary += "monitor_continuity_valid," + QByteArray(snapshot.monitorContinuityValid ? "true\n" : "false\n");
    summary += "ntc_coverage_c," + QByteArray::number(
        std::max(0.0, snapshot.ntcMaximumC - snapshot.ntcMinimumC), 'f', 6) + "\n";
    summary += "ntc_coverage_adu," + QByteArray::number(
        int(snapshot.ntcMaximumAdu) - int(snapshot.ntcMinimumAdu)) + "\n";
    summary += "row_ref_model_valid," + QByteArray(
        snapshot.calibration.rowRefModel.valid ? "true\n" : "false\n");
    summary += "row_ref_reference_adu," + QByteArray::number(
        snapshot.calibration.rowRefModel.referenceAdu) + "\n";
    summary += "row_ref_k_q8," + QByteArray::number(
        snapshot.calibration.rowRefModel.kQ8) + "\n";
    summary += "row_ref_limit_adu," + QByteArray::number(
        snapshot.calibration.rowRefModel.limitAdu) + "\n";
    summary += "post_write_validation_attempted," + QByteArray(
        snapshot.calibration.postWriteValidationAttempted
            ? "true\n" : "false\n");
    summary += "post_write_validation_samples," + QByteArray::number(
        snapshot.calibration.postWriteValidationSamples) + "\n";
    summary += "post_write_mean_c," + QByteArray::number(
        snapshot.calibration.postWriteMeanC, 'f', 6) + "\n";
    summary += "post_write_peak_to_peak_c," + QByteArray::number(
        snapshot.calibration.postWritePeakToPeakC, 'f', 6) + "\n";
    const bool productionPassed =
        !snapshot.calibration.testWrite
        && snapshot.low.complete && snapshot.high.complete
        && snapshot.calibration.writeVerified
        && (!EnableRowRefProduction
            || (snapshot.calibration.rowRefModel.valid
                && snapshot.calibration.postWriteValidationPassed));
    summary += "production_pass," + QByteArray(
        productionPassed ? "true\n" : "false\n");
    summary += "artifact_class," + QByteArray(
        snapshot.calibration.testWrite
            ? "test\n" : productionPassed
                ? "production\n" : "pending\n");
    if (!save(QStringLiteral("summary.csv"), summary)) return false;

    const auto telemetryJson = [](const TelemetryState &state) {
        QJsonObject object;
        object.insert(QStringLiteral("valid"), state.valid);
        if (!state.valid) return object;
        object.insert(QStringLiteral("timestamp_us"), state.timestampUs);
        object.insert(QStringLiteral("frame_number"),
                      qint64(state.frameNumber));
        object.insert(QStringLiteral("device_timestamp_ms"),
                      qint64(state.deviceTimestampMs));
        object.insert(QStringLiteral("calibration_id"),
                      qint64(state.calibrationId));
        if (std::isfinite(state.centerRoiC)) {
            object.insert(QStringLiteral("center_roi_c"), state.centerRoiC);
        }
        object.insert(QStringLiteral("center_roi_adu"), state.centerRoiAdu);
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
        object.insert(QStringLiteral("ntc_model_reported"),
                      state.ntcModelReported);
        object.insert(QStringLiteral("ntc_model_valid"), state.ntcModelValid);
        object.insert(QStringLiteral("ntc_model_reference_adu"),
                      state.ntcModelReferenceAdu);
        object.insert(QStringLiteral("ntc_model_k_q8"), state.ntcModelKQ8);
        object.insert(QStringLiteral("ntc_model_limit_adu"),
                      state.ntcModelLimitAdu);
        object.insert(QStringLiteral("startup_model_valid"),
                      state.startupModelValid);
        object.insert(QStringLiteral("startup_model_active"),
                      state.startupModelActive);
        object.insert(QStringLiteral("startup_ntc_gate_adu"),
                      state.startupModelNtcThresholdAdu);
        object.insert(QStringLiteral("startup_amplitude_adu"),
                      state.startupModelAmplitudeAdu);
        object.insert(QStringLiteral("startup_tau_ds"),
                      state.startupModelTauDs);
        object.insert(QStringLiteral("ffc_state"), state.ffcState);
        object.insert(QStringLiteral("row_ref_mean_adu"), state.rowRefMeanAdu);
        object.insert(QStringLiteral("row_ref_span_adu"), state.rowRefSpanAdu);
        object.insert(QStringLiteral("shutter_mean_adu"), state.shutterMeanAdu);
        object.insert(QStringLiteral("row_ref_model_reported"),
                      state.rowRefModelReported);
        object.insert(QStringLiteral("row_ref_model_valid"),
                      state.rowRefModelValid);
        object.insert(QStringLiteral("row_ref_model_active"),
                      state.rowRefModelActive);
        object.insert(QStringLiteral("row_ref_sample_valid"),
                      state.rowRefSampleValid);
        object.insert(QStringLiteral("row_ref_reference_adu"),
                      state.rowRefModelReferenceAdu);
        object.insert(QStringLiteral("row_ref_k_q8"), state.rowRefModelKQ8);
        object.insert(QStringLiteral("row_ref_limit_adu"),
                      state.rowRefModelLimitAdu);
        object.insert(QStringLiteral("row_ref_comp_adu"), state.rowRefCompAdu);
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

    QJsonObject calibration;
    calibration.insert(QStringLiteral("valid"), snapshot.calibration.valid);
    calibration.insert(QStringLiteral("mean_diff_adu"), snapshot.calibration.meanDiff);
    calibration.insert(QStringLiteral("t_ref_low_c"), snapshot.calibration.tRefLow);
    calibration.insert(QStringLiteral("t_ref_high_c"), ReferenceHighC);
    calibration.insert(QStringLiteral("bad_point_count"), snapshot.calibration.badPointCount);
    calibration.insert(QStringLiteral("cold_start_model_attempted"),
                       snapshot.calibration.coldStartModelAttempted);
    calibration.insert(QStringLiteral("write_readback_verified"),
                       snapshot.calibration.writeVerified);
    calibration.insert(QStringLiteral("post_write_validation_attempted"),
                       snapshot.calibration.postWriteValidationAttempted);
    calibration.insert(QStringLiteral("post_write_validation_passed"),
                       snapshot.calibration.postWriteValidationPassed);
    calibration.insert(QStringLiteral("post_write_validation_samples"),
                       snapshot.calibration.postWriteValidationSamples);
    calibration.insert(QStringLiteral("post_write_mean_c"),
                       snapshot.calibration.postWriteMeanC);
    calibration.insert(QStringLiteral("post_write_peak_to_peak_c"),
                       snapshot.calibration.postWritePeakToPeakC);
    calibration.insert(QStringLiteral("artifact_class"),
                       snapshot.calibration.testWrite
                           ? QStringLiteral("test")
                           : productionPassed
                               ? QStringLiteral("production")
                               : QStringLiteral("pending"));
    QJsonObject rowRefModel;
    rowRefModel.insert(QStringLiteral("valid"),
                       snapshot.calibration.rowRefModel.valid);
    rowRefModel.insert(QStringLiteral("reference_adu"),
                       snapshot.calibration.rowRefModel.referenceAdu);
    rowRefModel.insert(QStringLiteral("k_q8"),
                       snapshot.calibration.rowRefModel.kQ8);
    rowRefModel.insert(QStringLiteral("limit_adu"),
                       snapshot.calibration.rowRefModel.limitAdu);
    calibration.insert(QStringLiteral("row_ref_model"), rowRefModel);
    QJsonObject ntcModel;
    ntcModel.insert(QStringLiteral("valid"),
                    snapshot.calibration.ntcModel.valid);
    ntcModel.insert(QStringLiteral("reference_adu"),
                    snapshot.calibration.ntcModel.referenceAdu);
    ntcModel.insert(QStringLiteral("k_q8"),
                    snapshot.calibration.ntcModel.kQ8);
    ntcModel.insert(QStringLiteral("limit_adu"),
                    snapshot.calibration.ntcModel.limitAdu);
    calibration.insert(QStringLiteral("ntc_anchor_model"), ntcModel);
    QJsonObject coldStartModel;
    coldStartModel.insert(QStringLiteral("valid"),
                          snapshot.calibration.coldStartModel.valid);
    coldStartModel.insert(QStringLiteral("failure"),
                          int(snapshot.calibration.coldStartModel.failure));
    coldStartModel.insert(QStringLiteral("cold_ntc_threshold_adu"),
                          snapshot.calibration.coldStartModel.coldNtcThresholdAdu);
    coldStartModel.insert(QStringLiteral("amplitude_adu"),
                          snapshot.calibration.coldStartModel.amplitudeAdu);
    coldStartModel.insert(QStringLiteral("tau_ds"),
                          snapshot.calibration.coldStartModel.tauDs);
    coldStartModel.insert(QStringLiteral("sample_count"),
                          qint64(snapshot.calibration.coldStartModel.sampleCount));
    coldStartModel.insert(QStringLiteral("first_sample_ms"),
                          qint64(snapshot.calibration.coldStartModel.firstSampleMs));
    coldStartModel.insert(QStringLiteral("last_sample_ms"),
                          qint64(snapshot.calibration.coldStartModel.lastSampleMs));
    coldStartModel.insert(QStringLiteral("fitted_offset_adu"),
                          snapshot.calibration.coldStartModel.fittedOffsetAdu);
    coldStartModel.insert(QStringLiteral("residual_rmse_adu"),
                          snapshot.calibration.coldStartModel.residualRmseAdu);
    coldStartModel.insert(QStringLiteral("residual_peak_to_peak_adu"),
                          snapshot.calibration.coldStartModel.residualPeakToPeakAdu);
    calibration.insert(QStringLiteral("cold_start_model"), coldStartModel);
    calibration.insert(QStringLiteral("payload_crc32"),
                       QStringLiteral("%1").arg(snapshot.writePayloadCrc, 8, 16,
                                                QLatin1Char('0')));
    calibration.insert(QStringLiteral("header_crc32"),
                       QStringLiteral("%1").arg(snapshot.writeHeaderCrc, 8, 16,
                                                QLatin1Char('0')));
    calibration.insert(QStringLiteral("flash_header_crc32"),
                       QStringLiteral("%1").arg(snapshot.writeFlashHeaderCrc,
                                                8, 16, QLatin1Char('0')));

    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"), QStringLiteral("tn160.production-calibration.v1"));
    manifest.insert(QStringLiteral("data_contract_version"), 5);
    manifest.insert(QStringLiteral("algorithm"),
                    EnableRowRefProduction
                        ? QStringLiteral("v1_two_point_plus_row_reference")
                        : QStringLiteral("v1_two_point_generic"));
    manifest.insert(QStringLiteral("algorithm_version"),
                    EnableRowRefProduction ? 6 : 1);
    manifest.insert(QStringLiteral("saved_at"),
                    QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    manifest.insert(QStringLiteral("sensor_id"), snapshot.sensorId);
    manifest.insert(QStringLiteral("board_id"), snapshot.boardId);
    manifest.insert(QStringLiteral("firmware_identity"),
                    snapshot.firmwareIdentity.isEmpty()
                        ? QStringLiteral("not-reported-by-device")
                        : snapshot.firmwareIdentity);
    manifest.insert(QStringLiteral("firmware_path"), snapshot.firmwarePath);
    manifest.insert(QStringLiteral("firmware_sha256"), snapshot.firmwareSha256);
    manifest.insert(QStringLiteral("bootsel_root"), snapshot.bootselRoot);
    manifest.insert(QStringLiteral("transport"),
                    QStringLiteral("usb-cdc-ncm-http"));
    manifest.insert(QStringLiteral("raw_frame_bytes"), RawFrameSize);
    manifest.insert(QStringLiteral("required_accepted_frames"), RequiredFrames);
    manifest.insert(QStringLiteral("single_device_workflow"), true);
    manifest.insert(QStringLiteral("fixed_endpoint"),
                    QStringLiteral("http://192.168.17.1"));
    manifest.insert(QStringLiteral("post_write_validation_seconds"), 10);
    manifest.insert(QStringLiteral("production_pass"), productionPassed);
    manifest.insert(QStringLiteral("monitor_continuity_valid"),
                    snapshot.monitorContinuityValid);
    manifest.insert(QStringLiteral("connection_reference"),
                    telemetryJson(snapshot.connectionReference));
    QJsonObject coverage;
    coverage.insert(QStringLiteral("minimum_c"), snapshot.ntcMinimumC);
    coverage.insert(QStringLiteral("maximum_c"), snapshot.ntcMaximumC);
    coverage.insert(QStringLiteral("span_c"),
                    std::max(0.0, snapshot.ntcMaximumC
                                      - snapshot.ntcMinimumC));
    coverage.insert(QStringLiteral("minimum_adu"), snapshot.ntcMinimumAdu);
    coverage.insert(QStringLiteral("maximum_adu"), snapshot.ntcMaximumAdu);
    coverage.insert(QStringLiteral("span_adu"),
                    int(snapshot.ntcMaximumAdu)
                        - int(snapshot.ntcMinimumAdu));
    manifest.insert(QStringLiteral("ntc_coverage"), coverage);
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
    QJsonObject captures;
    captures.insert(QStringLiteral("low"), captureJson(snapshot.low));
    captures.insert(QStringLiteral("high"), captureJson(snapshot.high));
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
        "timestamp_us,frame_number,device_timestamp_ms,calibration_id,"
        "elapsed_s,center_roi_c,center_roi_adu,ntc_c,ntc_adu,vtemp_adu,smooth_vtemp_adu,"
        "nuc_decision,nuc_count,nuc_dg,nuc_dv,nuc_dn,cumulative_nuc_dg,"
        "boot_ntc_adu,mean_diff,anchor,effective_anchor,"
        "ntc_model_reported,ntc_model_valid,ntc_model_reference_adu,"
        "ntc_model_k_q8,ntc_model_limit_adu,startup_model_valid,"
        "startup_model_active,startup_ntc_gate_adu,startup_amplitude_adu,"
        "startup_tau_ds,ffc_state,row_ref_mean_adu,row_ref_span_adu,"
        "shutter_mean_adu,row_ref_model_reported,row_ref_model_valid,"
        "row_ref_model_active,row_ref_sample_valid,row_ref_reference_adu,"
        "row_ref_k_q8,row_ref_limit_adu,row_ref_comp_adu\n");
    const qint64 originUs = trace.constFirst().timestampUs;
    for (const TelemetryState &state : trace) {
        if (!state.valid) continue;
        csv += QByteArray::number(state.timestampUs) + ',';
        csv += QByteArray::number(state.frameNumber) + ',';
        csv += QByteArray::number(state.deviceTimestampMs) + ',';
        csv += QByteArray::number(state.calibrationId) + ',';
        csv += QByteArray::number(
                   double(state.timestampUs - originUs) / 1000000.0,
                   'f', 6)
            + ',';
        if (std::isfinite(state.centerRoiC)) {
            csv += QByteArray::number(state.centerRoiC, 'f', 3);
        }
        csv += ',' + QByteArray::number(state.centerRoiAdu) + ',';
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
        csv += QByteArray::number(state.effectiveAnchor) + ',';
        csv += state.ntcModelReported ? "1," : "0,";
        csv += state.ntcModelValid ? "1," : "0,";
        csv += QByteArray::number(state.ntcModelReferenceAdu) + ',';
        csv += QByteArray::number(state.ntcModelKQ8) + ',';
        csv += QByteArray::number(state.ntcModelLimitAdu) + ',';
        csv += state.startupModelValid ? "1," : "0,";
        csv += state.startupModelActive ? "1," : "0,";
        csv += QByteArray::number(state.startupModelNtcThresholdAdu) + ',';
        csv += QByteArray::number(state.startupModelAmplitudeAdu) + ',';
        csv += QByteArray::number(state.startupModelTauDs) + ',';
        csv += QByteArray::number(int(state.ffcState)) + ',';
        csv += QByteArray::number(state.rowRefMeanAdu) + ',';
        csv += QByteArray::number(state.rowRefSpanAdu) + ',';
        csv += QByteArray::number(state.shutterMeanAdu) + ',';
        csv += state.rowRefModelReported ? "1," : "0,";
        csv += state.rowRefModelValid ? "1," : "0,";
        csv += state.rowRefModelActive ? "1," : "0,";
        csv += state.rowRefSampleValid ? "1," : "0,";
        csv += QByteArray::number(state.rowRefModelReferenceAdu) + ',';
        csv += QByteArray::number(state.rowRefModelKQ8) + ',';
        csv += QByteArray::number(state.rowRefModelLimitAdu) + ',';
        csv += QByteArray::number(state.rowRefCompAdu) + '\n';
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
    return detail::crc32(QByteArrayView(data.constData(), data.size()));
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

    updateQualitySummary();

}

void ProductionCalibrationPage::updateQualitySummary()
{
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
    };
    qualityLines.append(
        tr("监控连续性（仅诊断）：%1")
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
        if (!EnableRowRefProduction) {
            qualityLines.append(tr("温漂模式：通用补偿（模型扩展清零）"));
        } else if (m_calibration.rowRefModel.valid) {
            qualityLines.append(
                tr("RR模型：reference %1 ADU · K_Q8 %2 · limit %3 ADU")
                    .arg(m_calibration.rowRefModel.referenceAdu)
                    .arg(m_calibration.rowRefModel.kQ8)
                    .arg(m_calibration.rowRefModel.limitAdu));
        } else {
            qualityLines.append(
                tr("RR模型：50°C行参考无效，仅允许基础V1测试件"));
        }
        if (EnableRowRefProduction
            && m_calibration.postWriteValidationAttempted) {
            qualityLines.append(
                tr("写后50°C验收：%1 · 均值 %2°C · P-P %3°C")
                    .arg(m_calibration.postWriteValidationPassed
                             ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                    .arg(m_calibration.postWriteMeanC, 0, 'f', 2)
                    .arg(m_calibration.postWritePeakToPeakC, 0, 'f', 2));
        }
    } else {
        qualityLines.append(tr("坏点：尚未计算"));
    }
    m_quality->setText(qualityLines.join(QLatin1Char('\n')));
}

ProductionWorkflowActions ProductionCalibrationPage::actionAvailability() const
{
    const bool idle = !operationActive();
    const bool finalizing = m_finalizationStage != FinalizationStage::None;
    const bool transportActive = m_transport.isActive();
    const bool monitorReady = transportActive && m_monitorReadyAnnounced;
    const bool calibrationStation = m_singleDeviceMode
        || (m_stationSegment >= 11 && m_stationSegment <= 16);
    const bool hasRunData = m_low.complete || m_high.complete
        || !m_coldStartRecords.isEmpty()
        || m_coldStartWindowMissed || m_calibration.valid;
    ProductionWorkflowFacts facts;
    facts.idle = idle;
    facts.finalizing = finalizing;
    facts.transportActive = transportActive;
    facts.transportConnecting = m_transport.isConnecting();
    facts.monitorReady = monitorReady;
    facts.calibrationStation = calibrationStation;
    facts.lowComplete = m_low.complete;
    facts.highComplete = m_high.complete;
    facts.calibrationValid = m_calibration.valid;
    facts.productionPass = productionWriteAvailable();
    facts.testWriteAvailable = testWriteAvailable();
    facts.writeRecoveryAvailable = m_writeAwaitingVerification;
    facts.hasRunData = hasRunData;
    ProductionWorkflowActions actions = evaluateProductionWorkflow(facts);
    if (m_workflowState == QStringLiteral("completed")
        || m_workflowState == QStringLiteral("test-completed")) {
        actions.captureLow = false;
        actions.captureHigh = false;
        actions.restartRun = false;
        actions.writeTest = false;
        actions.writeProduction = false;
    }
    if (m_workflowState == QStringLiteral("validation-failed")) {
        actions.captureLow = false;
        actions.captureHigh = false;
        actions.writeTest = false;
        actions.writeProduction = false;
    }
    return actions;
}

void ProductionCalibrationPage::updateControls()
{
    const ProductionWorkflowActions actions = actionAvailability();
    const bool calibrationStation = m_singleDeviceMode
        || (m_stationSegment >= 11 && m_stationSegment <= 16);
    m_lowButton->setEnabled(actions.captureLow);
    m_highButton->setEnabled(actions.captureHigh);
    m_importButton->setEnabled(!operationActive()
                               && m_finalizationStage == FinalizationStage::None
                               && calibrationStation);
    m_exportButton->setEnabled(!operationActive()
                               && m_finalizationStage == FinalizationStage::None
                               && m_calibration.valid);
    m_importButton->setVisible(calibrationStation);
    m_exportButton->setVisible(calibrationStation);
    if (m_recalibrateButton) {
        const bool productRecalibration =
            !m_singleDeviceMode && m_stationSegment == ProductSegment
            && m_productAddressCommitted;
        m_recalibrateButton->setVisible(productRecalibration);
        m_recalibrateButton->setEnabled(productRecalibration
                                        && actions.connection);
    }
    if (m_restartButton) {
        m_restartButton->setVisible(calibrationStation);
        m_restartButton->setEnabled(actions.restartRun);
    }
    if (m_writeButton) {
        m_writeButton->setVisible(
            calibrationStation && productionWriteAvailable());
        m_writeButton->setEnabled(actions.writeProduction);
    }
    if (m_testWriteButton) {
        m_testWriteButton->setText(
            EnableRowRefProduction && m_calibration.rowRefModel.valid
                ? tr("写入RR测试件") : tr("写入基础V1测试件"));
        m_testWriteButton->setVisible(
            calibrationStation && m_calibration.valid
            && !productionWriteAvailable());
        m_testWriteButton->setEnabled(actions.writeTest);
    }
    if (m_retryVerifyButton) {
        m_retryVerifyButton->setVisible(
            calibrationStation && m_writeAwaitingVerification);
        m_retryVerifyButton->setEnabled(actions.retryVerification);
    }
    emitWorkflowState();
}

void ProductionCalibrationPage::setStatus(const QString &state,
                                          const QString &text)
{
    if (m_rowStatus) {
        m_rowStatus->setText(text);
        m_rowStatus->setProperty("state", state);
        m_rowStatus->style()->unpolish(m_rowStatus);
        m_rowStatus->style()->polish(m_rowStatus);
    }
    emit transferStateChanged(state, text);
    if (m_host) m_host->setStatus(tr("TN160 · 生产标定 · %1").arg(text));
}

void ProductionCalibrationPage::emitWorkflowState()
{
    const ProductionWorkflowActions actions = actionAvailability();
    QVariantMap state;
    state.insert(QStringLiteral("state"), m_workflowState);
    state.insert(QStringLiteral("single_device"), m_singleDeviceMode);
    state.insert(QStringLiteral("endpoint"), m_endpoint);
    state.insert(QStringLiteral("serial"), m_deviceSerial);
    state.insert(QStringLiteral("firmware_path"), m_firmwarePath);
    state.insert(QStringLiteral("firmware_sha256"), m_firmwareSha256);
    state.insert(QStringLiteral("session_path"), m_sessionPath);
    state.insert(QStringLiteral("connected"),
                 m_transport.isActive() && m_monitorReadyAnnounced);
    state.insert(QStringLiteral("recording"),
                 m_continuousAdcFile && m_continuousAdcFile->isOpen());
    state.insert(QStringLiteral("ntc_coverage_c"), ntcCoverageC());
    state.insert(QStringLiteral("low_complete"), m_low.complete);
    state.insert(QStringLiteral("high_complete"), m_high.complete);
    state.insert(QStringLiteral("row_ref_model_valid"),
                 m_calibration.rowRefModel.valid);
    state.insert(QStringLiteral("post_write_validation_passed"),
                 m_calibration.postWriteValidationPassed);
    state.insert(QStringLiteral("production_pass"),
                 !m_calibration.testWrite
                     && m_low.complete && m_high.complete
                     && m_calibration.writeVerified
                     && (!EnableRowRefProduction
                         || (m_calibration.rowRefModel.valid
                             && m_calibration.postWriteValidationPassed)));
    state.insert(QStringLiteral("test_write_available"), testWriteAvailable());
    state.insert(QStringLiteral("connection_action_enabled"),
                 actions.connection);
    state.insert(QStringLiteral("scan_action_enabled"), actions.scan);
    state.insert(QStringLiteral("endpoint_edit_enabled"),
                 actions.editEndpoint);
    state.insert(QStringLiteral("write_recovery_available"),
                 actions.retryVerification);
    if (m_calibration.rowRefModel.valid) {
        state.insert(QStringLiteral("row_ref_reference_adu"),
                     m_calibration.rowRefModel.referenceAdu);
        state.insert(QStringLiteral("row_ref_k_q8"),
                     m_calibration.rowRefModel.kQ8);
        state.insert(QStringLiteral("row_ref_limit_adu"),
                     m_calibration.rowRefModel.limitAdu);
    }
    emit workflowStateChanged(state);
}

void ProductionCalibrationPage::log(const QString &message,
                                    const QString &level)
{
    if (m_productionLog) {
        const QString levelText =
            level == QStringLiteral("error") ? tr("失败")
            : level == QStringLiteral("warning") ? tr("警告")
                                                 : QString{};
        const QString prefix =
            QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        m_productionLog->appendPlainText(
            levelText.isEmpty()
                ? tr("%1  %2").arg(prefix, message)
                : tr("%1  %2  %3").arg(prefix, levelText, message));
    }
}

} // namespace serein::scope::tn160ncm
