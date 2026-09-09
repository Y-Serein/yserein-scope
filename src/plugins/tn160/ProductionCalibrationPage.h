#pragma once

#include "CalibrationTransfer.h"
#include "Tn160Engine.h"
#include "Tn160ProductionFrameFilter.h"
#include "../Tn160ProductionStateGate.h"

#include <serein/scope/BackendIoTaskGroup.h>
#include <serein/scope/PluginInterface.h>
#include <serein/scope/SerialDataSession.h>

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QPoint>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace serein::scope::tn160 {

class ThermalView;

class ProductionCalibrationPage final : public QWidget {
    Q_OBJECT

public:
    explicit ProductionCalibrationPage(IHostServices *host,
                                       QWidget *parent = nullptr);
    void shutdown();

public slots:
    void refreshPorts();
    void setPort(const QString &port);
    void toggleConnection();
    [[nodiscard]] bool connectionActive() const noexcept;
    [[nodiscard]] int baudRate() const noexcept;
    [[nodiscard]] bool operationActive() const noexcept;
    [[nodiscard]] QWidget *contextPanel() const noexcept;

signals:
    void transferStateChanged(const QString &state, const QString &text);

private slots:
    void newSensorSession();
    void captureLow();
    void captureHigh();
    void captureHighB();
    void captureOffset();
    void computeTwoPoint();
    void importCalibration();
    void writeAndVerify();
    void exportCalibration();
    void handleTransportState(const QString &state);
    void handleDecodedPackets(const serein::scope::PacketList &packets);
    void handleTransportError(const QString &message);
    void handleTimeout();
    void receiveVerifyPayload(int index, const QByteArray &payload);
    void verifyCompleted(bool success, const QString &message);

private:
    enum class CaptureKind { None, Low, High, HighB, Offset };
    enum class Operation { Idle, Capturing, Writing, Verifying };
    enum class WriteStage {
        Idle,
        AwaitHeaderReady,
        AwaitImageReady,
        AwaitGainReady,
        AwaitBadPointsReady,
        AwaitFinal,
    };

    static QString captureName(CaptureKind kind);
    static int requiredFramesFor(CaptureKind kind);

    struct TelemetryState {
        bool valid = false;
        qint64 timestampUs = 0;
        double ntcC = 0.0;
        quint16 ntcAdu = 0;
        quint16 vtempAdu = 0;
        quint16 smoothVtempAdu = 0;
        quint8 nucDecision = 0xff;
        quint8 nucCount = 0;
        qint16 nucDg = 0;
        qint16 nucDv = 0;
        qint16 nucDn = 0;
        qint32 cumulativeNucDg = 0;
        quint16 bootNtcAdu = 0;
        qint32 anchor = 0;
        qint32 effectiveAnchor = 0;
        float meanDiff = 0.0f;
    };

    struct FrameInfo {
        double mean = 0.0;
        quint16 vtemp = 0;
        bool accepted = false;
        QString rejectionReason;
    };

    struct CaptureData {
        QVector<QByteArray> frames;
        QVector<FrameInfo> frameInfo;
        QVector<int> acceptedIndices;
        QVector<quint16> meanPixels;
        int requiredFrames = 0;
        TelemetryState before;
        TelemetryState after;
        bool complete = false;
    };

    struct CalibrationData {
        QByteArray background;
        QByteArray gain;
        QByteArray badPoints;
        QByteArray packet;
        float meanDiff = 0.0f;
        float tRefLow = 0.0f;
        int badPointCount = 0;
        bool valid = false;
        bool singlePointOffsetApplied = false;
        bool writeVerified = false;
    };

    struct ArchiveSnapshot {
        QString boardId;
        QString sensorId;
        CaptureData low;
        CaptureData high;
        CaptureData highB;
        CaptureData offset;
        QVector<TelemetryState> monitorTrace;
        bool monitorContinuityValid = true;
        TelemetryState stateAReference;
        bool cycleWarningIssued = false;
        CalibrationData calibration;
        quint32 writePayloadCrc = 0;
        quint32 writeHeaderCrc = 0;
        quint32 expectedFlashHeaderCrc = 0;
    };

    using SaveCompletion = std::function<void(bool, const QString &)>;

    void beginCapture(CaptureKind kind);
    void handleMonitorPackets(const serein::scope::PacketList &packets);
    void handleMonitorSample(const serein::scope::SamplePacket &packet);
    [[nodiscard]] static TelemetryState medianTelemetry(
        const QVector<TelemetryState> &samples);
    [[nodiscard]] bool stateBReady(const TelemetryState &state) const;
    [[nodiscard]] tn160common::StateBGateResult stateBGate(
        const TelemetryState &state) const;
    [[nodiscard]] QString stateBGateMessage(
        const tn160common::StateBGateResult &result) const;
    [[nodiscard]] int stateBFfcDelta(const TelemetryState &state) const;
    void updateThermalStatus(const TelemetryState &state);
    void handleRawFrame(const QByteArray &frame, double mean, quint16 vtemp);
    void stopCapture(bool success, const QString &message);
    void finishCapture(bool success, const QString &message);
    void storeCurrentCapture();
    CaptureData *captureFor(CaptureKind kind);
    const CaptureData *captureFor(CaptureKind kind) const;
    static bool decodeRawFrame(const QByteArray &frame,
                               QVector<quint16> *pixels,
                               double *mean,
                               quint16 *vtemp);
    static bool calculateMeanImage(CaptureData *capture, QString *error);
    bool calculateCalibration(QString *error);
    bool applySinglePointOffset(QString *error);

    void prepareWritePacket();
    void beginWriteAfterPersist();
    void handleWriteReply(quint8 status, quint8 detail,
                          quint32 payloadCrc, quint32 headerCrc,
                          quint32 flashHeaderCrc);
    void failOperation(const QString &message);
    void updateControls();
    void updatePreviews();
    void setStatus(const QString &state, const QString &text);
    void log(const QString &message, const QString &level = QStringLiteral("info"));

    bool ensureSessionDirectory(QString *error);
    [[nodiscard]] ArchiveSnapshot archiveSnapshot() const;
    [[nodiscard]] QList<QPair<QString, CaptureData>> failedCaptures() const;
    void savePackageAsync(const QString &directory,
                          QList<QPair<QString, CaptureData>> failedCaptures,
                          SaveCompletion completion);
    static bool writePackageSnapshot(const ArchiveSnapshot &snapshot,
                                     const QString &directory,
                                     const QList<QPair<QString, CaptureData>> &failedCaptures,
                                     QString *error);
    bool loadPackage(const QString &fileName, QString *error);
    static bool writeAtomic(const QString &fileName,
                            const QByteArray &data, QString *error);
    static QByteArray captureBytes(const CaptureData &capture);
    static QByteArray acceptanceCsv(const CaptureData &capture);
    static QByteArray monitorTraceCsv(const QVector<TelemetryState> &trace);
    static QByteArray meanImageBytes(const QVector<quint16> &pixels);
    static QByteArray mapToGray(const QVector<quint16> &values);
    static QByteArray gainToGray(const QByteArray &gainBytes);
    static double acceptedMeanSpan(const CaptureData &capture);
    static int acceptedVtempSpan(const CaptureData &capture);
    static QString badPointCoordinates(const QByteArray &badPoints);
    static quint32 crc32(const QByteArray &data);
    static QString sha256(const QByteArray &data);
    static QString safeLabel(const QString &label);

    IHostServices *m_host = nullptr;
    BackendIoTaskGroup m_ioTasks;
    SerialDataSession m_transport;
    CalibrationTransfer m_verifyTransfer;
    QTimer m_timeout;
    QElapsedTimer m_captureSilence;
    Operation m_operation = Operation::Idle;
    CaptureKind m_captureKind = CaptureKind::None;
    WriteStage m_writeStage = WriteStage::Idle;
    bool m_captureRestoring = false;
    bool m_captureSuccess = false;
    bool m_captureGapActive = false;
    bool m_waitingCloseForVerify = false;
    bool m_saveActive = false;
    bool m_shuttingDown = false;
    QString m_captureMessage;
    QString m_sessionPath;
    QString m_portForVerify;
    QByteArray m_writeHeader;
    quint32 m_writePayloadCrc = 0;
    quint32 m_writeHeaderCrc = 0;
    quint32 m_expectedFlashHeaderCrc = 0;
    QVector<QByteArray> m_verifyPayloads;
    QVector<TelemetryState> m_recentTelemetry;
    QVector<TelemetryState> m_postCaptureTelemetry;
    QVector<TelemetryState> m_monitorTrace;
    TelemetryState m_stateAReference;
    qint64 m_lastTraceTimestampUs = 0;
    int m_lastMonitorNucCount = -1;
    qint32 m_cumulativeNucDg = 0;
    bool m_monitorContinuityValid = true;
    bool m_stateBReady = false;
    bool m_cycleWarningIssued = false;
    CaptureData m_low;
    CaptureData m_high;
    CaptureData m_highB;
    CaptureData m_offset;
    CaptureData m_currentCapture;
    CalibrationData m_calibration;
    Tn160ProductionFrameFilter m_frameFilter;

    QComboBox *m_port = nullptr;
    QWidget *m_contextPanel = nullptr;
    QPushButton *m_connectionButton = nullptr;
    QLineEdit *m_boardId = nullptr;
    QLineEdit *m_sensorId = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_session = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_newSensor = nullptr;
    QPushButton *m_lowButton = nullptr;
    QPushButton *m_highButton = nullptr;
    QPushButton *m_highBButton = nullptr;
    QPushButton *m_computeButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_offsetButton = nullptr;
    QPushButton *m_writeButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    ThermalView *m_lowView = nullptr;
    ThermalView *m_highView = nullptr;
    ThermalView *m_diffView = nullptr;
    ThermalView *m_backgroundView = nullptr;
    ThermalView *m_gainView = nullptr;
    QLabel *m_quality = nullptr;
};

} // namespace serein::scope::tn160
