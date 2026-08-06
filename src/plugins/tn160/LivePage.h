#pragma once

#include <serein/scope/BufferedDecoder.h>
#include <serein/scope/PluginInterface.h>
#include <serein/scope/SerialTransport.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QByteArray>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

#include <memory>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace serein::scope::tn160 {

class ThermalView;
class TrendWidget;
class WorkspaceCanvas;

class LivePage final : public QWidget {
    Q_OBJECT

public:
    explicit LivePage(IHostServices *host, QWidget *parent = nullptr);
    void shutdown();

public slots:
    void refreshPorts();
    void toggleCapture();
    void startRawCapture(int frameCount);
    void cancelRawCapture();
    void setPort(const QString &port);
    void setBaud(int baud);
    [[nodiscard]] int baudRate() const noexcept;
    [[nodiscard]] bool connectionActive() const noexcept;
    [[nodiscard]] bool rawCaptureActive() const noexcept;

private slots:
    void handleState(const QString &state);
    void handleData(const QByteArray &data);
    void handleError(const QString &message);
    void handleRawTimeout();

signals:
    void connectionStateChanged(const QString &state, const QString &text);
    void rawCaptureStateChanged(const QString &state, const QString &text);
    void telemetryChanged(const QString &center, const QString &ntc,
                          const QString &centerAdu, const QString &vtemp,
                          const QString &fps);

private:
    void updateSample(const SamplePacket &sample);
    bool processPackets(const PacketList &packets);
    void handleRawData(const QByteArray &data);
    void restoreMonitorAfterRawCapture();
    void finishRawCapture(bool success, const QString &message);
    void setState(const QString &state, const QString &text);

    IHostServices *m_host;
    SerialTransport m_transport;
    std::unique_ptr<BufferedDecoder> m_decoder;
    QElapsedTimer m_elapsed;
    qint64 m_frames = 0;
    QComboBox *m_port = nullptr;
    QSpinBox *m_baud = nullptr;
    QPushButton *m_scan = nullptr;
    QPushButton *m_capture = nullptr;
    QLabel *m_state = nullptr;
    ThermalView *m_thermal = nullptr;
    TrendWidget *m_trend = nullptr;
    WorkspaceCanvas *m_workspace = nullptr;
    QByteArray m_rawBuffer;
    QVector<QByteArray> m_rawFrames;
    QTimer m_rawTimeout;
    QString m_rawFailureMessage;
    int m_rawTargetFrames = 0;
    bool m_rawCapture = false;
    bool m_rawRestoring = false;
    bool m_rawCancelled = false;
};

} // namespace serein::scope::tn160
