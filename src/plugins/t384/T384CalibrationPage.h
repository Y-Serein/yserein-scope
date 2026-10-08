#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

class QLabel;
class QPushButton;

namespace serein::scope::t384 {

/** UI-only production-test controller for experimental two-point Y16BE models.
 *
 * The workspace/transport owns HTTP and device writes. This widget accepts
 * complete frames, applies the quality gate, exposes evidence, and asks its
 * owner to atomically apply a model. It never labels this as OEM radiometry.
 */
class T384CalibrationPage final : public QWidget {
    Q_OBJECT
public:
    static constexpr int kSamplesPerPoint = 30;
    explicit T384CalibrationPage(QWidget *parent = nullptr);

    void ingestFrame(const QByteArray &y16be, int width, int height,
                     quint32 sequence = 0, quint32 captureMs = 0);
    void ingestFrame(const QByteArray &y16be);
    void setDeviceIdentity(const QByteArray &identitySha256);
    [[nodiscard]] QWidget *contextPanel() const noexcept { return m_contextPanel; }
    [[nodiscard]] QWidget *resultPanel() const noexcept { return m_resultPanel; }
    [[nodiscard]] QByteArray captureEvidenceJson() const;

signals:
    void calibrationRunStarted();
    void modelChanged(bool enabled, double zeroRaw, double countsPerC);
    // Owner must stop stream, backup, write A/B, read back and verify. On
    // ambiguous transport result it must call onAutoApplyFinished(false,true).
    void autoApplyRequested(const QByteArray &modelPacket, const QByteArray &evidenceJson);
    void verifySavedRequested(const QByteArray &modelPacket,
                              const QByteArray &evidenceJson);
    void boundaryStateRequested(const QString &tag);
    void evidenceCaptured(const QByteArray &evidenceJson);

public slots:
    void onAutoApplyFinished(bool success, bool writeUncertain, const QString &message);
    void onBoundaryStateReady(const QString &tag, bool success,
                              const QVariantMap &snapshot,
                              const QString &message);
    void onTransportInterrupted(const QString &message);
    void setRuntimeModel(bool enabled, double zeroRaw, double countsPerC);

private slots:
    void startZeroCapture();
    void startHighCapture();
    void restartCalibration();
    void importModel();
    void exportModel();
    void verifySaved();

private:
    enum class State { Idle, AwaitingZeroBefore, CapturingZero,
                       AwaitingZeroAfter, AwaitingHighBefore, CapturingHigh,
                       AwaitingHighAfter, QualityFailed,
                       Applying, Applied, WriteUncertain };
    void updateUi();
    void evaluateAndAutoApply();
    void resetCapture();
    [[nodiscard]] bool supportedProfile(int width, int height) const;
    [[nodiscard]] bool qualityGate(QString *reason);
    [[nodiscard]] QByteArray buildModelPacket() const;
    [[nodiscard]] QByteArray buildEvidenceJson(const QString &outcome) const;
    [[nodiscard]] bool saveJson(const QString &path) const;
    bool loadJson(const QString &path);
    static double roiMean(const QByteArray &frame, int width, int height);
    static double sampleStdDev(const QVector<double> &samples);
    bool validateBoundaryPair(const QJsonObject &before,
                              const QJsonObject &after,
                              QString *reason) const;

    QWidget *m_contextPanel = nullptr;
    QWidget *m_resultPanel = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_stats = nullptr;
    QLabel *m_live = nullptr;
    QLabel *m_boundarySummary = nullptr;
    QPushButton *m_zeroButton = nullptr;
    QPushButton *m_highButton = nullptr;
    QPushButton *m_restartButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_verifyButton = nullptr;
    State m_state = State::Idle;
    QVector<double> m_zeroSamples;
    QVector<double> m_highSamples;
    double m_lastRaw = 0.0;
    double m_zeroRaw = 0.0;
    double m_highRaw = 0.0;
    double m_countsPerC = 0.0;
    double m_zeroStdDev = 0.0;
    double m_highStdDev = 0.0;
    bool m_modelEnabled = false;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    QByteArray m_deviceIdentity; // SHA-256(PN:SN:FW), exactly 32 bytes
    QSet<quint32> m_zeroSequences;
    QSet<quint32> m_highSequences;
    quint32 m_lastSequence = 0;
    quint64 m_sequenceGaps = 0;
    quint32 m_lastCaptureMs = 0;
    QByteArray m_pendingPacket;
    QJsonObject m_zeroBefore;
    QJsonObject m_zeroAfter;
    QJsonObject m_highBefore;
    QJsonObject m_highAfter;
    QTimer m_captureTimer;
    QString m_qualityReason;
    QByteArray m_lastEvidence;
};

} // namespace serein::scope::t384
