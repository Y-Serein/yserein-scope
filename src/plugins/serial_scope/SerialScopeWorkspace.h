#pragma once

#include <serein/scope/BufferedDecoder.h>
#include <serein/scope/PluginInterface.h>
#include <serein/scope/SerialTransport.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QQueue>
#include <QtWidgets/QWidget>

#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace serein::scope::serial {

class WaveformWidget;

} // namespace serein::scope::serial

namespace serein::scope::tn160 {

class WorkspaceCanvas;

} // namespace serein::scope::tn160

namespace serein::scope::serial {

class SerialScopeWorkspace final : public QWidget, public IDeviceWorkspace {
    Q_OBJECT
    Q_INTERFACES(serein::scope::IDeviceWorkspace)

public:
    explicit SerialScopeWorkspace(IHostServices *host, QWidget *parent = nullptr);
    ~SerialScopeWorkspace() override = default;
    void shutdown() override;

public slots:
    void refreshPorts();
    void toggleConnection();
    void setPort(const QString &port);
    void setBaud(int baud);
    void setEngine(const QString &engine);
    [[nodiscard]] int baudRate() const noexcept;
    [[nodiscard]] bool connectionActive() const noexcept;
    void sendCommandText(const QString &command, const QString &ending);
    void clearData();

private slots:
    void handleState(const QString &state);
    void handleData(const QByteArray &data);
    void handleError(const QString &message);
    void handleBytesWritten(qint64 count);
    void channelVisibilityChanged(QListWidgetItem *item);

signals:
    void connectionStateChanged(const QString &state, const QString &text);

private:
    struct PendingTransmit {
        QString command;
        qint64 byteCount = 0;
    };

    void rebuildDecoder();
    void syncChannels(const SamplePacket &sample);
    void appendPacket(const Packet &packet);
    void setStatePill(const QString &state, const QString &text);
    void sendCommand();

    IHostServices *m_host;
    SerialTransport m_transport;
    std::unique_ptr<BufferedDecoder> m_decoder;
    QElapsedTimer m_rateTimer;
    qint64 m_totalBytes = 0;
    QComboBox *m_port = nullptr;
    QSpinBox *m_baud = nullptr;
    QComboBox *m_engine = nullptr;
    QPushButton *m_scan = nullptr;
    QPushButton *m_connect = nullptr;
    QLabel *m_state = nullptr;
    QLabel *m_rate = nullptr;
    WaveformWidget *m_waveform = nullptr;
    QListWidget *m_channels = nullptr;
    QPlainTextEdit *m_terminal = nullptr;
    QLineEdit *m_command = nullptr;
    QComboBox *m_ending = nullptr;
    tn160::WorkspaceCanvas *m_workspace = nullptr;
    QQueue<PendingTransmit> m_pendingTransmitCommands;
    bool m_connected = false;
};

} // namespace serein::scope::serial
