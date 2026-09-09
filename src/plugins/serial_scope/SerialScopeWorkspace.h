#pragma once

#include <serein/scope/PluginInterface.h>
#include <serein/scope/SerialDataSession.h>
#include <serein/scope/WorkspaceComponents.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QQueue>
#include <QtCore/QTimer>
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

namespace serein::scope {

class WorkspaceCanvas;

} // namespace serein::scope

namespace serein::scope::serial {

class WaveformWidget;

class SerialScopeWorkspace final : public QWidget, public IDeviceWorkspace,
                                   public IWorkspaceUiProvider,
                                   public IWorkspaceComponentProvider {
    Q_OBJECT
    Q_INTERFACES(serein::scope::IDeviceWorkspace
                 serein::scope::IWorkspaceUiProvider
                 serein::scope::IWorkspaceComponentProvider)
    Q_PROPERTY(quint64 uiFlushCount READ uiFlushCount)
    Q_PROPERTY(quint64 coalescedUiSamples READ coalescedUiSamples)
    Q_PROPERTY(quint64 coalescedTerminalLines READ coalescedTerminalLines)

public:
    explicit SerialScopeWorkspace(IHostServices *host, QWidget *parent = nullptr);
    ~SerialScopeWorkspace() override = default;
    void shutdown() override;
    [[nodiscard]] WorkspaceUiProvider *workspaceUiProvider() override;
    [[nodiscard]] QList<WorkspaceComponentDefinition>
    componentDefinitions() const override;
    QWidget *createComponent(const QString &definitionId,
                              QWidget *parent) override;

public slots:
    void refreshPorts();
    void toggleConnection();
    void setPort(const QString &port);
    void setBaud(int baud);
    void setEngine(const QString &engine);
    [[nodiscard]] int baudRate() const noexcept;
    [[nodiscard]] bool connectionActive() const noexcept;
    [[nodiscard]] quint64 uiFlushCount() const noexcept;
    [[nodiscard]] quint64 coalescedUiSamples() const noexcept;
    [[nodiscard]] quint64 coalescedTerminalLines() const noexcept;
    void sendCommandText(const QString &command, const QString &ending);
    void clearData();
    bool activateLibraryComponent(const QString &componentId);

private slots:
    void handleState(const QString &state);
    void handleData(const QByteArray &data);
    void handleBytesReceived(qint64 count);
    void handleDecodedPackets(const serein::scope::PacketList &packets);
    void handleDecoderProgress(qsizetype pendingBytes);
    void handleDecoderError(const QString &message);
    void handleError(const QString &message);
    void handleBytesWritten(qint64 count);
    void channelVisibilityChanged(QListWidgetItem *item);
    void flushPendingUi();

signals:
    void connectionStateChanged(const QString &state, const QString &text);

private:
    struct PendingTransmit {
        QString command;
        qint64 byteCount = 0;
    };

    void rebuildDecoder();
    void updateRate(qsizetype pendingBytes);
    void syncChannels(const SamplePacket &sample);
    void appendPacket(const Packet &packet);
    void setStatePill(const QString &state, const QString &text);
    void sendCommand();

    IHostServices *m_host;
    WorkspaceUiProvider *m_uiProvider = nullptr;
    QWidget *m_contextPanel = nullptr;
    SerialDataSession m_session;
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
    WorkspaceCanvas *m_workspace = nullptr;
    QQueue<PendingTransmit> m_pendingTransmitCommands;
    QQueue<SamplePacket> m_pendingSamples;
    QStringList m_pendingTerminalLines;
    QList<QPointer<WaveformWidget>> m_waveformViews;
    QTimer m_uiFlush;
    quint64 m_coalescedTerminalLines = 0;
    quint64 m_totalCoalescedTerminalLines = 0;
    quint64 m_coalescedUiSamples = 0;
    quint64 m_uiFlushCount = 0;
    bool m_connected = false;
    bool m_shuttingDown = false;

    static constexpr qsizetype MaxPendingSamples = 2048;
    static constexpr qsizetype MaxPendingTerminalLines = 512;
};

} // namespace serein::scope::serial
