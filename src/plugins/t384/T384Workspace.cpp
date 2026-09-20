#include "T384Workspace.h"
#include "T384CalibrationPage.h"
#include "T384Engine.h"
#include "T384Transport.h"
#include "ThermalView.h"
#include "TrendWidget.h"

#include <serein/scope/WorkspaceCanvas.h>
#include <QtCore/QDateTime>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <cmath>
#include <limits>
#include <memory>

namespace serein::scope::t384 {

T384Workspace::T384Workspace(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host), m_uiProvider(new WorkspaceUiProvider(this)),
      m_engine(new T384Engine),
      m_transport(new T384Transport(std::unique_ptr<T384Engine>(m_engine), this)),
      m_calibration(new T384CalibrationPage(this))
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

    m_contextPanel = new QWidget(this);
    auto *context = new QVBoxLayout(m_contextPanel);
    context->setContentsMargins(0, 0, 0, 0);
    context->setSpacing(8);
    context->addWidget(new QLabel(tr("T256/T384/T640 USB NCM 实时监控"), m_contextPanel));
    m_endpoint = new QLineEdit(QStringLiteral("http://192.168.17.1"), m_contextPanel);
    m_endpoint->setObjectName(QStringLiteral("t384Endpoint"));
    context->addWidget(m_endpoint);
    auto *actions = new QHBoxLayout;
    m_connect = new QPushButton(tr("连接"), m_contextPanel);
    m_stop = new QPushButton(tr("停止"), m_contextPanel);
    actions->addWidget(m_connect);
    actions->addWidget(m_stop);
    context->addLayout(actions);
    m_status = new QLabel(tr("未连接"), m_contextPanel);
    m_status->setObjectName(QStringLiteral("statusPill"));
    context->addWidget(m_status);
    m_identity = new QLabel(tr("设备：—"), m_contextPanel);
    m_identity->setWordWrap(true);
    m_format = new QLabel(tr("格式：—"), m_contextPanel);
    m_format->setWordWrap(true);
    m_pipeline = new QLabel(tr("Pipeline：—"), m_contextPanel);
    m_pipeline->setWordWrap(true);
    m_stream = new QLabel(tr("Stream：—"), m_contextPanel);
    m_stream->setWordWrap(true);
    m_traffic = new QLabel(tr("接收：0 B · 完整帧：0"), m_contextPanel);
    m_traffic->setWordWrap(true);
    m_error = new QLabel(tr("错误：—"), m_contextPanel);
    m_error->setWordWrap(true);
    m_error->setObjectName(QStringLiteral("pageSubtitle"));
    context->addWidget(m_identity);
    context->addWidget(m_format);
    context->addWidget(m_pipeline);
    context->addWidget(m_stream);
    context->addWidget(m_traffic);
    context->addWidget(m_error);
    context->addWidget(m_calibration->contextPanel());
    context->addStretch();

    m_outputPanel = new QWidget(this);
    auto *outputLayout = new QVBoxLayout(m_outputPanel);
    outputLayout->setContentsMargins(8, 8, 8, 8);
    m_log = new QPlainTextEdit(m_outputPanel);
    m_log->setObjectName(QStringLiteral("t384OutputLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(200);
    outputLayout->addWidget(m_log);

    WorkspaceUiContext ui;
    ui.id = QStringLiteral("t384");
    ui.layoutProfile = WorkspaceLayoutProfile::Diagnostics;
    ui.contextPanel = {m_contextPanel, tr("T256/T384/T640 设备与标定"), nullptr, {}};
    ui.outputPanel = {m_outputPanel, tr("T256/T384/T640 输出"), nullptr, {}};
    ui.componentLibrary.items = {
        {QStringLiteral("t384.thermal"), tr("T256/T384/T640 热图"), QStringLiteral(":/serein/thermal.svg")},
        {QStringLiteral("t384.trend"), tr("T256/T384/T640 趋势"), {}},
        {QStringLiteral("t384.calibration"), tr("实验二点标定"), {}},
    };
    ui.componentLibrary.activationTarget = this;
    ui.componentLibrary.activationMethod = QByteArrayLiteral("activateLibraryComponent");
    m_uiProvider->setContext(std::move(ui));
    connect(m_connect, &QPushButton::clicked, this, [this] {
        m_bytesReceived = 0;
        m_framesReceived = 0;
        if (m_traffic) m_traffic->setText(tr("接收：0 B · 完整帧：0"));
        if (m_error) m_error->setText(tr("错误：—"));
        appendLog(QStringLiteral("info"),
                  tr("开始连接：%1").arg(m_endpoint->text()));
        m_transport->start(m_endpoint->text());
    });
    connect(m_stop, &QPushButton::clicked, this, [this] {
        m_transport->stop();
        appendLog(QStringLiteral("info"), tr("已停止 T384 数据流"));
    });
    connect(m_transport, &T384Transport::stateChanged, this, [this](const QString &state) {
        const QString shown = state == QStringLiteral("connecting") ? tr("连接中")
            : state == QStringLiteral("reconnecting") ? tr("等待 USB 网络恢复")
            : state == QStringLiteral("streaming") ? tr("流式传输")
            : state == QStringLiteral("error") ? tr("错误")
            : state == QStringLiteral("disconnected") ? tr("未连接") : state;
        if (m_status) m_status->setText(shown);
        appendLog(QStringLiteral("info"), tr("状态：%1").arg(shown));
        if (m_host) m_host->setStatus(tr("T384 · %1").arg(shown));
    });
    connect(m_transport, &T384Transport::errorOccurred, this,
            [this](const QString &message) {
                if (m_error) m_error->setText(tr("错误：%1").arg(message));
                appendLog(QStringLiteral("error"), message);
                if (m_host) m_host->setStatus(tr("T384 · %1").arg(message));
            });
    connect(m_transport, &T384Transport::bytesReceived, this,
            [this](const qint64 count) {
                m_bytesReceived += count;
                if (m_traffic) {
                    m_traffic->setText(tr("接收：%1 B · 完整帧：%2")
                                            .arg(m_bytesReceived)
                                            .arg(m_framesReceived));
                }
            });
    connect(m_transport, &T384Transport::packetsDecoded, this, &T384Workspace::handlePackets);
    connect(m_transport, &T384Transport::diagnosticsReady, this,
            [this](const QVariantMap &values) {
                updateDiagnostics(values);
                const QString identity = values.value(QStringLiteral("mini2.pn")).toString()
                    + QLatin1Char('/') + values.value(QStringLiteral("mini2.sn")).toString()
                    + QLatin1Char('/') + values.value(QStringLiteral("mini2.firmware_version")).toString();
                if (!identity.trimmed().isEmpty()) {
                    m_calibration->setDeviceIdentity(identity.toUtf8());
                }
                appendLog(QStringLiteral("info"), tr("设备诊断已更新：%1").arg(identity));
            });
    connect(m_calibration, &T384CalibrationPage::modelChanged, this,
            [this](bool enabled, double zero, double scale) {
                m_engine->setExperimentalModel(enabled, zero, scale);
            });
    connect(m_calibration, &T384CalibrationPage::uploadRequested,
            m_transport, &T384Transport::uploadCalibrationData);
    connect(m_calibration, &T384CalibrationPage::readbackRequested,
            m_transport, &T384Transport::requestCalibrationData);
    connect(m_calibration, &T384CalibrationPage::commitRequested,
            m_transport, &T384Transport::commitCalibration);
    connect(m_calibration, &T384CalibrationPage::abortRequested,
            m_transport, &T384Transport::abortCalibration);
    connect(m_transport, &T384Transport::calibrationOperationFinished,
            m_calibration, &T384CalibrationPage::onCalibrationResult);
    connect(m_transport, &T384Transport::calibrationOperationFinished, this,
            [this](const QString &operation, const bool success,
                   const QString &message) {
                appendLog(success ? QStringLiteral("info") : QStringLiteral("error"),
                          success ? tr("标定操作 %1 成功").arg(operation)
                                  : tr("标定操作 %1 失败：%2").arg(operation, message));
            });
    connect(m_transport, &T384Transport::calibrationDataReady,
            m_calibration, &T384CalibrationPage::applyDeviceData);
}

T384Workspace::~T384Workspace() { shutdown(); }

void T384Workspace::shutdown()
{
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
        {QStringLiteral("t384.calibration"), QStringLiteral("t384"), tr("实验二点标定"),
         tr("标定"), {}, QSize(420, 300), QSize(300, 220), caps,
         WorkspaceBindingKind::None, {}},
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
    if (id == QStringLiteral("t384.calibration")) {
        auto *page = new T384CalibrationPage(parent);
        connect(m_transport, &T384Transport::packetsDecoded, page,
                [page](const PacketList &packets) {
                    for (const Packet &packet : packets) {
                        if (const auto *image = std::get_if<ImagePacket>(&packet);
                            image && image->source == QString::fromLatin1(T384Raw16Source)) {
                            page->ingestFrame(image->pixels, image->width, image->height);
                        }
                    }
                });
        connect(page, &T384CalibrationPage::modelChanged, this,
                [this](bool enabled, double zero, double scale) {
                    if (m_engine) m_engine->setExperimentalModel(enabled, zero, scale);
                });
        return page;
    }
    return nullptr;
}

bool T384Workspace::activateLibraryComponent(const QString &definitionId)
{
    return m_canvas && m_canvas->addOrShowComponentAt(definitionId.trimmed(), QPoint(-1, -1));
}

void T384Workspace::handlePackets(const PacketList &packets)
{
    for (const Packet &packet : packets) {
        if (const auto *image = std::get_if<ImagePacket>(&packet)) {
            if (image->source == QString::fromLatin1(T384MonitorSource)) {
                for (const auto &view : m_thermals) if (view) view->setFrame(image->pixels, image->width, image->height);
            } else if (image->source == QString::fromLatin1(T384Raw16Source)) {
                m_calibration->ingestFrame(image->pixels, image->width, image->height);
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
            TrendSample trend;
            trend.seconds = sample->timestampUs / 1e6;
            trend.centerPoint = temp;
            trend.centerRoi = temp;
            trend.centerPointAdu = raw;
            trend.centerRoiAdu = raw;
            for (const auto &widget : m_trends) if (widget) widget->addSample(trend);
        }
    }
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
}

void T384Workspace::updateContext()
{
    if (!m_lastDiagnostics.isEmpty()) updateDiagnostics(m_lastDiagnostics);
}

} // namespace serein::scope::t384
