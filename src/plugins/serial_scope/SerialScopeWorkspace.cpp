#include "SerialScopeWorkspace.h"
#include "DelimitedEngine.h"
#include "WaveformWidget.h"
#include "WorkspaceCanvas.h"

#include <QtGui/QAction>
#include <QtGui/QBrush>
#include <QtGui/QTextDocument>
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

namespace serein::scope::serial {

SerialScopeWorkspace::SerialScopeWorkspace(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host), m_transport(this)
{
    setObjectName(QStringLiteral("serialScopeWorkspace"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *transportState = new QWidget(this);
    transportState->setObjectName(QStringLiteral("serialTransportState"));
    transportState->hide();
    m_port = new QComboBox(transportState);
    m_port->setObjectName(QStringLiteral("serialPort"));
    m_baud = new QSpinBox(transportState);
    m_baud->setObjectName(QStringLiteral("serialBaud"));
    m_baud->setRange(300, 12000000);
    m_baud->setValue(115200);
    m_baud->setSingleStep(9600);
    m_engine = new QComboBox(transportState);
    m_engine->setObjectName(QStringLiteral("serialEngine"));
    m_engine->addItem(QStringLiteral("Delimited numeric"), QStringLiteral("delimited"));
    m_engine->addItem(QStringLiteral("Raw text"), QStringLiteral("raw"));
    m_scan = new QPushButton(tr("扫描"), transportState);
    m_state = new QLabel(tr("未连接"), transportState);
    m_state->setObjectName(QStringLiteral("statusPill"));
    m_connect = new QPushButton(tr("连接"), transportState);
    m_connect->setProperty("kind", QStringLiteral("primary"));

    auto *plotPanel = new QFrame;
    plotPanel->setObjectName(QStringLiteral("panel"));
    auto *plotLayout = new QVBoxLayout(plotPanel);
    plotLayout->setContentsMargins(10, 10, 10, 10);
    auto *plotHeader = new QHBoxLayout;
    m_rate = new QLabel(QStringLiteral("0 B/s  ·  buffer 0 B"));
    m_rate->setObjectName(QStringLiteral("pageSubtitle"));
    auto *clear = new QPushButton(tr("清空"));
    clear->setProperty("kind", QStringLiteral("quiet"));
    plotHeader->addWidget(m_rate);
    plotHeader->addStretch();
    plotHeader->addWidget(clear);
    plotLayout->addLayout(plotHeader);
    m_waveform = new WaveformWidget;
    plotLayout->addWidget(m_waveform, 1);
    auto *channelPanel = new QFrame;
    channelPanel->setObjectName(QStringLiteral("panel"));
    channelPanel->setMinimumWidth(190);
    channelPanel->setMaximumWidth(270);
    auto *channelLayout = new QVBoxLayout(channelPanel);
    channelLayout->setContentsMargins(10, 10, 10, 10);
    auto *channelHint = new QLabel(tr("勾选决定波形可见性"));
    channelHint->setObjectName(QStringLiteral("pageSubtitle"));
    m_channels = new QListWidget;
    m_channels->setObjectName(QStringLiteral("channelList"));
    channelLayout->addWidget(channelHint);
    channelLayout->addWidget(m_channels, 1);
    auto *terminalPanel = new QFrame;
    terminalPanel->setObjectName(QStringLiteral("panel"));
    auto *terminalLayout = new QVBoxLayout(terminalPanel);
    terminalLayout->setContentsMargins(10, 9, 10, 10);
    m_terminal = new QPlainTextEdit;
    m_terminal->setObjectName(QStringLiteral("serialTerminalOutput"));
    m_terminal->setReadOnly(true);
    m_terminal->document()->setMaximumBlockCount(1200);
    m_terminal->setMaximumHeight(160);
    terminalLayout->addWidget(m_terminal);
    auto *sendRow = new QHBoxLayout;
    m_command = new QLineEdit;
    m_command->setObjectName(QStringLiteral("serialCommandInput"));
    m_command->setPlaceholderText(tr("发送文本命令"));
    m_ending = new QComboBox;
    m_ending->addItems({QStringLiteral("LF"), QStringLiteral("CRLF"), tr("无换行")});
    m_command->setEnabled(false);
    m_ending->setEnabled(false);
    auto *send = new QPushButton(tr("发送"));
    send->setObjectName(QStringLiteral("serialSendButton"));
    sendRow->addWidget(m_command, 1);
    sendRow->addWidget(m_ending);
    sendRow->addWidget(send);
    terminalLayout->addLayout(sendRow);
    // Serial Scope is the host workspace itself.  Keep the canvas empty on
    // first open; waveform, channel and terminal views are optional
    // instances, not three fixed panels that compete with the host docks.
    m_workspace = new tn160::WorkspaceCanvas(QStringLiteral("serial/serial-scope-v2"));
    m_workspace->setProperty("emptyTitle", tr("Serial Scope"));
    m_workspace->setProperty("emptyHint", tr("从组件库添加实时波形、数据通道或串口终端"));
    m_workspace->addComponent(QStringLiteral("waveform"), tr("实时波形"), plotPanel,
                              QRectF(0.018, 0.028, 0.690, 0.560), QSize(360, 210));
    m_workspace->addComponent(QStringLiteral("channels"), tr("数据通道"), channelPanel,
                              QRectF(0.720, 0.028, 0.262, 0.560), QSize(180, 210));
    m_workspace->addComponent(QStringLiteral("terminal"), tr("串口终端"), terminalPanel,
                              QRectF(0.018, 0.642, 0.964, 0.330), QSize(520, 130));
    m_workspace->setComponentVisible(QStringLiteral("waveform"), false);
    m_workspace->setComponentVisible(QStringLiteral("channels"), false);
    m_workspace->setComponentVisible(QStringLiteral("terminal"), false);

    root->addWidget(m_workspace, 1);

    connect(m_scan, &QPushButton::clicked, this, &SerialScopeWorkspace::refreshPorts);
    connect(m_connect, &QPushButton::clicked, this, &SerialScopeWorkspace::toggleConnection);
    connect(clear, &QPushButton::clicked, this, &SerialScopeWorkspace::clearData);
    connect(send, &QPushButton::clicked, this, &SerialScopeWorkspace::sendCommand);
    connect(m_command, &QLineEdit::returnPressed, this, &SerialScopeWorkspace::sendCommand);
    connect(m_channels, &QListWidget::itemChanged,
            this, &SerialScopeWorkspace::channelVisibilityChanged);
    connect(&m_transport, &SerialTransport::stateChanged,
            this, &SerialScopeWorkspace::handleState);
    connect(&m_transport, &SerialTransport::dataReceived,
            this, &SerialScopeWorkspace::handleData);
    connect(&m_transport, &SerialTransport::errorOccurred,
            this, &SerialScopeWorkspace::handleError);
    connect(&m_transport, &SerialTransport::bytesWritten,
            this, &SerialScopeWorkspace::handleBytesWritten);

    rebuildDecoder();
    refreshPorts();
}

void SerialScopeWorkspace::shutdown()
{
    m_pendingTransmitCommands.clear();
    m_connected = false;
    m_transport.close();
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
    if (m_transport.isActive()) {
        m_transport.close();
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
    m_transport.open({m_port->currentText(), m_baud->value()});
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
    return m_transport.isActive();
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
    m_totalBytes += data.size();
    try {
        const PacketList packets = m_decoder->feed(QByteArrayView(data));
        for (const Packet &packet : packets) {
            appendPacket(packet);
        }
    } catch (const std::exception &error) {
        m_decoder->reset();
        handleError(QString::fromUtf8(error.what()));
    }
    const double seconds = std::max(m_rateTimer.elapsed() / 1000.0, 0.001);
    m_rate->setText(tr("%1 B/s  ·  buffer %2 B")
                        .arg(m_totalBytes / seconds, 0, 'f', 0)
                        .arg(m_decoder->pendingBytes()));
}

void SerialScopeWorkspace::handleError(const QString &message)
{
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
    if (!m_connected || !m_transport.isActive()) {
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
    m_transport.write(payload);
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
    m_waveform->setChannelVisible(item->data(Qt::UserRole).toInt(),
                                  item->checkState() == Qt::Checked);
}

void SerialScopeWorkspace::clearData()
{
    m_waveform->clear();
    m_channels->clear();
    m_terminal->clear();
}

void SerialScopeWorkspace::rebuildDecoder()
{
    std::unique_ptr<IDataEngine> engine;
    if (m_engine && m_engine->currentData().toString() == QStringLiteral("raw")) {
        engine = std::make_unique<RawTextEngine>();
    } else {
        engine = std::make_unique<DelimitedEngine>();
    }
    m_decoder = std::make_unique<BufferedDecoder>(std::move(engine));
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
    if (const auto *sample = std::get_if<SamplePacket>(&packet)) {
        m_waveform->addSample(sample->values, sample->labels);
        syncChannels(*sample);
        QStringList values;
        for (const double value : sample->values) {
            values.append(QString::number(value, 'g', 7));
        }
        m_terminal->appendPlainText(QStringLiteral("RX  %1").arg(values.join(QStringLiteral("  "))));
    } else if (const auto *text = std::get_if<TextPacket>(&packet)) {
        const QString line = QStringLiteral("RX  %1").arg(text->text.trimmed());
        m_terminal->appendPlainText(line);
    } else if (const auto *event = std::get_if<EventPacket>(&packet)) {
        if (m_host) {
            m_host->log(event->level, event->message);
        }
    }
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
