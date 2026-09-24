#include "Tn160Workspace.h"

#include "FirmwarePage.h"
#include "ProductionCalibrationPage.h"
#include "ThermalView.h"
#include "Tn160FirmwareProfileGenerator.h"
#include "Tn160Engine.h"
#include "TrendWidget.h"

#include <serein/scope/WorkspaceCanvas.h>

#include <serein/scope/AppPaths.h>

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QVariant>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStyle>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QVBoxLayout>

#include <cmath>
#include <utility>

namespace serein::scope::tn160ncm {
namespace {

struct SectionChrome {
    QFrame *frame = nullptr;
    QVBoxLayout *body = nullptr;
};

SectionChrome makeSection(QWidget *parent, const QString &objectName,
                          const QString &title, const bool showTitle = true)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(objectName);
    frame->setMinimumWidth(0);
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    if (showTitle) {
        auto *label = new QLabel(title, frame);
        label->setObjectName(QStringLiteral("metricGroupLabel"));
        label->setMinimumWidth(0);
        layout->addWidget(label);
    }
    return {frame, layout};
}

QFrame *makeMetric(QWidget *parent, const QString &name, QLabel **value,
                   const bool primary = false)
{
    auto *row = new QFrame(parent);
    row->setObjectName(primary ? QStringLiteral("metricPrimary")
                               : QStringLiteral("metricRow"));
    row->setFixedHeight(primary ? 38 : 28);
    row->setMinimumWidth(0);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(4, 0, 4, 0);
    auto *title = new QLabel(name, row);
    title->setObjectName(primary ? QStringLiteral("metricPrimaryLabel")
                                 : QStringLiteral("metricLabel"));
    title->setMinimumWidth(0);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    *value = new QLabel(primary ? QStringLiteral("--.-- °C")
                                : QStringLiteral("—"), row);
    (*value)->setObjectName(primary ? QStringLiteral("metricPrimaryValue")
                                    : QStringLiteral("metricValue"));
    (*value)->setMinimumWidth(0);
    (*value)->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(title, 1);
    layout->addWidget(*value);
    return row;
}

double sampleValue(const SamplePacket &sample, const qsizetype index,
                   const double fallback = 0.0)
{
    return index >= 0 && index < sample.values.size()
        ? sample.values.at(index) : fallback;
}

int sampleInteger(const SamplePacket &sample, const qsizetype index,
                  const int fallback = 0)
{
    const double value = sampleValue(sample, index, double(fallback));
    return std::isfinite(value) ? int(std::llround(value)) : fallback;
}

QString temperatureText(const double value)
{
    return std::isfinite(value)
        ? QStringLiteral("%1 °C").arg(value, 0, 'f', 2)
        : QStringLiteral("—");
}

QString diagnosticExportPath(const QString &extension)
{
    QString rootPath = serein::scope::AppPaths::dataRoot();
    if (rootPath.isEmpty()) {
        rootPath = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
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
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty()) {
        path += QLatin1Char('.') + extension;
    }
    return path;
}

QByteArray csvNumber(const double value)
{
    return std::isfinite(value) ? QByteArray::number(value, 'g', 12)
                                : QByteArray{};
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

bool saveDiagnosticCsv(const QString &path,
                       const QVector<SamplePacket> &history,
                       QString *error)
{
    static const char *const ValueColumns[] = {
        "center_roi_c", "ntc_c", "center_roi_adu", "vtemp_adu",
        "center_raw_c", "center_raw_adu", "ntc_adu", "ntc_ref_adu",
        "t_low_c", "t_high_c", "smooth_low_adu", "smooth_high_adu",
        "mean_diff", "smooth_vtemp_adu", "nuc_decision", "nuc_count",
        "nuc_dg", "nuc_dv", "nuc_dn", "boot_ntc_adu", "boot_delta_adu",
        "warmup_comp_adu", "normal_comp_adu", "total_comp_adu",
        "anchor_adu", "effective_anchor_adu", "ntc_model_reported",
        "ntc_model_valid", "ntc_model_reference_adu", "ntc_model_k_q8",
        "ntc_model_limit_adu", "ntc_model_active", "startup_model_valid",
        "startup_model_active", "startup_ntc_gate_adu",
        "startup_amplitude_adu", "startup_tau_ds", "ffc_state",
        "row_ref_mean_adu", "row_ref_span_adu", "shutter_mean_adu",
        "row_ref_model_reported", "row_ref_model_valid",
        "row_ref_model_active", "row_ref_sample_valid",
        "row_ref_reference_adu", "row_ref_k_q8", "row_ref_limit_adu",
        "row_ref_comp_adu",
    };
    static_assert(sizeof(ValueColumns) / sizeof(ValueColumns[0])
                      == MonitorSampleCount,
                  "diagnostic CSV columns must match monitor values");

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QObject::tr("无法创建导出目录");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QByteArray header("host_timestamp_us,t_s");
    for (const char *column : ValueColumns) {
        header += ',';
        header += column;
    }
    header += '\n';
    if (file.write("\xef\xbb\xbf", 3) != 3
        || file.write(header) != header.size()) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }

    const qint64 firstTimestampUs = history.isEmpty()
        ? 0 : history.constFirst().timestampUs;
    for (const SamplePacket &sample : history) {
        QByteArray row = QByteArray::number(sample.timestampUs);
        row += ',';
        row += csvNumber((sample.timestampUs - firstTimestampUs) / 1000000.0);
        for (qsizetype index = 0; index < MonitorSampleCount; ++index) {
            row += ',';
            row += index < sample.values.size()
                ? csvNumber(sample.values.at(index)) : QByteArray{};
        }
        row += '\n';
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

} // namespace

Tn160Workspace::Tn160Workspace(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host)
{
    setObjectName(QStringLiteral("instrumentWorkspace"));
    setProperty("shellRole", QStringLiteral("workspace-content"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 6, 0, 0);
    root->setSpacing(0);

    // These two existing controllers remain the sole owners of UF2 and
    // production-calibration behavior. This workspace only presents their
    // public state and real action widgets as one single-device surface.
    m_firmware = new FirmwarePage(host, this);
    m_firmware->hide();
    m_calibration = new ProductionCalibrationPage(host, this);
    m_calibration->hide();

    m_centerStack = new QStackedWidget(this);
    m_centerStack->setObjectName(QStringLiteral("tn160UnifiedCenter"));
    m_centerStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_centerStack->addWidget(createLiveSurface());
    // The thermal view is the work surface from the moment the workflow opens.
    // Before the first frame it renders its own empty state; firmware controls
    // belong in the Context dock and must never displace the heatmap.
    m_centerStack->setCurrentIndex(0);
    root->addWidget(m_centerStack, 1);

    m_contextPanel = createUnifiedContext();
    m_inspectorPanel = createUnifiedInspector();
    m_outputPanel = createUnifiedOutput();
    m_uiProvider = new WorkspaceUiProvider(this);

    connect(m_firmware, &FirmwarePage::firmwareFlashSucceeded,
            this,
            [this](const QString &firmwarePath,
                   const QString &firmwareSha256,
                   const QString &bootselRoot) {
                if (!m_firmware->autoConnectAfterFlash()) return;
                m_calibration->notifyFirmwareFlashed(
                    firmwarePath, firmwareSha256, bootselRoot);
            });
    connect(m_calibration, &ProductionCalibrationPage::livePacketsDecoded,
            this, &Tn160Workspace::handleLivePackets);
    connect(m_calibration, &ProductionCalibrationPage::workflowStateChanged,
            this, &Tn160Workspace::applyWorkflowState);
    connect(m_calibration, &ProductionCalibrationPage::transferStateChanged,
            this, [this](const QString &state, const QString &text) {
                if (!m_connectionState) return;
                m_connectionState->setText(text);
                m_connectionState->setProperty("state", state);
                m_connectionState->style()->unpolish(m_connectionState);
                m_connectionState->style()->polish(m_connectionState);
            });
    m_calibration->prepareForSingleDevice();

    WorkspaceUiContext uiContext;
    uiContext.id = QStringLiteral("tn160/firmware-calibration");
    uiContext.layoutProfile = WorkspaceLayoutProfile::CalibrationTask;
    uiContext.contextPanel = {m_contextPanel, tr("固件标定"), nullptr, {}};
    uiContext.inspectorPanel = {m_inspectorPanel, tr("数据与属性"), nullptr, {}};
    uiContext.outputPanel = {m_outputPanel, tr("诊断输出"), nullptr, {}};
    uiContext.componentLibrary.items = {};
    WorkspaceUiPresentationOptions presentationOptions;
    presentationOptions.contextPanel.margins = WorkspacePanelMargins::Flush;
    presentationOptions.inspectorPanel.margins = WorkspacePanelMargins::Flush;
    presentationOptions.outputPanel.margins = WorkspacePanelMargins::Flush;
    presentationOptions.outputPanel.headerControl = m_outputTabs;
    m_uiProvider->setContext(std::move(uiContext), presentationOptions);

    if (m_host) {
        m_host->log(QStringLiteral("info"),
                    tr("TN160 USB NCM 固件标定工作区已加载"));
        m_host->setStatus(tr("TN160 · 固件标定"));
    }
}

QWidget *Tn160Workspace::createLiveSurface()
{
    m_liveCanvas = new WorkspaceCanvas(QStringLiteral("tn160-ncm/live-workbench-v1"), this);
    m_liveCanvas->setProperty("emptyTitle", tr("TN160 实时诊断"));
    m_liveCanvas->setProperty("emptyHint", tr("从组件库添加热图、关键测量或实时趋势"));
    m_thermal = new ThermalView;
    m_thermal->setObjectName(QStringLiteral("tn160ThermalView"));
    m_thermal->setCrosshairVisible(true);
    m_thermal->setExtremaVisible(true);
    m_componentThermals.append(m_thermal);
    m_liveCanvas->addComponent(QStringLiteral("tn160.thermal"), tr("TN160 热图"),
                               m_thermal, QRectF(0.02, 0.02, 0.96, 0.92),
                               QSize(380, 220));
    return m_liveCanvas;
}

QWidget *Tn160Workspace::createUnifiedContext()
{
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("tn160UnifiedContext"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(0);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("tn160UnifiedContextBody"));
    body->setMinimumWidth(0);
    body->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    SectionChrome firmware = makeSection(
        body, QStringLiteral("tn160UnifiedFirmwareSection"), tr("UF2 固件"), false);
    QWidget *firmwareContext = m_firmware->contextPanel();
    firmwareContext->setMinimumWidth(0);
    firmwareContext->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    if (firmwareContext->layout()) {
        firmwareContext->layout()->setContentsMargins(0, 0, 0, 0);
    }
    if (auto *firmwareHint = firmwareContext->findChild<QLabel *>(
            QStringLiteral("tn160FirmwareHint"))) {
        firmwareHint->hide();
    }
    firmware.body->addWidget(firmwareContext);
    layout->addWidget(firmware.frame);

    SectionChrome connection = makeSection(
        body, QStringLiteral("tn160UnifiedConnectionSection"), tr("设备与连接"), false);
    auto *endpointLabel = new QLabel(tr("USB NCM 地址"), connection.frame);
    endpointLabel->setObjectName(QStringLiteral("metricLabel"));
    connection.body->addWidget(endpointLabel);
    m_endpointEdit = new QComboBox(connection.frame);
    m_endpointEdit->setObjectName(QStringLiteral("tn160UnifiedEndpoint"));
    m_endpointEdit->setEditable(true);
    m_endpointEdit->addItem(QStringLiteral("http://192.168.17.1"));
    m_endpointEdit->setCurrentText(QStringLiteral("http://192.168.17.1"));
    m_endpointEdit->lineEdit()->setPlaceholderText(
        tr("192.168.17.1 或 http://地址"));
    m_endpointEdit->lineEdit()->setClearButtonEnabled(true);
    m_endpointEdit->setToolTip(tr("可输入要连接的 TN160 USB NCM 地址"));
    m_endpointEdit->setMinimumWidth(0);
    m_endpointEdit->setMaximumWidth(QWIDGETSIZE_MAX);
    m_endpointEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connection.body->addWidget(m_endpointEdit);
    auto *actions = new QHBoxLayout;
    m_scanButton = new QPushButton(tr("扫描设备"), connection.frame);
    m_scanButton->setObjectName(QStringLiteral("tn160UnifiedScan"));
    m_connectButton = new QPushButton(tr("连接"), connection.frame);
    m_connectButton->setObjectName(QStringLiteral("tn160UnifiedConnect"));
    m_connectButton->setProperty("kind", QStringLiteral("primary"));
    m_scanButton->setMinimumWidth(0);
    m_scanButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_connectButton->setMinimumWidth(0);
    m_connectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    actions->addWidget(m_scanButton);
    actions->addWidget(m_connectButton, 1);
    connection.body->addLayout(actions);
    m_connectionState = new QLabel(tr("等待固件或设备"), connection.frame);
    m_connectionState->setObjectName(QStringLiteral("statusPill"));
    m_connectionState->setMinimumWidth(0);
    connection.body->addWidget(m_connectionState);
    connect(m_scanButton, &QPushButton::clicked,
            m_calibration, &ProductionCalibrationPage::scanAndConnect);
    connect(m_endpointEdit, &QComboBox::editTextChanged, this,
            [this](const QString &endpoint) { m_calibration->setPort(endpoint); });
    connect(m_connectButton, &QPushButton::clicked,
            m_calibration, &ProductionCalibrationPage::toggleConnection);
    layout->addWidget(connection.frame);

    SectionChrome calibration = makeSection(
        body, QStringLiteral("tn160UnifiedProductionSection"), tr("生产标定"), false);
    auto *stepLayout = new QGridLayout;
    stepLayout->setContentsMargins(0, 0, 0, 0);
    stepLayout->setHorizontalSpacing(6);
    stepLayout->setVerticalSpacing(6);
    const QStringList captions{
        tr("1、对准 0°C"), tr("2、对准 50°C"),
        tr("3、保持 50°C"), tr("4、回到 0°C"),
    };
    for (int index = 0; index < captions.size(); ++index) {
        auto *button = m_calibration->findChild<QPushButton *>(
            QStringLiteral("tn160ProductionStep%1").arg(index + 1));
        if (!button) continue;
        button->setText(captions.at(index));
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        stepLayout->addWidget(button, index / 2, index % 2);
    }
    stepLayout->setColumnStretch(0, 1);
    stepLayout->setColumnStretch(1, 1);
    calibration.body->addLayout(stepLayout);
    const QStringList recoveryActions{
        QStringLiteral("tn160ProductionRestart"),
        QStringLiteral("tn160ProductionWrite"),
        QStringLiteral("tn160ProductionWriteTest"),
        QStringLiteral("tn160ProductionRetryVerification"),
    };
    for (const QString &objectName : recoveryActions) {
        if (auto *button = m_calibration->findChild<QPushButton *>(objectName)) {
            button->setMinimumWidth(0);
            button->setMaximumWidth(QWIDGETSIZE_MAX);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            calibration.body->addWidget(button);
        }
    }
    layout->addWidget(calibration.frame);

    SectionChrome profile = makeSection(
        body, QStringLiteral("tn160FirmwareProfileSection"),
        tr("定制固件参数"), false);
    auto *profileButton = new QPushButton(tr("生成固件补偿参数"), profile.frame);
    profileButton->setObjectName(QStringLiteral("tn160GenerateFirmwareProfile"));
    profileButton->setProperty("kind", QStringLiteral("secondary"));
    profileButton->setToolTip(
        tr("导入固定 50°C 冷启动 CSV，离线生成 C++ 补偿参数；"
           "不会写入设备或改动生产标定。"));
    profileButton->setMinimumWidth(0);
    profileButton->setMaximumWidth(QWIDGETSIZE_MAX);
    profileButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    profile.body->addWidget(profileButton);
    connect(profileButton, &QPushButton::clicked,
            this, &Tn160Workspace::generateFirmwareProfile);
    layout->addWidget(profile.frame);

    SectionChrome records = makeSection(
        body, QStringLiteral("tn160UnifiedRecordsSection"), tr("生成记录"), false);
    QWidget *log = m_calibration->contextPanel();
    if (log->layout()) log->layout()->setContentsMargins(0, 0, 0, 0);
    log->setMinimumHeight(160);
    records.body->addWidget(log);
    layout->addWidget(records.frame);
    layout->addStretch(1);

    scroll->setWidget(body);
    return scroll;
}

QWidget *Tn160Workspace::createUnifiedInspector()
{
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("tn160UnifiedInspector"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(0);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *body = new QWidget(scroll);
    body->setObjectName(QStringLiteral("tn160UnifiedInspectorBody"));
    body->setMinimumWidth(0);
    body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    SectionChrome identity = makeSection(
        body, QStringLiteral("tn160UnifiedIdentitySection"), tr("设备"));
    m_deviceIdentity = new QLabel(tr("等待设备 · 192.168.17.1"), identity.frame);
    m_deviceIdentity->setObjectName(QStringLiteral("tn160UnifiedDeviceIdentity"));
    m_deviceIdentity->setWordWrap(true);
    m_deviceIdentity->setTextInteractionFlags(Qt::TextSelectableByMouse
                                               | Qt::TextSelectableByKeyboard);
    identity.body->addWidget(m_deviceIdentity);
    layout->addWidget(identity.frame);

    SectionChrome metrics = makeSection(
        body, QStringLiteral("tn160UnifiedMetricsSection"), tr("实时数据"));
    metrics.body->addWidget(makeMetric(metrics.frame, QStringLiteral("Center 5×5"),
                                       &m_centerMetric, true));
    metrics.body->addWidget(makeMetric(metrics.frame, QStringLiteral("NTC"),
                                       &m_ntcMetric));
    metrics.body->addWidget(makeMetric(metrics.frame, QStringLiteral("△NTC"),
                                       &m_deltaNtcMetric));
    metrics.body->addWidget(makeMetric(metrics.frame, QStringLiteral("Center ADU"),
                                       &m_centerAduMetric));
    metrics.body->addWidget(makeMetric(metrics.frame, QStringLiteral("VTEMP ADU"),
                                       &m_vtempMetric));
    metrics.body->addWidget(makeMetric(metrics.frame, tr("帧率"), &m_fpsMetric));
    layout->addWidget(metrics.frame);

    SectionChrome hint = makeSection(
        body, QStringLiteral("tn160UnifiedInspectorHint"), tr("采集说明"));
    auto *text = new QLabel(
        tr("热图在中央主视图显示；温度、原始量和帧率在这里实时更新。"),
        hint.frame);
    text->setObjectName(QStringLiteral("pageSubtitle"));
    text->setWordWrap(true);
    hint.body->addWidget(text);
    layout->addWidget(hint.frame);
    layout->addStretch(1);

    scroll->setWidget(body);
    return scroll;
}

QWidget *Tn160Workspace::createUnifiedOutput()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("tn160UnifiedOutput"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_outputTabs = new QTabBar(panel);
    m_outputTabs->setObjectName(QStringLiteral("tn160UnifiedOutputTabs"));
    m_outputTabs->setDrawBase(false);
    m_outputTabs->setExpanding(false);
    m_outputTabs->addTab(tr("实时趋势"));
    m_outputTabs->addTab(tr("标定结果"));
    layout->addWidget(m_outputTabs);
    m_outputStack = new QStackedWidget(panel);
    m_outputStack->setObjectName(QStringLiteral("tn160UnifiedOutputStack"));
    m_outputStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_trend = new TrendWidget(m_outputStack);
    m_trend->setObjectName(QStringLiteral("tn160TrendWidget"));
    // The standalone trend widget keeps a 220 px editing minimum. Inside the
    // dock, host chrome and tabs sit above it, so the standalone minimum would
    // otherwise prevent the host from applying its normal dock split.
    m_trend->setMinimumHeight(0);
    m_trend->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_componentTrends.append(m_trend);
    connect(m_trend, &TrendWidget::exportPngRequested,
            this, &Tn160Workspace::exportTrendPng);
    connect(m_trend, &TrendWidget::exportCsvRequested,
            this, &Tn160Workspace::exportDiagnosticCsv);
    m_outputStack->addWidget(m_trend);
    auto *resultsPage = new QWidget(m_outputStack);
    resultsPage->setObjectName(QStringLiteral("tn160CalibrationResultsPage"));
    resultsPage->setMinimumSize(0, 0);
    resultsPage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *resultsLayout = new QVBoxLayout(resultsPage);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(0);

    auto *resultsToolbar = new QFrame(resultsPage);
    resultsToolbar->setObjectName(QStringLiteral("trendToolbar"));
    resultsToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *resultsToolbarLayout = new QHBoxLayout(resultsToolbar);
    resultsToolbarLayout->setContentsMargins(8, 3, 8, 3);
    resultsToolbarLayout->setSpacing(4);
    const QStringList packageActions{
        QStringLiteral("tn160ProductionImport"),
        QStringLiteral("tn160ProductionExport"),
    };
    for (const QString &objectName : packageActions) {
        if (auto *button = m_calibration->findChild<QPushButton *>(objectName)) {
            button->setMinimumWidth(0);
            button->setMaximumWidth(QWIDGETSIZE_MAX);
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            resultsToolbarLayout->addWidget(button);
        }
    }
    resultsToolbarLayout->addStretch(1);
    resultsLayout->addWidget(resultsToolbar);

    QWidget *results = m_calibration->outputPanel();
    results->setMinimumSize(0, 0);
    results->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    // QStackedWidget includes hidden pages in its minimum-size calculation.
    // Production previews used a hard 160x105 minimum, which forced the
    // entire Output dock above the host's normal split even while the trend
    // page was selected. In this unified, resizable dock the views
    // must yield to the operator's split and grow again when Output is enlarged.
    for (ThermalView *preview : results->findChildren<ThermalView *>()) {
        preview->setMinimumSize(0, 0);
        preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    }
    resultsLayout->addWidget(results, 1);
    m_outputStack->addWidget(resultsPage);
    layout->addWidget(m_outputStack, 1);
    connect(m_outputTabs, &QTabBar::currentChanged,
            m_outputStack, &QStackedWidget::setCurrentIndex);
    m_outputTabs->setCurrentIndex(0);
    m_outputStack->setCurrentIndex(0);
    return panel;
}

void Tn160Workspace::handleLivePackets(const PacketList &packets)
{
    const SamplePacket *sample = nullptr;
    for (const Packet &packet : packets) {
        if (const auto *image = std::get_if<ImagePacket>(&packet);
            image && image->source == QString::fromLatin1(Tn160MonitorSource)
            && image->pixelFormat == QStringLiteral("gray8")
            && image->width == FrameWidth && image->height == FrameHeight
            && image->pixels.size() == FrameWidth * FrameHeight) {
            for (const auto &thermal : std::as_const(m_componentThermals)) {
                if (thermal) thermal->setFrame(image->pixels, image->width, image->height);
            }
            revealLiveSurface();
        } else if (const auto *decoded = std::get_if<SamplePacket>(&packet);
                   decoded
                   && decoded->source
                          == QString::fromLatin1(Tn160MonitorSource)) {
            sample = decoded;
        }
    }
    if (!sample || sample->values.size() < MonitorSampleCount) return;
    const int incomingNucCount = sampleInteger(*sample, MonitorNucCount, -1);
    if (!m_diagnosticHistory.isEmpty() && incomingNucCount >= 0) {
        const int previousNucCount = sampleInteger(
            m_diagnosticHistory.constLast(), MonitorNucCount, -1);
        if (previousNucCount >= 3
            && incomingNucCount + 1 < previousNucCount) {
            /* A reboot/reconnect can keep the same serial.  Do not export two
             * thermal sessions as one continuous trend. */
            m_diagnosticHistory.clear();
            for (const auto &trend : std::as_const(m_componentTrends)) {
                if (trend) trend->clear();
            }
            m_firstSampleTimestampUs = 0;
            m_previousSampleTimestampUs = 0;
        }
    }
    m_diagnosticHistory.append(*sample);
    if (m_firstSampleTimestampUs <= 0) {
        m_firstSampleTimestampUs = sample->timestampUs;
    }
    const double seconds = qMax<qint64>(
        0, sample->timestampUs - m_firstSampleTimestampUs) / 1000000.0;
    const double deltaSeconds = m_previousSampleTimestampUs > 0
        ? (sample->timestampUs - m_previousSampleTimestampUs) / 1000000.0
        : 0.0;
    m_previousSampleTimestampUs = sample->timestampUs;

    TrendSample trend;
    trend.seconds = seconds;
    trend.centerPoint = sampleValue(*sample, MonitorCenterRawC);
    trend.centerRoi = sampleValue(*sample, MonitorCenterRoiC);
    trend.ntc = sampleValue(*sample, MonitorNtcC);
    trend.centerPointAdu = sampleValue(*sample, MonitorCenterRawAdu);
    trend.centerRoiAdu = sampleValue(*sample, MonitorCenterRoiAdu);
    trend.vtempRaw = sampleValue(*sample, MonitorVtemp);
    trend.vtempSmooth = sampleValue(*sample, MonitorSmoothVtempAdu);
    trend.nucDecision = sampleInteger(*sample, MonitorNucDecision, 255);
    trend.nucCount = sampleInteger(*sample, MonitorNucCount);
    trend.nucDg = sampleInteger(*sample, MonitorNucDg);
    trend.nucDv = sampleInteger(*sample, MonitorNucDv);
    trend.nucDn = sampleInteger(*sample, MonitorNucDn);
    for (const auto &trendView : std::as_const(m_componentTrends)) {
        if (trendView) trendView->addSample(trend);
    }

    ThermalFrameInfo info;
    info.centerPointC = trend.centerPoint;
    info.centerRoiC = trend.centerRoi;
    info.centerPointAdu = trend.centerPointAdu;
    info.centerRoiAdu = trend.centerRoiAdu;
    info.ntcC = trend.ntc;
    info.tLowC = sampleValue(*sample, MonitorTLowC);
    info.tHighC = sampleValue(*sample, MonitorTHighC);
    info.vtemp = trend.vtempRaw;
    info.fps = deltaSeconds > 0.0 ? 1.0 / deltaSeconds : 0.0;
    for (const auto &thermal : std::as_const(m_componentThermals)) {
        if (thermal) thermal->setFrameInfo(info);
    }

    m_centerMetric->setText(temperatureText(trend.centerRoi));
    m_ntcMetric->setText(temperatureText(trend.ntc));
    m_centerAduMetric->setText(
        QString::number(std::llround(trend.centerRoiAdu)));
    m_vtempMetric->setText(QString::number(std::llround(trend.vtempRaw)));
    m_fpsMetric->setText(deltaSeconds > 0.0
        ? QStringLiteral("%1 FPS").arg(1.0 / deltaSeconds, 0, 'f', 1)
        : QStringLiteral("—"));
    emit telemetryChanged(temperatureText(trend.centerRoi),
                          temperatureText(trend.ntc),
                          QString::number(std::llround(trend.centerRoiAdu)),
                          QString::number(std::llround(trend.vtempRaw)),
                          deltaSeconds > 0.0
                              ? QStringLiteral("%1 FPS").arg(1.0 / deltaSeconds, 0, 'f', 1)
                              : QStringLiteral("—"));
}

void Tn160Workspace::applyWorkflowState(const QVariantMap &state)
{
    const bool connected = state.value(QStringLiteral("connected")).toBool();
    const QString workflow = state.value(QStringLiteral("state")).toString();
    const QString serial = state.value(QStringLiteral("serial")).toString();
    if (!serial.trimmed().isEmpty() && serial != m_historySerial) {
        m_historySerial = serial;
        m_diagnosticHistory.clear();
        for (const auto &trend : std::as_const(m_componentTrends)) {
            if (trend) trend->clear();
        }
        m_firstSampleTimestampUs = 0;
        m_previousSampleTimestampUs = 0;
    }
    const bool waitingForNextDevice = workflow.isEmpty()
        || workflow == QStringLiteral("waiting-device")
        || workflow == QStringLiteral("disconnected");
    if (m_firmware) m_firmware->setNextDeviceBlocked(!waitingForNextDevice);
    if (m_connectButton) {
        m_connectButton->setText(connected ? tr("断开")
            : workflow.contains(QStringLiteral("connecting"))
                || workflow == QStringLiteral("waiting-reboot")
              ? tr("取消连接") : tr("连接"));
        m_connectButton->setEnabled(
            state.value(QStringLiteral("connection_action_enabled"), true)
                .toBool());
    }
    if (m_scanButton) {
        m_scanButton->setEnabled(
            state.value(QStringLiteral("scan_action_enabled"), true).toBool());
    }
    if (m_endpointEdit) {
        m_endpointEdit->setEnabled(
            state.value(QStringLiteral("endpoint_edit_enabled"), true).toBool());
    }
    if (m_deviceIdentity) {
        m_deviceIdentity->setText(serial.trimmed().isEmpty()
            ? tr("设备身份 · 等待 192.168.17.1")
            : tr("设备身份 · %1 · TN160").arg(serial.trimmed()));
    }
    const QString endpoint = state.value(QStringLiteral("endpoint")).toString().trimmed();
    if (m_endpointEdit && !endpoint.isEmpty()
        && !m_endpointEdit->lineEdit()->hasFocus()
        && m_endpointEdit->currentText() != endpoint) {
        m_endpointEdit->setEditText(endpoint);
    }
    const QVariant delta = state.value(QStringLiteral("delta_ntc_c"));
    const double deltaNtc = delta.toDouble();
    if (m_deltaNtcMetric) {
        m_deltaNtcMetric->setText(delta.isValid() && std::isfinite(deltaNtc)
            ? QStringLiteral("%1 °C").arg(deltaNtc, 0, 'f', 2)
            : QStringLiteral("—"));
    }
}

void Tn160Workspace::revealLiveSurface()
{
    if (m_centerStack) m_centerStack->setCurrentIndex(0);
}

void Tn160Workspace::exportTrendPng()
{
    if (!m_trend || m_trend->historySampleCount() <= 0) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("导出完整趋势 PNG"),
        diagnosticExportPath(QStringLiteral("png")),
        tr("PNG 图像 (*.png)"));
    if (path.isEmpty()) return;
    path = withExtension(path, QStringLiteral("png"));
    QString error;
    if (!savePngImage(path, m_trend->renderHistoryImage(), &error)) {
        if (m_host) {
            m_host->log(QStringLiteral("error"),
                        tr("TN160 趋势 PNG 导出失败：%1").arg(error));
            m_host->setStatus(tr("TN160 · PNG 导出失败"));
        }
        return;
    }
    if (m_host) {
        m_host->log(QStringLiteral("info"),
                    tr("TN160 趋势 PNG 已导出：%1").arg(path));
        m_host->setStatus(tr("TN160 · PNG 导出完成"));
    }
}

void Tn160Workspace::exportDiagnosticCsv()
{
    if (m_diagnosticHistory.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("导出完整诊断 CSV"),
        diagnosticExportPath(QStringLiteral("csv")),
        tr("CSV 数据 (*.csv)"));
    if (path.isEmpty()) return;
    path = withExtension(path, QStringLiteral("csv"));
    QString error;
    if (!saveDiagnosticCsv(path, m_diagnosticHistory, &error)) {
        if (m_host) {
            m_host->log(QStringLiteral("error"),
                        tr("TN160 诊断 CSV 导出失败：%1").arg(error));
            m_host->setStatus(tr("TN160 · CSV 导出失败"));
        }
        return;
    }
    if (m_host) {
        m_host->log(QStringLiteral("info"),
                    tr("TN160 诊断 CSV 已导出：%1").arg(path));
        m_host->setStatus(tr("TN160 · CSV 导出完成"));
    }
}

void Tn160Workspace::generateFirmwareProfile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("导入 TN160 冷启动 CSV"), diagnosticExportPath(QStringLiteral("csv")),
        tr("CSV 数据 (*.csv)"));
    if (path.isEmpty()) return;

    const FirmwareProfileDraft draft = generateFirmwareProfileFromCsv(path);
    if (!draft.valid) {
        QMessageBox::warning(this, tr("无法生成固件参数"), draft.error);
        if (m_host) {
            m_host->log(QStringLiteral("warning"),
                        tr("TN160 固件参数生成失败：%1").arg(draft.error));
        }
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("TN160 定制固件参数"));
    dialog->resize(760, 560);
    auto *layout = new QVBoxLayout(dialog);
    auto *summary = new QLabel(
        tr("样本 %1 · mean_diff %2 · anchor %3～%4\n"
           "预测（有效样本 t≥6 秒）：%5～%6°C，P-P %7°C，最大误差 %8°C\n"
           "这是离线 C++ 参数，不会写设备、不生成 UF2。")
            .arg(draft.sampleCount)
            .arg(draft.meanDiff, 0, 'f', 3)
            .arg(draft.anchorMinimum)
            .arg(draft.anchorMaximum)
            .arg(draft.predictedMinimumC, 0, 'f', 3)
            .arg(draft.predictedMaximumC, 0, 'f', 3)
            .arg(draft.predictedPeakToPeakC, 0, 'f', 3)
            .arg(draft.predictedMaximumAbsoluteErrorC, 0, 'f', 3),
        dialog);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *source = new QPlainTextEdit(dialog);
    source->setReadOnly(true);
    source->setPlainText(draft.cppSource);
    source->setObjectName(QStringLiteral("tn160FirmwareProfileSource"));
    layout->addWidget(source, 1);

    auto *buttons = new QDialogButtonBox(dialog);
    auto *copy = buttons->addButton(tr("复制 C++"), QDialogButtonBox::ActionRole);
    auto *save = buttons->addButton(tr("保存 .inc"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(copy, &QPushButton::clicked, dialog, [&draft] {
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(draft.cppSource);
        }
    });
    connect(save, &QPushButton::clicked, dialog, [this, &draft, dialog] {
        QString output = QFileDialog::getSaveFileName(
            dialog, tr("保存固件参数片段"),
            QFileInfo(draft.sourcePath).dir().filePath(
                draft.identifier + QStringLiteral(".inc")),
            tr("C/C++ 片段 (*.inc *.h *.txt)"));
        if (output.isEmpty()) return;
        if (QFileInfo(output).suffix().isEmpty()) {
            output += QStringLiteral(".inc");
        }
        QSaveFile file(output);
        const QByteArray bytes = draft.cppSource.toUtf8();
        if (!file.open(QIODevice::WriteOnly)
            || file.write(bytes) != bytes.size() || !file.commit()) {
            QMessageBox::warning(dialog, tr("保存失败"), file.errorString());
            return;
        }
        if (m_host) {
            m_host->log(QStringLiteral("info"),
                        tr("TN160 固件参数已导出：%1").arg(output));
            m_host->setStatus(tr("TN160 · 固件参数已导出"));
        }
    });
    layout->addWidget(buttons);
    dialog->exec();
    dialog->deleteLater();
}

void Tn160Workspace::shutdown()
{
    if (m_firmware) m_firmware->shutdown();
    if (m_calibration) m_calibration->shutdown();
}

WorkspaceUiProvider *Tn160Workspace::workspaceUiProvider()
{
    return m_uiProvider;
}

QList<WorkspaceComponentDefinition> Tn160Workspace::componentDefinitions() const
{
    const auto common = WorkspaceComponentCapability::Movable
        | WorkspaceComponentCapability::Resizable
        | WorkspaceComponentCapability::Maximizable
        | WorkspaceComponentCapability::Cloneable;
    return {
        {QStringLiteral("tn160.thermal"), QStringLiteral("tn160-ncm"),
         tr("TN160 热图"), tr("实时诊断"), QStringLiteral(":/serein/thermal.svg"),
         QSize(640, 420), QSize(380, 220),
         common | WorkspaceComponentCapability::DataBinding,
         WorkspaceBindingKind::ImageChannel, {}},
        {QStringLiteral("tn160.measurements"), QStringLiteral("tn160-ncm"),
         tr("关键测量"), tr("实时诊断"), QString(), QSize(280, 220), QSize(220, 160),
         common | WorkspaceComponentCapability::DataBinding,
         WorkspaceBindingKind::SampleChannel, {}},
        {QStringLiteral("tn160.trend"), QStringLiteral("tn160-ncm"),
         tr("实时趋势"), tr("实时诊断"), QString(), QSize(640, 260), QSize(320, 160),
         common | WorkspaceComponentCapability::DataBinding,
         WorkspaceBindingKind::SampleChannel, {}},
    };
}

QWidget *Tn160Workspace::createComponent(const QString &definitionId,
                                         QWidget *parent)
{
    const QString id = definitionId.trimmed();
    if (id == QStringLiteral("tn160.thermal")) {
        auto *thermal = new ThermalView(parent);
        thermal->setObjectName(QStringLiteral("tn160NcmThermalComponent"));
        thermal->setCrosshairVisible(true);
        thermal->setExtremaVisible(true);
        m_componentThermals.append(thermal);
        return thermal;
    }
    if (id == QStringLiteral("tn160.trend")) {
        auto *trend = new TrendWidget(parent);
        trend->setObjectName(QStringLiteral("tn160NcmTrendComponent"));
        m_componentTrends.append(trend);
        return trend;
    }
    if (id == QStringLiteral("tn160.measurements")) {
        auto *panel = new QWidget(parent);
        auto *layout = new QVBoxLayout(panel);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(6);
        auto addMetric = [panel, layout](const QString &name, QLabel **value,
                                         const bool primary = false) {
            auto *row = new QFrame(panel);
            row->setObjectName(primary ? QStringLiteral("metricPrimary")
                                       : QStringLiteral("metricRow"));
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 0, 4, 0);
            auto *label = new QLabel(name, row);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            auto *result = new QLabel(primary ? QStringLiteral("--.-- °C")
                                              : QStringLiteral("—"), row);
            result->setMinimumWidth(54);
            result->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rowLayout->addWidget(label, 1);
            rowLayout->addWidget(result);
            layout->addWidget(row);
            *value = result;
        };
        QLabel *center = nullptr;
        QLabel *ntc = nullptr;
        QLabel *adu = nullptr;
        QLabel *vtemp = nullptr;
        QLabel *fps = nullptr;
        addMetric(QStringLiteral("Center 5×5"), &center, true);
        addMetric(QStringLiteral("NTC"), &ntc);
        addMetric(QStringLiteral("Center ADU"), &adu);
        addMetric(QStringLiteral("VTEMP ADU"), &vtemp);
        addMetric(tr("帧率"), &fps);
        layout->addStretch(1);
        connect(this, &Tn160Workspace::telemetryChanged, panel,
                [center, ntc, adu, vtemp, fps](const QString &c, const QString &n,
                                               const QString &a, const QString &v,
                                               const QString &f) {
                    if (center) center->setText(c);
                    if (ntc) ntc->setText(n);
                    if (adu) adu->setText(a);
                    if (vtemp) vtemp->setText(v);
                    if (fps) fps->setText(f);
                });
        return panel;
    }
    return nullptr;
}

int Tn160Workspace::currentPageIndex() const
{
    return 0;
}

} // namespace serein::scope::tn160ncm
