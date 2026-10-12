#include "T384Workspace.h"
#include "T384CalibrationPage.h"
#include "T384OemKPanel.h"
#include "T384CalibrationProtocol.h"
#include "T384CalibrationTransaction.h"
#include "T384Engine.h"
#include "T384Transport.h"
#include "ThermalView.h"
#include "TrendWidget.h"
#include <ThermalWorkbenchUi.h>

#include <serein/scope/WorkspaceCanvas.h>
#include <serein/scope/AppPaths.h>
#include <serein/scope/HttpRequestClient.h>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QImage>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QVBoxLayout>

#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace serein::scope::t384 {
namespace {
QString safePathLabel(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),
                  QStringLiteral("_"));
    return value.isEmpty() ? QStringLiteral("unknown-device") : value.left(80);
}
}

T384Workspace::T384Workspace(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host), m_uiProvider(new WorkspaceUiProvider(this)),
      m_engine(new T384Engine),
      m_transport(new T384Transport(std::unique_ptr<T384Engine>(m_engine), this)),
      m_calibration(new T384CalibrationPage(this)),
      m_calibrationHttp(new HttpRequestClient(this)),
      m_calibrationTransaction(
          new T384CalibrationTransaction(m_calibrationHttp, this))
{
    setObjectName(QStringLiteral("t384Workspace"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    m_canvas = new WorkspaceCanvas(QStringLiteral("t384/live-workbench-v1"));
    auto *thermal = new ThermalView;
    thermal->setObjectName(QStringLiteral("t384ThermalView"));
    thermal->setCrosshairVisible(true);
    thermal->setExtremaVisible(true);
    m_thermals.append(thermal);
    auto *image = new QWidget;
    auto *imageLayout = new QVBoxLayout(image);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    imageLayout->addWidget(thermal, 1);
    m_canvas->addComponent(QStringLiteral("t384.thermal"), tr("T256/T384/T640 热图"), image,
                           QRectF(0, 0, 1, 1), QSize(420, 280));
    root->addWidget(m_canvas, 1);

    m_workbenchUi = std::make_unique<thermal::ThermalWorkbenchUi>(
        this, QStringLiteral("t384"));
    buildContextPanel();
    buildInspectorPanel();
    buildOutputPanel();

    WorkspaceUiContext ui;
    ui.id = QStringLiteral("t384");
    ui.layoutProfile = WorkspaceLayoutProfile::CalibrationTask;
    ui.contextPanel = {m_contextPanel, tr("生产标定"), nullptr, {}};
    ui.inspectorPanel = {m_inspectorPanel, tr("数据与属性"), nullptr, {}};
    ui.outputPanel = {m_outputPanel, tr("诊断输出"), nullptr, {}};
    ui.componentLibrary.items = {
        {QStringLiteral("t384.thermal"), tr("T256/T384/T640 热图"), QStringLiteral(":/serein/thermal.svg")},
        {QStringLiteral("t384.trend"), tr("T256/T384/T640 趋势"), {}},
    };
    ui.componentLibrary.activationTarget = this;
    ui.componentLibrary.activationMethod = QByteArrayLiteral("activateLibraryComponent");
    WorkspaceUiPresentationOptions presentation;
    presentation.contextPanel.margins = WorkspacePanelMargins::Flush;
    presentation.inspectorPanel.margins = WorkspacePanelMargins::Flush;
    presentation.outputPanel.margins = WorkspacePanelMargins::Flush;
    presentation.outputPanel.headerControl = m_outputTabs;
    m_uiProvider->setContext(std::move(ui), presentation);
    connect(m_endpoint, &QLineEdit::textChanged, m_oemK, &T384OemKPanel::setEndpoint);
    connect(m_calibration, &T384CalibrationPage::operationActiveChanged,
            m_oemK, [this](bool active) { m_oemK->setEnabled(!active); });
    connect(m_oemK, &T384OemKPanel::record, this, [this](const QString &message) {
        appendLog(QStringLiteral("info"), message);
    });
    connect(m_oemK, &T384OemKPanel::availabilityChanged, this, [this] {
        const bool busy = m_oemK->busy();
        const bool held = busy || m_oemK->holdsCalibration();
        m_calibration->contextPanel()->setEnabled(!held);
        m_calibration->resultPanel()->setEnabled(!held);
        m_endpoint->setEnabled(!held);
        m_connect->setEnabled(!busy);
        m_stop->setEnabled(!busy && m_transport->isActive());
    });
    connect(m_oemK, &T384OemKPanel::commandSubmitted,
            m_calibration, &T384CalibrationPage::invalidateCapturedPoints);
    connect(m_oemK, &T384OemKPanel::prepareCommand, this, [this] {
        m_transport->stop();
    });
    connect(m_oemK, &T384OemKPanel::operationFinished, this, [this](bool resume) {
        if (resume) QTimer::singleShot(250, this, [this] {
            if (!m_oemK->busy()) m_transport->start(m_endpoint->text());
        });
    });
    connect(m_connect, &QPushButton::clicked, this, [this] {
        m_lastLoggedRetryReason.clear();
        m_bytesReceived = 0;
        m_framesReceived = 0;
        m_firstSampleTimestampUs = 0;
        m_lastFrameSequence = 0;
        m_hostSequenceGaps = 0;
        m_lastCenterTemperature = std::numeric_limits<double>::quiet_NaN();
        m_lastCenterRaw = std::numeric_limits<double>::quiet_NaN();
        m_trendHistory.clear();
        for (const auto &trend : std::as_const(m_trends))
            if (trend) trend->clear();
        if (m_traffic) m_traffic->setText(tr("接收：0 B · 完整帧：0"));
        if (m_error) {
            m_error->clear();
            m_error->setToolTip({});
        }
        appendLog(QStringLiteral("info"),
                  tr("开始连接：%1").arg(m_endpoint->text()));
        if (!m_oemK->busy()) m_transport->start(m_endpoint->text());
    });
    connect(m_stop, &QPushButton::clicked, this, [this] {
        m_transport->stop();
        appendLog(QStringLiteral("info"), tr("已停止 T384 数据流"));
    });
    connect(m_transport, &T384Transport::stateChanged, this, [this](const QString &state) {
        const QString shown = state == QStringLiteral("connecting") ? tr("连接中")
            : state == QStringLiteral("reconnecting") ? tr("重连中")
            : state == QStringLiteral("streaming") ? tr("流式传输")
            : state == QStringLiteral("error") ? tr("错误")
            : state == QStringLiteral("disconnected") ? tr("未连接") : state;
        if (m_status) m_status->setText(shown);
        if (m_connect) m_connect->setText(
            state == QStringLiteral("streaming") ? tr("重新连接") : tr("连接"));
        if (m_stop) m_stop->setEnabled(state != QStringLiteral("disconnected") && !m_oemK->busy());
        updateInspector();
        appendLog(QStringLiteral("info"), tr("状态：%1").arg(shown));
        if (m_host) m_host->setStatus(tr("T384 · %1").arg(shown));
        if (state == QStringLiteral("streaming")) {
            m_lastLoggedRetryReason.clear();
            if (m_error) {
                m_error->clear();
                m_error->setToolTip({});
            }
        }
    });
    connect(m_transport, &T384Transport::errorOccurred, this,
            [this](const QString &message) {
                if (m_error) {
                    m_error->setText(m_lastLoggedRetryReason.contains(
                            QStringLiteral("409"))
                        ? tr("视频流仍被占用，请关闭其他画面后重试")
                        : tr("连接失败，请检查设备后重试"));
                    m_error->setToolTip(message);
                }
                appendLog(QStringLiteral("error"), message);
                if (m_host) m_host->setStatus(
                    tr("T384 · %1").arg(m_error ? m_error->text()
                                               : tr("连接失败")));
            });
    connect(m_transport, &T384Transport::retryScheduled, this,
            [this](const QString &reason, const int delayMs,
                   const int remainingMs) {
                if (m_error) {
                    m_error->setText(reason.contains(QStringLiteral("409"))
                        || reason.contains(tr("占用"))
                        ? tr("视频流仍被占用，正在等待释放")
                        : tr("正在恢复连接…"));
                    m_error->setToolTip(reason);
                }
                m_calibration->onTransportInterrupted(reason);
                if (m_lastLoggedRetryReason != reason) {
                    appendLog(QStringLiteral("warning"),
                              tr("连接重试：%1；%2 ms后重试，剩余%3 ms")
                                  .arg(reason).arg(delayMs).arg(remainingMs));
                    m_lastLoggedRetryReason = reason;
                }
            });
    connect(m_transport, &T384Transport::bytesReceived, this,
            [this](const qint64 count) {
                m_bytesReceived += count;
                if (m_traffic) {
                    m_traffic->setText(tr("接收：%1 B · 完整帧：%2")
                                            .arg(m_bytesReceived)
                                            .arg(m_framesReceived));
                }
                updateInspector();
            });
    connect(m_transport, &T384Transport::packetsDecoded, this, &T384Workspace::handlePackets);
    connect(m_transport, &T384Transport::diagnosticsReady, this,
            [this](const QVariantMap &values) {
                updateDiagnostics(values);
                const QString identity = values.value(QStringLiteral("mini2.pn")).toString()
                    + QLatin1Char('/') + values.value(QStringLiteral("mini2.sn")).toString()
                    + QLatin1Char('/') + values.value(QStringLiteral("mini2.firmware_version")).toString();
                const QByteArray pn = values.value(QStringLiteral("mini2.pn")).toString().toUtf8();
                const QByteArray sn = values.value(QStringLiteral("mini2.sn")).toString().toUtf8();
                const QByteArray fwHex = values.value(QStringLiteral("mini2.firmware_version"))
                                             .toString().toUtf8().toHex();
                if (!pn.isEmpty() && !sn.isEmpty() && !fwHex.isEmpty()) {
                    m_calibration->setDeviceIdentity(
                        calibrationIdentity(pn, sn, fwHex));
                }
                appendLog(QStringLiteral("info"), tr("设备诊断已更新：%1").arg(identity));
            });
    connect(m_transport, &T384Transport::streamMetadataReady, this,
            [this](const QVariantMap &metadata) {
                const QString model = metadata
                    .value(QStringLiteral("temperature_model")).toString();
                const QStringList values = metadata
                    .value(QStringLiteral("y16_linear_x100"))
                    .toString().split(QLatin1Char(','));
                bool zeroOk = false;
                bool countsOk = false;
                const double zeroX100 = values.value(0).toDouble(&zeroOk);
                const double countsX100 = values.value(1).toDouble(&countsOk);
                const bool available = !model.isEmpty()
                    && model != QStringLiteral("unavailable")
                    && zeroOk && countsOk && countsX100 > 0.0;
                const double zeroRaw = available ? zeroX100 / 100.0 : 0.0;
                const double countsPerC = available ? countsX100 / 100.0 : 0.0;
                m_calibration->setRuntimeModel(available, zeroRaw, countsPerC);
                appendLog(available ? QStringLiteral("info")
                                    : QStringLiteral("warning"),
                          available
                              ? tr("已从RAW流加载运行时温度模型：%1，zero=%2，counts/°C=%3")
                                    .arg(model).arg(zeroRaw, 0, 'f', 2)
                                    .arg(countsPerC, 0, 'f', 2)
                              : tr("设备RAW流未提供可用温度模型；仅显示Y16原始量"));
            });
    connect(m_calibration, &T384CalibrationPage::modelChanged, this,
            [this](bool enabled, double zero, double scale) {
                m_engine->setExperimentalModel(enabled, zero, scale);
            });
    connect(m_calibration, &T384CalibrationPage::calibrationRunStarted, this,
            [this] {
                m_calibrationSessionPath.clear();
                m_pendingCalibrationPacket.clear();
                m_pendingCalibrationEvidence.clear();
                m_calibrationAutoWritePending = false;
                appendLog(QStringLiteral("info"), tr("开始新一轮两点标定"));
            });
    connect(m_calibration, &T384CalibrationPage::boundaryStateRequested,
            this, [this](const QString &tag) {
                if (m_connect) m_connect->setEnabled(false);
                if (m_stop) m_stop->setEnabled(false);
                m_transport->stop();
                appendLog(QStringLiteral("info"),
                          tr("停流并读取采集边界状态：%1").arg(tag));
                m_transport->requestCalibrationBoundaryState(tag);
            });
    connect(m_transport, &T384Transport::calibrationBoundaryStateReady,
            this, [this](const QString &tag, const bool success,
                         const QVariantMap &snapshot, const QString &message) {
                if (!success) appendLog(QStringLiteral("error"), message);
                else appendLog(QStringLiteral("info"),
                               tr("%1：身份 %2 · gain=%3 · FFC=%4 · 快门=%5")
                                   .arg(tag,
                                        snapshot.value(QStringLiteral("identity_sha256_hex"))
                                            .toString().left(12),
                                        snapshot.value(QStringLiteral("gain")).toString(),
                                        snapshot.value(QStringLiteral("auto_ffc_enabled"))
                                            .toBool() ? tr("开") : tr("关"),
                                        snapshot.value(QStringLiteral("shutter_status"))
                                            .toString()));
                m_calibration->onBoundaryStateReady(tag, success, snapshot, message);
                if (m_connect) m_connect->setEnabled(!m_calibrationAutoWritePending);
                if (m_stop) m_stop->setEnabled(success && !m_calibrationAutoWritePending);
                if (success && !m_calibrationAutoWritePending) {
                    QTimer::singleShot(250, this, [this] {
                        if (!m_oemK->busy()) m_transport->start(m_endpoint->text());
                    });
                }
            });
    connect(m_calibration, &T384CalibrationPage::evidenceCaptured, this,
            [this](const QByteArray &evidence) {
                m_pendingCalibrationEvidence = evidence;
                const QJsonObject object = QJsonDocument::fromJson(evidence).object();
                const QString qualityReason = object.value(QStringLiteral("quality_reason"))
                                                  .toString();
                const QString outcome = object.value(QStringLiteral("outcome")).toString();
                if (!qualityReason.isEmpty()
                    && (outcome.endsWith(QStringLiteral("failed"))
                        || outcome == QStringLiteral("capture_timeout")
                        || outcome == QStringLiteral("stream_interrupted")))
                    appendLog(QStringLiteral("error"), qualityReason);
                updateCalibrationResult(object,
                                        tr("已记录本轮产测证据，等待后续状态"),
                                        false, false);
                if (object.value(QStringLiteral("zero_samples")).toArray().isEmpty()
                    && object.value(QStringLiteral("high_samples")).toArray().isEmpty())
                    return;
                if (m_calibrationSessionPath.isEmpty()) {
                    QString error;
                    if (!beginCalibrationArchive({}, evidence, &error))
                        appendLog(QStringLiteral("error"), error);
                } else {
                    QString error;
                    if (!writeArchiveFile(QStringLiteral("evidence.json"), evidence, &error))
                        appendLog(QStringLiteral("error"), error);
                }
            });
    connect(m_calibration, &T384CalibrationPage::autoApplyRequested, this,
            &T384Workspace::beginAutomaticCalibrationWrite);
    connect(m_calibration, &T384CalibrationPage::verifySavedRequested, this,
            &T384Workspace::beginReadOnlyCalibrationVerify);
    connect(m_calibrationTransaction,
            &T384CalibrationTransaction::previousPacketReady, this,
            [this](const QByteArray &packet) {
                if (packet.isEmpty()) return;
                QString error;
                if (!writeArchiveFile(QStringLiteral("before.packet"), packet, &error))
                    appendLog(QStringLiteral("error"), error);
            });
    connect(m_calibrationTransaction, &T384CalibrationTransaction::progress, this,
            [this](T384CalibrationTransaction::Phase, const QString &detail) {
                appendLog(QStringLiteral("info"), tr("产测标定：%1").arg(detail));
                if (m_calibrationResult)
                    m_calibrationResult->setText(tr("产测标定进行中：%1").arg(detail));
            });
    connect(m_calibrationTransaction, &T384CalibrationTransaction::complete, this,
            [this](const T384CalibrationTransaction::Outcome outcome,
                   const QString &detail, const QJsonObject &result) {
                finishCalibrationTransaction(int(outcome), detail, result);
            });
}

void T384Workspace::buildContextPanel()
{
    thermal::WorkbenchSection connection = m_workbenchUi->addContextSection(
        QStringLiteral("t384ConnectionSection"), tr("设备与连接"));
    auto *endpointLabel = new QLabel(tr("USB NCM 地址"), connection.frame);
    endpointLabel->setObjectName(QStringLiteral("metricLabel"));
    connection.body->addWidget(endpointLabel);
    m_endpoint = new QLineEdit(QStringLiteral("http://192.168.17.1"),
                               connection.frame);
    m_endpoint->setObjectName(QStringLiteral("t384Endpoint"));
    m_endpoint->setPlaceholderText(tr("192.168.17.1 或 http://地址"));
    m_endpoint->setClearButtonEnabled(true);
    m_endpoint->setMinimumWidth(0);
    m_endpoint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connection.body->addWidget(m_endpoint);
    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(6);
    m_connect = new QPushButton(tr("连接"), connection.frame);
    m_connect->setObjectName(QStringLiteral("t384Connect"));
    m_connect->setProperty("kind", QStringLiteral("primary"));
    m_stop = new QPushButton(tr("停止"), connection.frame);
    m_stop->setObjectName(QStringLiteral("t384Stop"));
    m_stop->setEnabled(false);
    for (QPushButton *button : {m_connect, m_stop}) {
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        actions->addWidget(button, 1);
    }
    connection.body->addLayout(actions);
    m_status = new QLabel(tr("未连接"), connection.frame);
    m_status->setObjectName(QStringLiteral("statusPill"));
    connection.body->addWidget(m_status);
    m_error = new QLabel(tr("连接后显示设备状态"), connection.frame);
    m_error->setWordWrap(true);
    m_error->setObjectName(QStringLiteral("pageSubtitle"));
    connection.body->addWidget(m_error);
    thermal::WorkbenchSection oemK = m_workbenchUi->addContextSection(
        QStringLiteral("t384OemKSection"), tr("原厂 K 标定 · 65°C"));
    m_oemK = new T384OemKPanel(oemK.frame);
    m_oemK->setEndpoint(m_endpoint->text());
    oemK.body->addWidget(m_oemK);
    thermal::WorkbenchSection calibration = m_workbenchUi->addContextSection(
        QStringLiteral("t384ProductionSection"), tr("生产标定"));
    QWidget *calibrationPanel = m_calibration->contextPanel();
    calibrationPanel->setMinimumWidth(0);
    calibrationPanel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    if (calibrationPanel->layout())
        calibrationPanel->layout()->setContentsMargins(0, 0, 0, 0);
    calibration.body->addWidget(calibrationPanel);
    thermal::WorkbenchSection records = m_workbenchUi->addContextSection(
        QStringLiteral("t384ProductionRecords"), tr("生成记录"));
    m_log = new QPlainTextEdit(records.frame);
    m_log->setObjectName(QStringLiteral("t384ProductionLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    m_log->setMinimumHeight(160);
    m_log->setPlaceholderText(tr("连接与标定记录"));
    records.body->addWidget(m_log);
    m_workbenchUi->addContextStretch();
    m_contextPanel = m_workbenchUi->contextPanel();
}

void T384Workspace::buildInspectorPanel()
{
    thermal::WorkbenchSection identity = m_workbenchUi->addInspectorSection(
        QStringLiteral("t384IdentitySection"), tr("设备"));
    m_inspectorIdentity = new QLabel(tr("等待设备 · 192.168.17.1"),
                                     identity.frame);
    m_inspectorIdentity->setObjectName(
        QStringLiteral("t384UnifiedDeviceIdentity"));
    m_inspectorIdentity->setWordWrap(true);
    m_inspectorIdentity->setMinimumWidth(0);
    m_inspectorIdentity->setTextInteractionFlags(Qt::TextSelectableByMouse
                                                  | Qt::TextSelectableByKeyboard);
    identity.body->addWidget(m_inspectorIdentity);
    m_workbenchUi->addMetric(identity, tr("Profile / 格式"), &m_inspectorProfile);

    thermal::WorkbenchSection metrics = m_workbenchUi->addInspectorSection(
        QStringLiteral("t384MetricsSection"), tr("实时数据"));
    m_workbenchUi->addMetric(metrics, tr("中心 16×16 温度"),
                             &m_inspectorTemperature, true);
    m_workbenchUi->addMetric(metrics, tr("中心 16×16 Y16"), &m_inspectorRaw);
    m_workbenchUi->addMetric(metrics, tr("源帧率"), &m_inspectorSourceFps);
    m_workbenchUi->addMetric(metrics, tr("完整流帧率"), &m_inspectorStreamFps);

    thermal::WorkbenchSection link = m_workbenchUi->addInspectorSection(
        QStringLiteral("t384LinkSection"), tr("链路质量"));
    m_workbenchUi->addMetric(link, tr("连接状态"), &m_inspectorLink);
    m_workbenchUi->addMetric(link, tr("完整帧 / 接收"), &m_inspectorCompleteFrames);
    m_workbenchUi->addMetric(link, tr("序号缺口"), &m_inspectorSequenceGaps);

    m_workbenchUi->addInspectorStretch();
    m_inspectorPanel = m_workbenchUi->inspectorPanel();
}

void T384Workspace::buildOutputPanel()
{
    m_outputPanel = m_workbenchUi->outputPanel();
    m_outputTabs = m_workbenchUi->outputTabs();
    m_outputStack = m_workbenchUi->outputStack();
    m_outputTrend = new TrendWidget(m_outputStack);
    m_outputTrend->setObjectName(QStringLiteral("t384OutputTrend"));
    m_outputTrend->setMinimumHeight(0);
    m_outputTrend->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_trends.append(m_outputTrend);
    connect(m_outputTrend, &TrendWidget::exportPngRequested,
            this, &T384Workspace::exportTrendPng);
    connect(m_outputTrend, &TrendWidget::exportCsvRequested,
            this, &T384Workspace::exportTrendCsv);
    m_workbenchUi->addOutputPage(tr("实时趋势"), m_outputTrend);

    auto *results = new QWidget(m_outputStack);
    results->setMinimumSize(0, 0);
    results->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *resultsLayout = new QVBoxLayout(results);
    resultsLayout->setContentsMargins(8, 8, 8, 8);
    resultsLayout->setSpacing(8);
    m_calibrationResult = new QLabel(tr("尚未开始产测双点标定"), results);
    m_calibrationResult->setObjectName(QStringLiteral("statusPill"));
    m_calibrationResult->setWordWrap(true);
    resultsLayout->addWidget(m_calibrationResult);
    m_calibrationArchive = new QLabel(tr("证据目录：—"), results);
    m_calibrationArchive->setObjectName(QStringLiteral("pageSubtitle"));
    m_calibrationArchive->setWordWrap(true);
    m_calibrationArchive->setTextInteractionFlags(Qt::TextSelectableByMouse
                                                   | Qt::TextSelectableByKeyboard);
    resultsLayout->addWidget(m_calibrationArchive);
    QWidget *calibrationResults = m_calibration->resultPanel();
    calibrationResults->setMinimumSize(0, 0);
    calibrationResults->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    if (calibrationResults->layout())
        calibrationResults->layout()->setContentsMargins(0, 0, 0, 0);
    resultsLayout->addWidget(calibrationResults, 1);
    m_workbenchUi->addOutputPage(tr("标定结果"), results);
}

T384Workspace::~T384Workspace() { shutdown(); }

void T384Workspace::shutdown()
{
    if (m_oemK) m_oemK->shutdown();
    if (m_calibrationTransaction) m_calibrationTransaction->shutdown();
    if (m_calibrationHttp) m_calibrationHttp->cancelAll();
    if (m_transport) m_transport->stop();
}

QList<WorkspaceComponentDefinition> T384Workspace::componentDefinitions() const
{
    const auto caps = WorkspaceComponentCapability::Movable
        | WorkspaceComponentCapability::Resizable
        | WorkspaceComponentCapability::Maximizable;
    return {
        {QStringLiteral("t384.thermal"), QStringLiteral("t384"), tr("T256/T384/T640 热图"),
         tr("实时监控"), QStringLiteral(":/serein/thermal.svg"), QSize(640, 420),
         QSize(380, 240), caps | WorkspaceComponentCapability::DataBinding,
         WorkspaceBindingKind::ImageChannel, {}},
        {QStringLiteral("t384.trend"), QStringLiteral("t384"), tr("T256/T384/T640 趋势"),
         tr("实时监控"), {}, QSize(640, 260), QSize(320, 160), caps,
         WorkspaceBindingKind::SampleChannel, {}},
    };
}

QWidget *T384Workspace::createComponent(const QString &id, QWidget *parent)
{
    if (id == QStringLiteral("t384.thermal")) {
        auto *view = new ThermalView(parent);
        view->setCrosshairVisible(true);
        view->setExtremaVisible(true);
        m_thermals.append(view);
        return view;
    }
    if (id == QStringLiteral("t384.trend")) {
        auto *trend = new TrendWidget(parent);
        m_trends.append(trend);
        return trend;
    }
    return nullptr;
}

bool T384Workspace::activateLibraryComponent(const QString &definitionId)
{
    return m_canvas && m_canvas->addOrShowComponentAt(definitionId.trimmed(), QPoint(-1, -1));
}

void T384Workspace::beginAutomaticCalibrationWrite(
    const QByteArray &packet, const QByteArray &evidence)
{
    m_calibrationAutoWritePending = true;
    if (m_calibrationTransaction->active()) {
        m_calibration->onAutoApplyFinished(false, false,
                                           tr("已有标定事务正在执行"));
        m_calibrationAutoWritePending = false;
        return;
    }
    m_pendingCalibrationPacket = packet;
    m_pendingCalibrationEvidence = evidence;
    QString error;
    if (m_calibrationSessionPath.isEmpty()
        && !beginCalibrationArchive(packet, evidence, &error)) {
        m_calibration->onAutoApplyFinished(false, false, error);
        m_calibrationAutoWritePending = false;
        return;
    }
    if (!writeArchiveFile(QStringLiteral("candidate.packet"), packet, &error)
        || !writeArchiveFile(QStringLiteral("evidence.json"), evidence, &error)) {
        m_calibration->onAutoApplyFinished(false, false, error);
        m_calibrationAutoWritePending = false;
        return;
    }
    if (m_connect) m_connect->setEnabled(false);
    if (m_stop) m_stop->setEnabled(false);
    m_transport->stop();
    appendLog(QStringLiteral("info"),
              tr("质量门通过：已停止RAW16流，自动备份、写入并验证"));
    QTimer::singleShot(250, this,
                       [this] { startCalibrationTransaction(false); });
}

void T384Workspace::beginReadOnlyCalibrationVerify(
    const QByteArray &packet, const QByteArray &evidence)
{
    if (m_calibrationTransaction->active()) return;
    m_pendingCalibrationPacket = packet;
    m_pendingCalibrationEvidence = evidence;
    QString error;
    if (m_calibrationSessionPath.isEmpty()
        && !beginCalibrationArchive(packet, evidence, &error)) {
        m_calibration->onAutoApplyFinished(false, true, error);
        return;
    }
    if (m_connect) m_connect->setEnabled(false);
    if (m_stop) m_stop->setEnabled(false);
    m_transport->stop();
    appendLog(QStringLiteral("info"), tr("开始只读核验；不会再次写入Flash"));
    QTimer::singleShot(250, this,
                       [this] { startCalibrationTransaction(true); });
}

void T384Workspace::startCalibrationTransaction(const bool verifyOnly)
{
    T384CalibrationTransaction::ExpectedDevice expected;
    const QJsonObject capturedDevice = m_calibration->calibrationDeviceState();
    expected.endpoint = QUrl::fromUserInput(m_endpoint->text().trimmed());
    expected.pn = capturedDevice.isEmpty()
        ? m_lastDiagnostics.value(QStringLiteral("mini2.pn")).toString()
        : capturedDevice.value(QStringLiteral("pn")).toString();
    expected.sn = capturedDevice.isEmpty()
        ? m_lastDiagnostics.value(QStringLiteral("mini2.sn")).toString()
        : capturedDevice.value(QStringLiteral("sn")).toString();
    expected.fwHex = capturedDevice.isEmpty()
        ? m_lastDiagnostics.value(QStringLiteral("mini2.firmware_version"))
              .toString().toUtf8().toHex()
        : capturedDevice.value(QStringLiteral("fw_hex")).toString();
    expected.profile = currentProfile();
    const bool started = verifyOnly
        ? m_calibrationTransaction->startVerify(expected,
                                                m_pendingCalibrationPacket)
        : m_calibrationTransaction->startWrite(expected,
                                               m_pendingCalibrationPacket);
    if (!started) {
        m_calibrationAutoWritePending = false;
        const QString detail = tr("标定事务启动失败：设备身份、profile或标定包不完整");
        m_calibration->onAutoApplyFinished(false, verifyOnly, detail);
        appendLog(QStringLiteral("error"), detail);
        if (m_connect) m_connect->setEnabled(true);
        if (m_stop) m_stop->setEnabled(true);
        if (!verifyOnly)
            QTimer::singleShot(250, this,
                               [this] { if (!m_oemK->busy()) m_transport->start(m_endpoint->text()); });
    }
}

void T384Workspace::finishCalibrationTransaction(const int outcomeValue,
                                                  const QString &detail,
                                                  const QJsonObject &result)
{
    m_calibrationAutoWritePending = false;
    const auto outcome = T384CalibrationTransaction::Outcome(outcomeValue);
    const bool success = outcome == T384CalibrationTransaction::Outcome::Succeeded;
    const bool uncertain = outcome == T384CalibrationTransaction::Outcome::WriteUncertain;
    QJsonObject archived = result;
    archived.insert(QStringLiteral("finished_utc"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    archived.insert(QStringLiteral("detail"), detail);
    archived.insert(QStringLiteral("outcome"),
                    success ? QStringLiteral("applied_verified")
                            : uncertain ? QStringLiteral("write_uncertain")
                                        : QStringLiteral("failed"));
    archived.insert(QStringLiteral("packet_sha256"),
                    QString::fromLatin1(QCryptographicHash::hash(
                        m_pendingCalibrationPacket, QCryptographicHash::Sha256).toHex()));
    QString archiveError;
    if (!writeArchiveFile(QStringLiteral("result.json"),
                          QJsonDocument(archived).toJson(QJsonDocument::Indented),
                          &archiveError)) {
        appendLog(QStringLiteral("error"), archiveError);
    }
    m_calibration->onAutoApplyFinished(success, uncertain, detail);
    updateCalibrationResult(archived, detail, success, uncertain);
    appendLog(success ? QStringLiteral("info") : QStringLiteral("error"), detail);
    if (m_connect) m_connect->setEnabled(true);
    if (m_stop) m_stop->setEnabled(true);
    if (!uncertain) {
        QTimer::singleShot(250, this,
                           [this] { if (!m_oemK->busy()) m_transport->start(m_endpoint->text()); });
    }
}

bool T384Workspace::beginCalibrationArchive(const QByteArray &packet,
                                            const QByteArray &evidence,
                                            QString *error)
{
    const QString serial = safePathLabel(
        m_lastDiagnostics.value(QStringLiteral("mini2.sn")).toString());
    const QString stamp = QDateTime::currentDateTimeUtc()
                              .toString(QStringLiteral("yyyyMMddTHHmmss.zzzZ"));
    QDir root(AppPaths::dataRoot());
    const QString relative = QStringLiteral("production_calibration/t384/%1/%2")
                                 .arg(serial, stamp);
    if (!root.mkpath(relative)) {
        if (error) *error = tr("无法创建产测证据目录：%1").arg(root.filePath(relative));
        return false;
    }
    m_calibrationSessionPath = root.filePath(relative);
    if (m_calibrationArchive)
        m_calibrationArchive->setText(
            tr("证据目录：%1").arg(m_calibrationSessionPath));
    if (!packet.isEmpty()
        && !writeArchiveFile(QStringLiteral("candidate.packet"), packet, error))
        return false;
    if (!evidence.isEmpty()
        && !writeArchiveFile(QStringLiteral("evidence.json"), evidence, error))
        return false;
    appendLog(QStringLiteral("info"),
              tr("产测证据目录：%1").arg(m_calibrationSessionPath));
    return true;
}

bool T384Workspace::writeArchiveFile(const QString &name,
                                     const QByteArray &data,
                                     QString *error) const
{
    if (m_calibrationSessionPath.isEmpty()) {
        if (error) *error = tr("产测证据目录尚未建立");
        return false;
    }
    QSaveFile file(QDir(m_calibrationSessionPath).filePath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()
        || !file.commit()) {
        if (error) *error = tr("无法原子保存产测证据：%1").arg(file.fileName());
        return false;
    }
    return true;
}

QString T384Workspace::currentProfile() const
{
    const QString width = m_lastDiagnostics
                              .value(QStringLiteral("dvp.expected_width"))
                              .toString();
    const QString height = m_lastDiagnostics
                               .value(QStringLiteral("dvp.expected_height"))
                               .toString();
    const QString profile = width + QLatin1Char('x') + height;
    return profileFromName(profile.toLatin1()) ? profile : QString{};
}

void T384Workspace::handlePackets(const PacketList &packets)
{
    const ImagePacket *rawFrame = nullptr;
    quint32 frameSequence = 0;
    quint32 captureMs = 0;
    for (const Packet &packet : packets) {
        if (const auto *image = std::get_if<ImagePacket>(&packet);
            image && image->source == QString::fromLatin1(T384Raw16Source)) {
            rawFrame = image;
        } else if (const auto *sample = std::get_if<SamplePacket>(&packet);
                   sample && sample->source == QString::fromLatin1(T384MonitorSource)
                   && sample->values.size() >= 4) {
            frameSequence = quint32(sample->values[2]);
            captureMs = quint32(sample->values[3]);
        }
    }
    for (const Packet &packet : packets) {
        if (const auto *image = std::get_if<ImagePacket>(&packet)) {
            if (image->source == QString::fromLatin1(T384MonitorSource)) {
                for (const auto &view : m_thermals) if (view) view->setFrame(image->pixels, image->width, image->height);
            } else if (image->source == QString::fromLatin1(T384Raw16Source)) {
                ++m_framesReceived;
                if (m_traffic) {
                    m_traffic->setText(tr("接收：%1 B · 完整帧：%2")
                                            .arg(m_bytesReceived)
                                            .arg(m_framesReceived));
                }
            }
        } else if (const auto *sample = std::get_if<SamplePacket>(&packet)) {
            const double temp = sample->values.value(0, std::numeric_limits<double>::quiet_NaN());
            const double raw = sample->values.value(1, 0.0);
            if (sample->source != QString::fromLatin1(T384MonitorSource)
                || sample->values.size() < 4) continue;
            if (m_firstSampleTimestampUs <= 0)
                m_firstSampleTimestampUs = sample->timestampUs;
            const quint32 sequence = quint32(sample->values[2]);
            if (m_lastFrameSequence != 0 && sequence > m_lastFrameSequence + 1)
                m_hostSequenceGaps += sequence - m_lastFrameSequence - 1;
            m_lastFrameSequence = sequence;
            m_lastCenterTemperature = temp;
            m_lastCenterRaw = raw;
            TrendSample trend;
            trend.seconds = qMax<qint64>(0, sample->timestampUs
                                             - m_firstSampleTimestampUs) / 1e6;
            trend.centerPoint = temp;
            trend.centerRoi = temp;
            trend.centerPointAdu = raw;
            trend.centerRoiAdu = raw;
            m_trendHistory.append(trend);
            for (const auto &widget : m_trends) if (widget) widget->addSample(trend);
            updateInspector();
        }
    }
    if (rawFrame) {
        m_calibration->ingestFrame(rawFrame->pixels, rawFrame->width,
                                   rawFrame->height, frameSequence, captureMs);
    }
}

void T384Workspace::updateInspector()
{
    const auto value = [this](const QString &key,
                              const QString &fallback = QStringLiteral("—")) {
        const QString text = m_lastDiagnostics.value(key).toString();
        return text.isEmpty() ? fallback : text;
    };
    const auto fps = [this](const QString &key) {
        const double raw = m_lastDiagnostics.value(key).toDouble();
        return raw > 0.0 ? QStringLiteral("%1 FPS").arg(raw / 1000.0, 0, 'f', 2)
                         : QStringLiteral("—");
    };
    if (m_inspectorIdentity) {
        m_inspectorIdentity->setText(
            tr("%1 / %2 / %3").arg(value(QStringLiteral("mini2.pn")),
                                     value(QStringLiteral("mini2.sn")),
                                     value(QStringLiteral("mini2.firmware_version"))));
    }
    if (m_inspectorProfile) {
        m_inspectorProfile->setText(
            tr("%1×%2 / %3").arg(value(QStringLiteral("dvp.expected_width")),
                                   value(QStringLiteral("dvp.expected_height")),
                                   value(QStringLiteral("dvp.y16_input"),
                                         value(QStringLiteral("source.frame_mode")))));
    }
    if (m_inspectorTemperature) {
        m_inspectorTemperature->setText(std::isfinite(m_lastCenterTemperature)
            ? tr("%1 °C").arg(m_lastCenterTemperature, 0, 'f', 2)
            : QStringLiteral("—"));
    }
    if (m_inspectorRaw) {
        m_inspectorRaw->setText(std::isfinite(m_lastCenterRaw)
            ? QString::number(m_lastCenterRaw, 'f', 2) : QStringLiteral("—"));
    }
    if (m_inspectorSourceFps)
        m_inspectorSourceFps->setText(fps(QStringLiteral("source.fps_x1000")));
    if (m_inspectorStreamFps)
        m_inspectorStreamFps->setText(fps(QStringLiteral("stream.fps_x1000")));
    if (m_inspectorCompleteFrames)
        m_inspectorCompleteFrames->setText(
            tr("%1 / %2 MB").arg(m_framesReceived)
                              .arg(m_bytesReceived / 1000000.0, 0, 'f', 1));
    if (m_inspectorSequenceGaps)
        m_inspectorSequenceGaps->setText(QString::number(m_hostSequenceGaps));
    if (m_inspectorLink)
        m_inspectorLink->setText(m_status ? m_status->text() : QStringLiteral("—"));
}

void T384Workspace::updateCalibrationResult(const QJsonObject &result,
                                            const QString &detail,
                                            const bool success,
                                            const bool uncertain)
{
    if (!m_calibrationResult) return;
    const QString outcome = success
        ? tr("写入并回读通过（experimental / non-OEM）")
        : uncertain ? tr("WRITE UNCERTAIN：只能只读核验")
                    : tr("尚未形成写入通过结论");
    const QString profile = result.value(QStringLiteral("profile"))
                                .toString(currentProfile().isEmpty()
                                              ? QStringLiteral("—")
                                              : currentProfile());
    const QString generation = result.contains(QStringLiteral("generation"))
        ? result.value(QStringLiteral("generation")).toVariant().toString()
        : QStringLiteral("—");
    const QString applied = result.contains(QStringLiteral("applied"))
        ? (result.value(QStringLiteral("applied")).toBool()
               ? tr("是") : tr("否"))
        : QStringLiteral("—");
    m_calibrationResult->setText(
        tr("%1\n%2\nProfile：%3 · Generation：%4 · Applied：%5\n"
           "没有独立温度点误差门限时，不标记精度产测PASS。")
            .arg(outcome, detail, profile, generation, applied));
    if (success || uncertain) {
        m_outputTabs->setCurrentIndex(1);
        m_outputStack->setCurrentIndex(1);
    }
}

void T384Workspace::exportTrendPng()
{
    if (!m_outputTrend || m_outputTrend->historySampleCount() == 0) return;
    QDir root(AppPaths::dataRoot());
    root.mkpath(QStringLiteral("logs"));
    QString path = QFileDialog::getSaveFileName(
        this, tr("导出T384趋势PNG"),
        root.filePath(QStringLiteral("logs/t384_trend_%1.png")
                          .arg(QDateTime::currentDateTime()
                                   .toString(QStringLiteral("yyyyMMdd_HHmmss")))),
        tr("PNG图像 (*.png)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".png");
    QSaveFile file(path);
    const QImage image = m_outputTrend->renderHistoryImage();
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")
        || !file.commit()) {
        appendLog(QStringLiteral("error"), tr("趋势PNG导出失败：%1").arg(path));
        return;
    }
    appendLog(QStringLiteral("info"), tr("趋势PNG已导出：%1").arg(path));
}

void T384Workspace::exportTrendCsv()
{
    if (m_trendHistory.isEmpty()) return;
    QDir root(AppPaths::dataRoot());
    root.mkpath(QStringLiteral("logs"));
    QString path = QFileDialog::getSaveFileName(
        this, tr("导出T384趋势CSV"),
        root.filePath(QStringLiteral("logs/t384_trend_%1.csv")
                          .arg(QDateTime::currentDateTime()
                                   .toString(QStringLiteral("yyyyMMdd_HHmmss")))),
        tr("CSV文件 (*.csv)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".csv");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        appendLog(QStringLiteral("error"), tr("趋势CSV导出失败：%1").arg(path));
        return;
    }
    const QByteArray header = "\xef\xbb\xbft_s,center_c,center_y16\n";
    if (file.write(header) != header.size()) {
        file.cancelWriting();
        appendLog(QStringLiteral("error"), tr("趋势CSV写入失败：%1").arg(path));
        return;
    }
    for (const TrendSample &sample : std::as_const(m_trendHistory)) {
        QByteArray row = QByteArray::number(sample.seconds, 'f', 6) + ','
            + (std::isfinite(sample.centerRoi)
                   ? QByteArray::number(sample.centerRoi, 'f', 6) : QByteArray{})
            + ',' + QByteArray::number(sample.centerRoiAdu, 'f', 6) + '\n';
        if (file.write(row) != row.size()) {
            file.cancelWriting();
            appendLog(QStringLiteral("error"), tr("趋势CSV写入失败：%1").arg(path));
            return;
        }
    }
    if (!file.commit()) {
        appendLog(QStringLiteral("error"), tr("趋势CSV提交失败：%1").arg(path));
        return;
    }
    appendLog(QStringLiteral("info"), tr("趋势CSV已导出：%1").arg(path));
}

void T384Workspace::appendLog(const QString &level, const QString &message)
{
    const QString line = QStringLiteral("[%1] %2")
                              .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), message);
    if (m_log) m_log->appendPlainText(line);
    if (m_host) m_host->log(level, message);
}

void T384Workspace::updateDiagnostics(const QVariantMap &values)
{
    m_lastDiagnostics = values;
    const auto text = [&values](const QString &key,
                                const QString &fallback = QStringLiteral("—")) {
        const QString value = values.value(key).toString();
        return value.isEmpty() ? fallback : value;
    };
    const auto fps = [&values](const QString &key) {
        const double value = values.value(key).toDouble();
        return value > 0.0 ? QString::number(value / 1000.0, 'f', 2) : QStringLiteral("—");
    };
    if (m_identity) {
        m_identity->setText(tr("设备：PN=%1 · SN=%2 · FW=%3")
                                .arg(text(QStringLiteral("mini2.pn")),
                                     text(QStringLiteral("mini2.sn")),
                                     text(QStringLiteral("mini2.firmware_version"))));
    }
    if (m_format) {
        const QString dataDomain = text(QStringLiteral("dvp.y16_input"),
                                        text(QStringLiteral("source.frame_mode")));
        m_format->setText(tr("格式：%1 · %2×%3 · 源 %4 FPS · 流 %5 FPS")
                              .arg(dataDomain)
                              .arg(text(QStringLiteral("dvp.expected_width")))
                              .arg(text(QStringLiteral("dvp.expected_height")))
                              .arg(fps(QStringLiteral("source.fps_x1000")))
                              .arg(fps(QStringLiteral("stream.fps_x1000"))));
    }
    if (m_pipeline) {
        m_pipeline->setText(tr("Pipeline：队列 %1/%2 · 完成 %3 · 中止 %4 · 无槽 %5")
                                .arg(text(QStringLiteral("pipeline.queued_chunks")))
                                .arg(text(QStringLiteral("pipeline.high_water_chunks")))
                                .arg(text(QStringLiteral("pipeline.frames_completed")))
                                .arg(text(QStringLiteral("pipeline.frames_aborted")))
                                .arg(text(QStringLiteral("pipeline.acquire_no_slot"))));
    }
    if (m_stream) {
        m_stream->setText(tr("Stream：payload %1 B/s · backpressure %2 · TX drop %3 · HTTP 错误 %4")
                              .arg(text(QStringLiteral("stream.payload_bps")))
                              .arg(text(QStringLiteral("stream.backpressure")))
                              .arg(text(QStringLiteral("ncm.tx_drop")))
                              .arg(text(QStringLiteral("stream.write_errors"))));
    }
    updateInspector();
}

void T384Workspace::updateContext()
{
    if (!m_lastDiagnostics.isEmpty()) updateDiagnostics(m_lastDiagnostics);
}

} // namespace serein::scope::t384
