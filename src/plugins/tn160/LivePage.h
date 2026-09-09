#pragma once

#include <serein/scope/PluginInterface.h>
#include <serein/scope/BackendIoTaskGroup.h>
#include <serein/scope/SerialDataSession.h>
#include <serein/scope/WorkspaceCanvas.h>

#include <QtCore/QElapsedTimer>
#include <QtCore/QByteArray>
#include <QtCore/QQueue>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

#include <memory>
#include <optional>
#include <array>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace serein::scope::tn160 {

class ThermalView;
class TrendWidget;

inline constexpr int MonitorSampleValueCount = 26;

struct DiagnosticRecord {
    qint64 timestampUs = 0;
    double seconds = 0.0;
    std::array<double, MonitorSampleValueCount> values{};
    QString ffcEvent;
};

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
    void exportPng();
    void exportCsv();
    void clearDiagnosticSession();
    bool activateLibraryComponent(const QString &componentId);
    bool exportPngToPath(const QString &path);
    bool exportCsvToPath(const QString &path);
    void setPort(const QString &port);
    void setBaud(int baud);
    [[nodiscard]] int baudRate() const noexcept;
    [[nodiscard]] bool connectionActive() const noexcept;
    [[nodiscard]] bool rawCaptureActive() const noexcept;
    [[nodiscard]] QWidget *contextPanel() const noexcept;
    [[nodiscard]] QWidget *inspectorPanel() const noexcept;
    [[nodiscard]] QWidget *outputPanel() const noexcept;

private slots:
    void handleState(const QString &state);
    void handleData(const QByteArray &data);
    void handleDecodedPackets(const serein::scope::PacketList &packets);
    void flushPendingUi();
    void handleError(const QString &message);
    void handleRawTimeout();
    void handleMonitorTimeout();

signals:
    void connectionStateChanged(const QString &state, const QString &text);
    void rawCaptureStateChanged(const QString &state, const QString &text);
    void focusModeChanged(bool focused);
    void telemetryChanged(const QString &center, const QString &ntc,
                          const QString &centerAdu, const QString &vtemp,
                          const QString &fps);
    void diagnosticsChanged(const QString &details);

private:
    struct PendingUiFrame {
        ImagePacket image;
        std::optional<SamplePacket> sample;
    };

    void updateSample(const SamplePacket &sample);
    void appendDiagnosticSample(const SamplePacket &sample, double seconds);
    bool processPackets(const PacketList &packets);
    void scheduleUiRefresh();
    [[nodiscard]] double renderFpsValue() const;
    [[nodiscard]] QString renderFpsText() const;
    void armMonitorWatchdog();
    void handleRawFrame(const QByteArray &frame);
    void restoreMonitorAfterRawCapture();
    void finishRawCapture(bool success, const QString &message);
    void completeRawCaptureSave(const QString &path, const QString &error,
                                int frameCount);
    void setState(const QString &state, const QString &text);
    void clearPendingUi();
    void clearDiagnosticHistory();
    void completeDiagnosticExport(const QString &kind, const QString &path,
                                  const QString &error);

    IHostServices *m_host;
    BackendIoTaskGroup m_ioTasks;
    SerialDataSession m_session;
    QElapsedTimer m_elapsed;
    QElapsedTimer m_lastUiFrame;
    QQueue<PendingUiFrame> m_pendingUiFrames;
    QQueue<qint64> m_renderFrameTimesMs;
    QVector<DiagnosticRecord> m_diagnosticHistory;
    QTimer m_uiRefresh;
    QComboBox *m_port = nullptr;
    QSpinBox *m_baud = nullptr;
    QPushButton *m_scan = nullptr;
    QPushButton *m_capture = nullptr;
    QLabel *m_state = nullptr;
    QWidget *m_contextPanel = nullptr;
    QWidget *m_inspectorPanel = nullptr;
    QLabel *m_inspectorStatus = nullptr;
    QLabel *m_inspectorCenter = nullptr;
    QLabel *m_inspectorNtc = nullptr;
    QLabel *m_inspectorCenterAdu = nullptr;
    QLabel *m_inspectorVtemp = nullptr;
    QLabel *m_inspectorFps = nullptr;
    QLabel *m_inspectorDiagnostics = nullptr;
    ThermalView *m_thermal = nullptr;
    TrendWidget *m_trend = nullptr;
    WorkspaceCanvas *m_workspace = nullptr;
    QVector<QByteArray> m_rawFrames;
    QTimer m_rawTimeout;
    QTimer m_monitorTimeout;
    QElapsedTimer m_rawSilence;
    QElapsedTimer m_monitorSilence;
    QString m_rawFailureMessage;
    int m_lastNucDecision = 255;
    int m_lastNucCount = 0;
    int m_lastNucDg = 0;
    int m_lastNucDv = 0;
    int m_lastNucDn = 0;
    int m_rawTargetFrames = 0;
    bool m_rawCapture = false;
    bool m_rawRestoring = false;
    bool m_rawCancelled = false;
    bool m_rawGapActive = false;
    bool m_monitorAwaitingFirstFrame = false;
    bool m_monitorGapActive = false;
    bool m_haveNucSignature = false;
    bool m_shuttingDown = false;
};

} // namespace serein::scope::tn160
