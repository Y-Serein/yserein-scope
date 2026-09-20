#include "SerialScopeWorkspace.h"
#include "DelimitedEngine.h"
#include "WaveformWidget.h"

#include <serein/scope/WorkspaceCanvas.h>

#include <QtGui/QAction>
#include <QtGui/QBrush>
#include <QtGui/QTextDocument>
#include <QtCore/QSettings>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMenu>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyle>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>

#include <exception>
#include <optional>
#include <utility>

namespace serein::scope::serial {

SerialScopeWorkspace::SerialScopeWorkspace(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host),
      m_session(std::make_unique<DelimitedEngine>(),
                DecoderRuntime::configuredLimits(), this)
{
    setObjectName(QStringLiteral("serialScopeWorkspace"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_contextPanel = new QWidget(this);
    m_contextPanel->setObjectName(QStringLiteral("workspaceContextContent"));
    auto *contextLayout = new QVBoxLayout(m_contextPanel);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    contextLayout->setSpacing(8);
    auto *hint = new QLabel(tr("Serial Scope 串口会话"), m_contextPanel);
    hint->setObjectName(QStringLiteral("pageSubtitle"));
    contextLayout->addWidget(hint);
    contextLayout->addWidget(new QLabel(tr("串口"), m_contextPanel));
    m_port = new QComboBox(m_contextPanel);
    m_port->setObjectName(QStringLiteral("serialPort"));
    contextLayout->addWidget(m_port);
    contextLayout->addWidget(new QLabel(tr("波特率"), m_contextPanel));
    m_baud = new QSpinBox(m_contextPanel);
    m_baud->setObjectName(QStringLiteral("serialBaud"));
    m_baud->setRange(300, 12000000);
    m_baud->setValue(115200);
    m_baud->setSingleStep(9600);
    contextLayout->addWidget(m_baud);
    contextLayout->addWidget(new QLabel(tr("解析引擎"), m_contextPanel));
    m_engine = new QComboBox(m_contextPanel);
    m_engine->setObjectName(QStringLiteral("serialEngine"));
    m_engine->addItem(QStringLiteral("Delimited numeric"), QStringLiteral("delimited"));
    m_engine->addItem(QStringLiteral("Raw text"), QStringLiteral("raw"));
    contextLayout->addWidget(m_engine);
    auto *actions = new QHBoxLayout;
    m_scan = new QPushButton(tr("扫描"), m_contextPanel);
    m_connect = new QPushButton(tr("连接"), m_contextPanel);
    m_connect->setProperty("kind", QStringLiteral("primary"));
    actions->addWidget(m_scan);
    actions->addWidget(m_connect, 1);
    contextLayout->addLayout(actions);
    m_state = new QLabel(tr("未连接"), m_contextPanel);
    m_state->setObjectName(QStringLiteral("statusPill"));
    contextLayout->addWidget(m_state);
    contextLayout->addStretch();

    m_uiProvider = new WorkspaceUiProvider(this);

    auto *plotPanel = createComponent(QStringLiteral("serial-scope.waveform"), nullptr);
    auto *channelPanel = createComponent(QStringLiteral("serial-scope.channels"), nullptr);
    auto *terminalPanel = createComponent(QStringLiteral("serial-scope.terminal"), nullptr);
    Q_ASSERT(plotPanel && channelPanel && terminalPanel);
    // Serial Scope is the host workspace itself.  Keep the canvas empty on
    // first open; waveform, channel and terminal views are optional
    // instances, not three fixed panels that compete with the host docks.
    // Definition IDs are globally stable and provider-qualified. Preserve
    // the existing layout once while moving from the historical canvas-local
    // IDs (waveform/channels/terminal).
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("serial/serial-scope-v2"));
        const QList<QPair<QString, QString>> aliases = {
            {QStringLiteral("waveform"), QStringLiteral("serial-scope.waveform")},
            {QStringLiteral("channels"), QStringLiteral("serial-scope.channels")},
            {QStringLiteral("terminal"), QStringLiteral("serial-scope.terminal")},
        };
        for (const auto &[oldId, newId] : aliases) {
            for (const QString &field : {QStringLiteral("geometry"),
                                         QStringLiteral("instanceId"),
                                         QStringLiteral("zOrder"),
                                         QStringLiteral("visible")}) {
                const QString oldKey = QStringLiteral("components/%1/%2")
                    .arg(oldId, field);
                const QString newKey = QStringLiteral("components/%1/%2")
                    .arg(newId, field);
                if (settings.contains(oldKey) && !settings.contains(newKey)) {
                    settings.setValue(newKey, settings.value(oldKey));
                }
            }
        }
        settings.endGroup();
    }
    m_workspace = new WorkspaceCanvas(QStringLiteral("serial/serial-scope-v2"));
    const bool restoreComponentLayout = m_workspace->hasStoredLayout();
    m_workspace->setProperty("emptyTitle", tr("Serial Scope"));
    m_workspace->setProperty("emptyHint", tr("从组件库添加实时波形、数据通道或串口终端"));
    m_workspace->addComponent(QStringLiteral("serial-scope.waveform"),
                              tr("实时波形"), plotPanel,
                              QRectF(0.018, 0.028, 0.690, 0.560), QSize(360, 210));
    m_workspace->addComponent(QStringLiteral("serial-scope.channels"), tr("数据通道"), channelPanel,
                              QRectF(0.720, 0.028, 0.262, 0.560), QSize(180, 210));
    m_workspace->addComponent(QStringLiteral("serial-scope.terminal"), tr("串口终端"), terminalPanel,
                              QRectF(0.018, 0.642, 0.964, 0.330), QSize(520, 130));
    if (!restoreComponentLayout) {
        m_workspace->setComponentVisible(QStringLiteral("serial-scope.waveform"), false);
        m_workspace->setComponentVisible(QStringLiteral("serial-scope.channels"), false);
        m_workspace->setComponentVisible(QStringLiteral("serial-scope.terminal"), false);
    }

    WorkspaceUiContext uiContext;
    uiContext.id = QStringLiteral("serial");
    uiContext.contextPanel = {m_contextPanel, tr("设备连接"), nullptr, {}};
    uiContext.componentLibrary.items = {
        {QStringLiteral("serial-scope.waveform"), tr("实时波形"),
         QStringLiteral(":/serein/waveform.svg")},
        {QStringLiteral("serial-scope.channels"), tr("数据通道"),
         QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("serial-scope.terminal"), tr("串口终端"),
         QStringLiteral(":/serein/terminal.svg")},
    };
    uiContext.componentLibrary.activationTarget = this;
    uiContext.componentLibrary.activationMethod =
        QByteArrayLiteral("activateLibraryComponent");
    m_uiProvider->setContext(std::move(uiContext));

    root->addWidget(m_workspace, 1);

    connect(m_scan, &QPushButton::clicked, this, &SerialScopeWorkspace::refreshPorts);
    connect(m_connect, &QPushButton::clicked, this, &SerialScopeWorkspace::toggleConnection);
    connect(m_command, &QLineEdit::returnPressed, this, &SerialScopeWorkspace::sendCommand);
    connect(&m_session, &SerialDataSession::stateChanged,
            this, &SerialScopeWorkspace::handleState);
    connect(&m_session, &SerialDataSession::packetsDecoded,
            this, &SerialScopeWorkspace::handleDecodedPackets);
    connect(&m_session, &SerialDataSession::decoderProgressChanged,
            this, &SerialScopeWorkspace::handleDecoderProgress);
    connect(&m_session, &SerialDataSession::errorOccurred,
            this, &SerialScopeWorkspace::handleDecoderError);
    connect(&m_session, &SerialDataSession::bytesReceived,
            this, &SerialScopeWorkspace::handleBytesReceived);
    connect(&m_session, &SerialDataSession::bytesWritten,
            this, &SerialScopeWorkspace::handleBytesWritten);
    connect(&m_channelRegistry, &ChannelRegistry::channelsChanged, this,
            [this] {
                if (!m_workspace) return;
                for (const QString &key : m_workspace->componentIds()) {
                    const WorkspaceComponentInstance instance =
                        m_workspace->componentInstance(key);
                    if (instance.definitionId != QStringLiteral("serial-scope.waveform")) {
                        continue;
                    }
                    QString error;
                    if (!applyComponentState(
                            m_workspace->componentContentByInstance(instance.instanceId),
                            instance, &error)
                        && m_host && !error.isEmpty()) {
                        m_host->log(QStringLiteral("warning"),
                                    tr("波形绑定未更新：%1").arg(error));
                    }
                }
            });
    m_uiFlush.setSingleShot(true);
    m_uiFlush.setInterval(50);
    m_uiFlush.setTimerType(Qt::PreciseTimer);
    connect(&m_uiFlush, &QTimer::timeout,
            this, &SerialScopeWorkspace::flushPendingUi);

    rebuildDecoder();
    refreshPorts();
}

void SerialScopeWorkspace::shutdown()
{
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    m_uiFlush.stop();
    m_pendingSamples.clear();
    m_pendingTerminalLines.clear();
    m_pendingTransmitCommands.clear();
    m_connected = false;
    m_session.shutdown();
}

WorkspaceUiProvider *SerialScopeWorkspace::workspaceUiProvider()
{
    return m_uiProvider;
}

QList<WorkspaceComponentDefinition> SerialScopeWorkspace::componentDefinitions() const
{
    const WorkspaceComponentCapabilities canvasCapabilities =
        WorkspaceComponentCapability::Movable
        | WorkspaceComponentCapability::Resizable
        | WorkspaceComponentCapability::Maximizable;
    return {
        {
            .definitionId = QStringLiteral("serial-scope.waveform"),
            .providerId = QStringLiteral("serial-scope"),
            .displayName = tr("实时波形"),
            .category = tr("数据可视化"),
            .iconSource = QStringLiteral(":/serein/waveform.svg"),
            .defaultSize = QSize(640, 320),
            .minimumSize = QSize(360, 210),
            .capabilities = canvasCapabilities
                | WorkspaceComponentCapability::PropertyEditing
                | WorkspaceComponentCapability::DataBinding,
            .bindingKinds = WorkspaceBindingKind::SampleChannel,
            .properties = {
                {QStringLiteral("lineWidth"), tr("线宽"), QStringLiteral("double"),
                 1.35, {{QStringLiteral("min"), 0.5}, {QStringLiteral("max"), 4.0}}},
            },
        },
        {
            .definitionId = QStringLiteral("serial-scope.channels"),
            .providerId = QStringLiteral("serial-scope"),
            .displayName = tr("数据通道"),
            .category = tr("数据源"),
            .iconSource = QStringLiteral(":/serein/channels.svg"),
            .defaultSize = QSize(240, 300),
            .minimumSize = QSize(180, 210),
            .capabilities = canvasCapabilities,
        },
        {
            .definitionId = QStringLiteral("serial-scope.terminal"),
            .providerId = QStringLiteral("serial-scope"),
            .displayName = tr("串口终端"),
            .category = tr("调试工具"),
            .iconSource = QStringLiteral(":/serein/terminal.svg"),
            .defaultSize = QSize(720, 190),
            .minimumSize = QSize(520, 130),
            .capabilities = canvasCapabilities,
        },
    };
}

QWidget *SerialScopeWorkspace::createComponent(const QString &definitionId,
                                                QWidget *parent)
{
    const QString id = definitionId.trimmed();
    if (id == QStringLiteral("serial-scope.waveform")) {
        return createWaveformComponent(parent);
    }
    if (id == QStringLiteral("serial-scope.channels")) {
        return createChannelsComponent(parent);
    }
    if (id == QStringLiteral("serial-scope.terminal")) {
        return createTerminalComponent(parent);
    }
    return nullptr;
}

QWidget *SerialScopeWorkspace::createWaveformComponent(QWidget *parent)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("panel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);
    auto *header = new QHBoxLayout;
    m_rate = new QLabel(QStringLiteral("0 B/s  ·  buffer 0 B"), panel);
    m_rate->setObjectName(QStringLiteral("pageSubtitle"));
    auto *clear = new QPushButton(tr("清空"), panel);
    clear->setProperty("kind", QStringLiteral("quiet"));
    header->addWidget(m_rate);
    header->addStretch();
    header->addWidget(clear);
    layout->addLayout(header);

    m_waveform = new WaveformWidget(panel);
    m_waveform->setObjectName(QStringLiteral("serialWaveform"));
    m_waveformViews.append(m_waveform);
    layout->addWidget(m_waveform, 1);
    connect(clear, &QPushButton::clicked, this, &SerialScopeWorkspace::clearData);
    return panel;
}

QWidget *SerialScopeWorkspace::createChannelsComponent(QWidget *parent)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("panel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);
    auto *hint = new QLabel(tr("勾选决定波形可见性"), panel);
    hint->setObjectName(QStringLiteral("pageSubtitle"));
    m_channels = new QListWidget(panel);
    m_channels->setObjectName(QStringLiteral("channelList"));
    layout->addWidget(hint);
    layout->addWidget(m_channels, 1);
    connect(m_channels, &QListWidget::itemChanged,
            this, &SerialScopeWorkspace::channelVisibilityChanged);
    return panel;
}

QWidget *SerialScopeWorkspace::createTerminalComponent(QWidget *parent)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("panel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 9, 10, 10);
    m_terminal = new QPlainTextEdit(panel);
    m_terminal->setObjectName(QStringLiteral("serialTerminalOutput"));
    m_terminal->setReadOnly(true);
    m_terminal->document()->setMaximumBlockCount(1200);
    m_terminal->setMaximumHeight(160);
    layout->addWidget(m_terminal);

    auto *sendRow = new QHBoxLayout;
    m_command = new QLineEdit(panel);
    m_command->setObjectName(QStringLiteral("serialCommandInput"));
    m_command->setPlaceholderText(tr("发送文本命令"));
    m_ending = new QComboBox(panel);
    m_ending->addItems({QStringLiteral("LF"), QStringLiteral("CRLF"), tr("无换行")});
    m_command->setEnabled(false);
    m_ending->setEnabled(false);
    auto *send = new QPushButton(tr("发送"), panel);
    send->setObjectName(QStringLiteral("serialSendButton"));
    sendRow->addWidget(m_command, 1);
    sendRow->addWidget(m_ending);
    sendRow->addWidget(send);
    layout->addLayout(sendRow);
    connect(send, &QPushButton::clicked, this, &SerialScopeWorkspace::sendCommand);
    return panel;
}

bool SerialScopeWorkspace::applyComponentState(
    QWidget *component, const WorkspaceComponentInstance &instance,
    QString *error)
{
    if (instance.definitionId != QStringLiteral("serial-scope.waveform")) {
        return true;
    }
    auto *waveform = qobject_cast<WaveformWidget *>(component);
    if (!waveform && component) {
        waveform = component->findChild<WaveformWidget *>();
    }
    if (!waveform) {
        if (error) *error = tr("实时波形实例缺少 WaveformWidget");
        return false;
    }

    const QVariant lineWidth = instance.properties.value(
        QStringLiteral("lineWidth"), 1.35);
    waveform->setLineWidth(lineWidth.toDouble());

    QSet<int> boundIndices;
    for (const WorkspaceBinding &binding : instance.bindings) {
        if (binding.kind != WorkspaceBindingKind::SampleChannel
            || binding.channelId.trimmed().isEmpty()) {
            continue;
        }
        const WorkspaceChannelDescriptor descriptor =
            m_channelRegistry.channel(binding.channelId);
        bool validIndex = false;
        const int index = descriptor.metadata.value(
            QStringLiteral("index")).toInt(&validIndex);
        if (descriptor.kind == WorkspaceBindingKind::SampleChannel
            && validIndex && index >= 0) {
            boundIndices.insert(index);
        }
    }
    // A newly added binding starts empty in the Inspector. Keep the normal
    // all-channel view until at least one binding resolves to a registered
    // channel; an incomplete edit must not make a live plot disappear.
    const bool hasResolvedBinding = !boundIndices.isEmpty();
    waveform->setBoundChannelIndices(std::move(boundIndices),
                                     hasResolvedBinding);
    return true;
}

bool SerialScopeWorkspace::activateLibraryComponent(const QString &componentId)
{
    QString normalized = componentId.trimmed();
    // Keep the historical short IDs accepted by older QML shells, while all
    // newly published definitions and persisted instances use provider-
    // qualified IDs.
    if (normalized == QStringLiteral("waveform")) {
        normalized = QStringLiteral("serial-scope.waveform");
    } else if (normalized == QStringLiteral("channels")) {
        normalized = QStringLiteral("serial-scope.channels");
    } else if (normalized == QStringLiteral("terminal")) {
        normalized = QStringLiteral("serial-scope.terminal");
    }
    return m_workspace
        && m_workspace->addOrShowComponentAt(normalized, QPoint(-1, -1));
}

void SerialScopeWorkspace::refreshPorts()
{
    const QString current = m_port->currentText();
    const QList<SerialPortInfo> ports = SerialTransport::availablePorts();
    m_port->clear();
    for (const SerialPortInfo &port : ports) {
        m_port->addItem(port.portName, port.description);
    }
    const int previous = m_port->findText(current);
    if (previous >= 0) {
        m_port->setCurrentIndex(previous);
    } else if (!current.trimmed().isEmpty()) {
        m_port->addItem(current.trimmed(), tr("手动端口"));
        m_port->setCurrentIndex(m_port->count() - 1);
    }
    if (m_host) {
        m_host->log(QStringLiteral("info"), tr("Serial Scope 扫描到 %1 个串口").arg(ports.size()));
    }
}

void SerialScopeWorkspace::toggleConnection()
{
    if (m_session.isActive()) {
        m_session.close();
        return;
    }
    if (m_port->currentText().isEmpty()) {
        handleError(tr("请先扫描并选择串口"));
        return;
    }
    rebuildDecoder();
    clearData();
    m_totalBytes = 0;
    m_rateTimer.restart();
    m_session.open({m_port->currentText(), m_baud->value()});
}

void SerialScopeWorkspace::setPort(const QString &port)
{
    if (!m_port) return;
    const QString normalized = port.trimmed();
    if (normalized.isEmpty()) return;
    int index = m_port->findText(normalized, Qt::MatchFixedString);
    if (index < 0) {
        m_port->addItem(normalized, tr("手动端口"));
        index = m_port->count() - 1;
    }
    m_port->setCurrentIndex(index);
}

void SerialScopeWorkspace::setBaud(const int baud)
{
    if (m_baud) m_baud->setValue(baud);
}

int SerialScopeWorkspace::baudRate() const noexcept
{
    return m_baud ? m_baud->value() : 0;
}

bool SerialScopeWorkspace::connectionActive() const noexcept
{
    return m_session.isActive();
}

quint64 SerialScopeWorkspace::uiFlushCount() const noexcept
{
    return m_uiFlushCount;
}

quint64 SerialScopeWorkspace::coalescedUiSamples() const noexcept
{
    return m_coalescedUiSamples;
}

quint64 SerialScopeWorkspace::coalescedTerminalLines() const noexcept
{
    return m_totalCoalescedTerminalLines;
}

ChannelRegistry *SerialScopeWorkspace::channelRegistry() noexcept
{
    return &m_channelRegistry;
}

void SerialScopeWorkspace::setEngine(const QString &engine)
{
    if (m_engine) {
        int index = m_engine->findData(engine);
        if (index < 0) index = m_engine->findText(engine);
        if (index >= 0 && m_engine->currentIndex() != index) {
            m_engine->setCurrentIndex(index);
            rebuildDecoder();
        }
    }
}

void SerialScopeWorkspace::sendCommandText(const QString &command, const QString &ending)
{
    if (m_command) m_command->setText(command);
    if (m_ending) {
        const int index = m_ending->findText(ending);
        if (index >= 0) m_ending->setCurrentIndex(index);
    }
    sendCommand();
}

void SerialScopeWorkspace::handleState(const QString &state)
{
    m_connected = state == QStringLiteral("connected");
    const bool locked = state == QStringLiteral("connecting")
        || state == QStringLiteral("connected")
        || state == QStringLiteral("disconnecting");
    const QString text = state == QStringLiteral("connected") ? tr("已连接")
        : state == QStringLiteral("connecting") ? tr("连接中")
        : state == QStringLiteral("disconnecting") ? tr("断开中") : tr("未连接");
    setStatePill(state, text);
    m_port->setEnabled(!locked);
    m_baud->setEnabled(!locked);
    m_engine->setEnabled(!locked);
    m_scan->setEnabled(!locked);
    m_connect->setText(state == QStringLiteral("connected") ? tr("断开") : tr("连接"));
    m_connect->setEnabled(state != QStringLiteral("connecting")
                          && state != QStringLiteral("disconnecting"));
    if (m_command) m_command->setEnabled(m_connected);
    if (m_ending) m_ending->setEnabled(m_connected);
    if (state == QStringLiteral("disconnected") && !m_pendingTransmitCommands.isEmpty()) {
        m_terminal->appendPlainText(tr("TX 失败：连接已断开，%1 条命令未发送")
                                         .arg(m_pendingTransmitCommands.size()));
        m_pendingTransmitCommands.clear();
    }
    if (m_host) {
        m_host->setStatus(tr("Serial Scope · %1").arg(text));
        m_host->log(QStringLiteral("info"), tr("串口会话状态：%1").arg(state));
    }
}

void SerialScopeWorkspace::handleData(const QByteArray &data)
{
    if (m_shuttingDown) return;
    m_session.ingest(data);
}

void SerialScopeWorkspace::handleBytesReceived(const qint64 count)
{
    if (m_shuttingDown || count <= 0) return;
    m_totalBytes += count;
    updateRate(m_session.snapshot().decoder.decoderPendingBytes);
}

void SerialScopeWorkspace::handleDecodedPackets(const PacketList &packets)
{
    if (m_shuttingDown) return;
    for (const Packet &packet : packets) appendPacket(packet);
    if ((!m_pendingSamples.isEmpty() || !m_pendingTerminalLines.isEmpty())
        && !m_uiFlush.isActive()) {
        m_uiFlush.start();
    }
}

void SerialScopeWorkspace::handleDecoderProgress(const qsizetype pendingBytes)
{
    if (m_shuttingDown) return;
    updateRate(pendingBytes);
}

void SerialScopeWorkspace::handleDecoderError(const QString &message)
{
    if (m_shuttingDown) return;
    handleError(message);
}

void SerialScopeWorkspace::updateRate(const qsizetype pendingBytes)
{
    const double seconds = std::max(m_rateTimer.elapsed() / 1000.0, 0.001);
    m_rate->setText(tr("%1 B/s  ·  buffer %2 B")
                        .arg(m_totalBytes / seconds, 0, 'f', 0)
                        .arg(pendingBytes));
}

void SerialScopeWorkspace::handleError(const QString &message)
{
    if (m_shuttingDown) return;
    m_session.resetDecoder();
    m_connected = false;
    setStatePill(QStringLiteral("error"), tr("错误"));
    m_terminal->appendPlainText(tr("ERROR  %1").arg(message));
    if (!m_pendingTransmitCommands.isEmpty()) {
        m_terminal->appendPlainText(tr("TX 失败：%1 条命令未确认发送")
                                         .arg(m_pendingTransmitCommands.size()));
        m_pendingTransmitCommands.clear();
    }
    if (m_host) {
        m_host->log(QStringLiteral("error"), message);
        m_host->setStatus(tr("Serial Scope · %1").arg(message));
    }
}

void SerialScopeWorkspace::sendCommand()
{
    const QString command = m_command->text();
    if (command.isEmpty()) {
        return;
    }
    if (!m_connected || !m_session.isActive()) {
        handleError(tr("请先连接串口，再发送命令"));
        return;
    }
    QByteArray payload = command.toUtf8();
    if (m_ending->currentText() == QStringLiteral("LF")) {
        payload.append('\n');
    } else if (m_ending->currentText() == QStringLiteral("CRLF")) {
        payload.append("\r\n");
    }
    m_pendingTransmitCommands.enqueue({command, payload.size()});
    m_session.write(payload);
    m_command->clear();
}

void SerialScopeWorkspace::handleBytesWritten(const qint64 count)
{
    if (count <= 0 || m_pendingTransmitCommands.isEmpty()) return;
    const PendingTransmit pending = m_pendingTransmitCommands.head();
    if (count != pending.byteCount) {
        handleError(tr("串口写入不完整：%1 / %2 bytes")
                        .arg(count).arg(pending.byteCount));
        return;
    }
    m_pendingTransmitCommands.dequeue();
    m_terminal->appendPlainText(tr("TX  %1  (%2 bytes)").arg(pending.command).arg(count));
}

void SerialScopeWorkspace::channelVisibilityChanged(QListWidgetItem *item)
{
    const int index = item->data(Qt::UserRole).toInt();
    const bool visible = item->checkState() == Qt::Checked;
    for (const QPointer<WaveformWidget> &waveform : std::as_const(m_waveformViews)) {
        if (waveform) waveform->setChannelVisible(index, visible);
    }
}

void SerialScopeWorkspace::clearData()
{
    m_uiFlush.stop();
    m_pendingSamples.clear();
    m_pendingTerminalLines.clear();
    m_coalescedTerminalLines = 0;
    m_totalCoalescedTerminalLines = 0;
    m_coalescedUiSamples = 0;
    m_uiFlushCount = 0;
    for (const QPointer<WaveformWidget> &waveform : std::as_const(m_waveformViews)) {
        if (waveform) waveform->clear();
    }
    m_channels->clear();
    m_terminal->clear();
    m_channelRegistry.clear();
}

void SerialScopeWorkspace::rebuildDecoder()
{
    std::unique_ptr<IDataEngine> engine;
    if (m_engine && m_engine->currentData().toString() == QStringLiteral("raw")) {
        engine = std::make_unique<RawTextEngine>();
    } else {
        engine = std::make_unique<DelimitedEngine>();
    }
    QString error;
    if (!m_session.replaceEngine(std::move(engine), &error)) {
        handleError(error);
    }
}

void SerialScopeWorkspace::syncChannels(const SamplePacket &sample)
{
    while (m_channels->count() < sample.values.size()) {
        const int index = m_channels->count();
        const QString name = index < sample.labels.size() ? sample.labels.at(index)
                                                          : QStringLiteral("CH%1").arg(index + 1);
        auto *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, index);
        item->setCheckState(Qt::Checked);
        item->setForeground(QBrush(m_waveform->channelColor(index)));
        m_channels->addItem(item);
    }
    for (int index = 0; index < sample.values.size(); ++index) {
        const QString name = index < sample.labels.size() ? sample.labels.at(index)
                                                          : QStringLiteral("CH%1").arg(index + 1);
        m_channels->item(index)->setText(QStringLiteral("%1    %2")
                                             .arg(name)
                                             .arg(sample.values.at(index), 0, 'g', 6));
    }
}

void SerialScopeWorkspace::appendPacket(const Packet &packet)
{
    QString publishError;
    if (!m_channelRegistry.publish(packet, &publishError) && m_host
        && !publishError.isEmpty()) {
        m_host->log(QStringLiteral("warning"),
                    tr("通道发布失败：%1").arg(publishError));
    }
    if (const auto *sample = std::get_if<SamplePacket>(&packet)) {
        m_pendingSamples.enqueue(*sample);
        while (m_pendingSamples.size() > MaxPendingSamples) {
            m_pendingSamples.dequeue();
            ++m_coalescedUiSamples;
        }
        QStringList values;
        for (const double value : sample->values) {
            values.append(QString::number(value, 'g', 7));
        }
        m_pendingTerminalLines.append(
            QStringLiteral("RX  %1").arg(values.join(QStringLiteral("  "))));
    } else if (const auto *text = std::get_if<TextPacket>(&packet)) {
        m_pendingTerminalLines.append(
            QStringLiteral("RX  %1").arg(text->text.trimmed()));
    } else if (const auto *event = std::get_if<EventPacket>(&packet)) {
        if (m_host) {
            m_host->log(event->level, event->message);
        }
    }
    while (m_pendingTerminalLines.size() > MaxPendingTerminalLines) {
        m_pendingTerminalLines.removeFirst();
        ++m_coalescedTerminalLines;
        ++m_totalCoalescedTerminalLines;
    }
}

void SerialScopeWorkspace::flushPendingUi()
{
    if (m_shuttingDown) return;
    ++m_uiFlushCount;
    if (m_pendingSamples.size() > 1) {
        m_coalescedUiSamples += quint64(m_pendingSamples.size() - 1);
    }
    std::optional<SamplePacket> latest;
    while (!m_pendingSamples.isEmpty()) {
        SamplePacket sample = std::move(m_pendingSamples.head());
        m_pendingSamples.dequeue();
        for (const QPointer<WaveformWidget> &waveform : std::as_const(m_waveformViews)) {
            if (waveform) waveform->addSample(sample.values, sample.labels, false);
        }
        latest = std::move(sample);
    }
    if (latest) {
        syncChannels(*latest);
        for (const QPointer<WaveformWidget> &waveform : std::as_const(m_waveformViews)) {
            if (waveform) waveform->refreshDisplay();
        }
    }

    QStringList lines = std::move(m_pendingTerminalLines);
    m_pendingTerminalLines.clear();
    if (m_coalescedTerminalLines > 0) {
        lines.prepend(tr("… 已合并 %1 条过快的显示日志，协议数据仍已完整解码")
                          .arg(m_coalescedTerminalLines));
        m_coalescedTerminalLines = 0;
    }
    if (!lines.isEmpty()) m_terminal->appendPlainText(lines.join(QLatin1Char('\n')));
}

void SerialScopeWorkspace::setStatePill(const QString &state, const QString &text)
{
    m_state->setText(text);
    m_state->setProperty("state", state);
    m_state->style()->unpolish(m_state);
    m_state->style()->polish(m_state);
    emit connectionStateChanged(state, text);
}

} // namespace serein::scope::serial
