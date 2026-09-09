#pragma once

#include "CalibrationTransfer.h"
#include "Tn160Engine.h"
#include "Tn160CalibrationValidation.h"
#include "Tn160ProductionFrameFilter.h"
#include "Tn160ProductionWorkflow.h"

#include <serein/scope/BackendIoTaskGroup.h>
#include <serein/scope/PluginInterface.h>
#include "Tn160NcmTransport.h"

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QPoint>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

#include <functional>
#include <limits>
#include <memory>

class QEvent;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace serein::scope::tn160ncm {

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
    void resumeAutomaticConnection();
    void connectAutomatically();
    void scanAndConnect();
    void toggleConnection();
    [[nodiscard]] bool connectionActive() const noexcept;
    [[nodiscard]] int baudRate() const noexcept;
    void prepareForProductRecalibration(const QString &serial);
    void prepareForSingleDevice();
    void notifyFirmwareFlashed(const QString &firmwarePath,
                               const QString &firmwareSha256,
                               const QString &bootselRoot);
    void captureLow();
    void captureHigh();
    void writeTestAndVerify();
    void writeAndVerify();
    void retryWriteVerification();
    void restartCurrentRun();
    void importCalibration();
    void exportCalibration();
    [[nodiscard]] bool operationActive() const noexcept;
    [[nodiscard]] QWidget *contextPanel() const noexcept;
    [[nodiscard]] QWidget *outputPanel() const noexcept;
    void resetCalibrationRun();
    void prepareForStation(int segment,
                           const QString &expectedSerial = {});
    void grantFinalization();
    void setStationPresentation(int station, const QString &serial,
                                const QString &state);
    void setStationSelected(bool selected);

    [[nodiscard]] QString deviceSerial() const;
    [[nodiscard]] int stationSegment() const noexcept;
    [[nodiscard]] bool finalProductReady() const noexcept;
    [[nodiscard]] bool productAddressCommitted() const noexcept;
    [[nodiscard]] bool productionWriteAvailable() const noexcept;
    [[nodiscard]] bool testWriteAvailable() const noexcept;
    [[nodiscard]] double ntcCoverageC() const noexcept;
    [[nodiscard]] QString workflowState() const;
    [[nodiscard]] QString sessionPath() const;

signals:
    void transferStateChanged(const QString &state, const QString &text);
    void deviceIdentified(ProductionCalibrationPage *page,
                          const QString &serial, int segment);
    void deviceConnected(ProductionCalibrationPage *page,
                         const QString &serial, int segment);
    void deviceDisconnected(ProductionCalibrationPage *page,
                            const QString &serial, int segment);
    void finalizationRequested(ProductionCalibrationPage *page);
    void finalProductReadyForRemoval(ProductionCalibrationPage *page,
                                     const QString &serial);
    void finalProductRemoved(ProductionCalibrationPage *page,
                             const QString &serial);
    void finalizationFailed(ProductionCalibrationPage *page,
                            const QString &message);
    void productRecalibrationRequested(ProductionCalibrationPage *page);
    void workspaceActivated(ProductionCalibrationPage *page);
    void livePacketsDecoded(const serein::scope::PacketList &packets);
    void workflowStateChanged(const QVariantMap &state);
    void continuousRecordingChanged(bool active, const QString &path);
    void calibrationCompleted(const QString &serial, bool productionPass,
                              const QString &sessionPath);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void computeTwoPoint();
    void handleTransportState(const QString &state);
    void handleDecodedPackets(const serein::scope::PacketList &packets);
    void handleTransportError(const QString &message);
    void handleTimeout();
    void receiveVerifyHeader(const QByteArray &header);
    void receiveVerifyPayload(int index, const QByteArray &payload);
    void verifyCompleted(bool success, const QString &message);
    void requestProductRecalibration();

private:
    enum class CaptureKind { None, Low, High };
    enum class Operation { Idle, Capturing, Writing, Verifying };
    enum class VerifyPhase { None, TemporaryAddress, ProductAddress };
    enum class FinalizationStage {
        None,
        WaitingGrant,
        ReconnectingTemporary,
        SwitchingToProduct,
        ReconnectingProduct,
        VerifyingProduct,
        AwaitingProductMonitor,
        ValidatingProduct,
        SavingValidation,
        AwaitingRemoval,
    };
    static QString captureName(CaptureKind kind);
    static int requiredFramesFor(CaptureKind kind);

    struct TelemetryState {
        bool valid = false;
        qint64 timestampUs = 0;
        quint32 frameNumber = 0;
        quint32 deviceTimestampMs = 0;
        quint32 calibrationId = 0;
        double centerRoiC = std::numeric_limits<double>::quiet_NaN();
        quint16 centerRoiAdu = 0;
        double ntcC = 0.0;
        quint16 ntcAdu = 0;
        quint16 vtempAdu = 0;
        quint16 smoothVtempAdu = 0;
        quint8 nucDecision = 0xff;
        quint16 nucCount = 0;
        qint16 nucDg = 0;
        qint16 nucDv = 0;
        qint16 nucDn = 0;
        qint32 cumulativeNucDg = 0;
        quint16 bootNtcAdu = 0;
        qint32 anchor = 0;
        qint32 effectiveAnchor = 0;
        float meanDiff = 0.0f;
        bool ntcModelReported = false;
        bool ntcModelValid = false;
        quint16 ntcModelReferenceAdu = 0;
        qint16 ntcModelKQ8 = 0;
        quint16 ntcModelLimitAdu = 0;
        bool startupModelValid = false;
        bool startupModelActive = false;
        quint16 startupModelNtcThresholdAdu = 0;
        qint16 startupModelAmplitudeAdu = 0;
        quint16 startupModelTauDs = 0;
        quint8 ffcState = 0xff;
        quint16 rowRefMeanAdu = 0;
        quint16 rowRefSpanAdu = 0;
        quint16 shutterMeanAdu = 0;
        bool rowRefModelReported = false;
        bool rowRefModelValid = false;
        bool rowRefModelActive = false;
        bool rowRefSampleValid = false;
        quint16 rowRefModelReferenceAdu = 0;
        qint16 rowRefModelKQ8 = 0;
        quint16 rowRefModelLimitAdu = 0;
        qint16 rowRefCompAdu = 0;
    };

    struct ColdStartRawRecord {
        QByteArray record;
        TelemetryState telemetry;
        quint32 deviceTimestampMs = 0;
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
        bool coldStartModelAttempted = false;
        bool writeVerified = false;
        NtcAnchorModelResult ntcModel;
        ColdStartModelResult coldStartModel;
        RowRefModelResult rowRefModel;
        bool postWriteValidationAttempted = false;
        bool postWriteValidationPassed = false;
        int postWriteValidationSamples = 0;
        double postWriteMeanC = std::numeric_limits<double>::quiet_NaN();
        double postWritePeakToPeakC = std::numeric_limits<double>::quiet_NaN();
        bool testWrite = false;
    };

    struct ArchiveSnapshot {
        bool singleDeviceMode = false;
        QString boardId;
        QString sensorId;
        QString firmwareIdentity;
        QString firmwarePath;
        QString firmwareSha256;
        QString bootselRoot;
        CaptureData low;
        CaptureData high;
        QVector<ColdStartSample> coldStartSamples;
        quint64 coldStartPairRejected = 0;
        QVector<TelemetryState> monitorTrace;
        bool monitorContinuityValid = true;
        TelemetryState connectionReference;
        double ntcMinimumC = 0.0;
        double ntcMaximumC = 0.0;
        quint16 ntcMinimumAdu = 0;
        quint16 ntcMaximumAdu = 0;
        CalibrationData calibration;
        quint32 writePayloadCrc = 0;
        quint32 writeHeaderCrc = 0;
        quint32 writeFlashHeaderCrc = 0;
    };

    using SaveCompletion = std::function<void(bool, const QString &)>;

    void beginCapture(CaptureKind kind);
    void requestCapture(CaptureKind kind);
    [[nodiscard]] ProductionWorkflowActions actionAvailability() const;
    [[nodiscard]] QString captureBlockedReason(CaptureKind kind) const;
    void invalidateDownstreamAfterCapture(CaptureKind kind);
    void clearWriteAttemptState();
    void updateWorkflowAfterCapture(CaptureKind kind);
    void confirmMonitorConnection();
    void handleMonitorPackets(const serein::scope::PacketList &packets);
    void handleMonitorSample(const serein::scope::SamplePacket &packet);
    [[nodiscard]] static TelemetryState medianTelemetry(
        const QVector<TelemetryState> &samples);
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
    [[nodiscard]] bool requiredAnchorFromAdc16(const QByteArray &record,
                                                double *requiredAnchor) const;
    [[nodiscard]] bool coldStartCaptureReady() const noexcept;
    [[nodiscard]] bool buildColdStartSamples();
    [[nodiscard]] static QString coldStartModelFailureText(
        ColdStartModelFailure failure);
    [[nodiscard]] bool reportedNtcModelMatches(
        const TelemetryState &state, QString *error) const;
    void beginPostWriteValidation(const TelemetryState &state);
    void consumePostWriteValidation(const TelemetryState &state);
    void finishPostWriteValidation(bool passed, const QString &message);

    void prepareWritePacket();
    void beginWriteAndVerify(bool testWrite);
    void beginWriteUpload();
    void completeNcmWrite(quint32 payloadCrc, quint32 headerCrc,
                          quint32 flashHeaderCrc);
    void failOperation(const QString &message);
    void updateControls();
    void updatePreviews();
    void updateQualitySummary();
    void setStatus(const QString &state, const QString &text);
    void log(const QString &message, const QString &level = QStringLiteral("info"));
    void emitWorkflowState();
    void startContinuousRecording();
    void stopContinuousRecording();
    void handleMonitorFrameMetadata(qint64 hostTimestampUs,
                                    quint32 frameNumber,
                                    quint32 deviceTimestampMs,
                                    quint32 calibrationId,
                                    quint16 ffcCount);
    void handleContinuousAdc16Record(qint64 hostTimestampUs,
                                     const QByteArray &record,
                                     quint32 sourceFrameNumber,
                                     quint32 deviceTimestampMs,
                                     quint32 calibrationId,
                                     quint16 ffcCount);
    void updateNtcCoverage(const TelemetryState &state);

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
    Tn160NcmTransport m_transport;
    CalibrationTransfer m_verifyTransfer;
    QTimer m_timeout;
    QTimer m_monitorFrameTimeout;
    QElapsedTimer m_captureSilence;
    Operation m_operation = Operation::Idle;
    CaptureKind m_captureKind = CaptureKind::None;
    bool m_captureRestoring = false;
    bool m_captureSuccess = false;
    bool m_captureGapActive = false;
    bool m_waitingCloseForVerify = false;
    bool m_autoConnectPaused = false;
    bool m_saveActive = false;
    bool m_shuttingDown = false;
    bool m_singleDeviceMode = false;
    VerifyPhase m_verifyPhase = VerifyPhase::None;
    FinalizationStage m_finalizationStage = FinalizationStage::None;
    QString m_deviceSerial;
    QString m_endpoint = QStringLiteral("http://192.168.10.1");
    QString m_expectedSerial;
    int m_stationSegment = 0;
    bool m_productAddressCommitted = false;
    bool m_monitorReadyAnnounced = false;
    QString m_deviceFirmwareIdentity;
    QString m_captureMessage;
    QString m_sessionPath;
    QString m_portForVerify;
    QString m_firmwarePath;
    QString m_firmwareSha256;
    QString m_bootselRoot;
    QByteArray m_writeHeader;
    quint32 m_writePayloadCrc = 0;
    quint32 m_writeHeaderCrc = 0;
    quint32 m_writeFlashHeaderCrc = 0;
    QByteArray m_verifyHeader;
    QVector<QByteArray> m_verifyPayloads;
    QVector<TelemetryState> m_recentTelemetry;
    QVector<TelemetryState> m_postCaptureTelemetry;
    QVector<TelemetryState> m_monitorTrace;
    QVector<TelemetryState> m_telemetryHistory;
    QVector<TelemetryState> m_postWriteValidation;
    TelemetryState m_connectionReference;
    double m_ntcMinimumC = 0.0;
    double m_ntcMaximumC = 0.0;
    quint16 m_ntcMinimumAdu = 0;
    quint16 m_ntcMaximumAdu = 0;
    qint64 m_pendingMonitorHostTimestampUs = 0;
    quint32 m_pendingMonitorFrameNumber = 0;
    quint32 m_pendingMonitorDeviceTimestampMs = 0;
    quint32 m_pendingMonitorCalibrationId = 0;
    qint64 m_lastTraceTimestampUs = 0;
    qint64 m_lastGateUiSecond = -1;
    int m_lastMonitorNucCount = -1;
    qint32 m_cumulativeNucDg = 0;
    bool m_monitorContinuityValid = true;
    bool m_disconnectCleanupPending = false;
    bool m_disconnectWasReady = false;
    QString m_disconnectCleanupSerial;
    bool m_writeCommitAttempted = false;
    bool m_writeAwaitingVerification = false;
    QString m_workflowState = QStringLiteral("idle");
    std::unique_ptr<QFile> m_continuousAdcFile;
    std::unique_ptr<QFile> m_continuousIndexFile;
    quint64 m_continuousRecordCount = 0;
    QVector<ColdStartRawRecord> m_coldStartRecords;
    QVector<ColdStartSample> m_coldStartSamples;
    quint64 m_coldStartPairRejected = 0;
    bool m_coldStartWindowMissed = false;
    CaptureData m_low;
    CaptureData m_high;
    CaptureData m_currentCapture;
    CalibrationData m_calibration;
    Tn160ProductionFrameFilter m_frameFilter;

    QWidget *m_contextPanel = nullptr;
    QWidget *m_outputPanel = nullptr;
    QFrame *m_rowFrame = nullptr;
    QPushButton *m_recalibrateButton = nullptr;
    QLabel *m_stationTitle = nullptr;
    QLabel *m_stationDevice = nullptr;
    QLabel *m_rowStatus = nullptr;
    QLabel *m_ntcStatus = nullptr;
    QPushButton *m_lowButton = nullptr;
    QPushButton *m_highButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_restartButton = nullptr;
    QPushButton *m_writeButton = nullptr;
    QPushButton *m_testWriteButton = nullptr;
    QPushButton *m_retryVerifyButton = nullptr;
    QPlainTextEdit *m_productionLog = nullptr;
    ThermalView *m_lowView = nullptr;
    ThermalView *m_highView = nullptr;
    ThermalView *m_diffView = nullptr;
    ThermalView *m_backgroundView = nullptr;
    ThermalView *m_gainView = nullptr;
    QLabel *m_quality = nullptr;
};

} // namespace serein::scope::tn160ncm
