#pragma once

#include <QtWidgets/QWidget>

#include <QtCore/QByteArray>
#include <QtCore/QVector>

class QLabel;
class QPushButton;

namespace serein::scope::t384 {

/** Host-side two-point calibration for T256/T384/T640 Y16BE frames.
 *
 * This deliberately stores an experimental model only; no device write or
 * OEM calibration-table access is attempted.
 */
class T384CalibrationPage final : public QWidget {
    Q_OBJECT
public:
    explicit T384CalibrationPage(QWidget *parent = nullptr);

    void ingestFrame(const QByteArray &y16be, int width, int height);
    void ingestFrame(const QByteArray &y16be);
    [[nodiscard]] QWidget *contextPanel() const noexcept { return m_contextPanel; }
    [[nodiscard]] bool modelEnabled() const noexcept { return m_modelEnabled; }
    [[nodiscard]] double zeroRaw() const noexcept { return m_zeroRaw; }
    [[nodiscard]] double countsPerC() const noexcept { return m_countsPerC; }
    void setDeviceIdentity(const QByteArray &identity);

signals:
    void modelChanged(bool enabled, double zeroRaw, double countsPerC);
    void uploadRequested(const QByteArray &payload);
    void readbackRequested();
    void commitRequested();
    void abortRequested();

private slots:
    void captureZero();
    void captureHigh();
    void importModel();
    void exportModel();
    void uploadModel();
    void readbackModel();
    void commitModel();
    void abortModel();
public slots:
    void onCalibrationResult(const QString &operation, bool success, const QString &message);
    void applyDeviceData(const QByteArray &data);

private:
    void updateUi();
    void fitModel();
    bool saveJson(const QString &path) const;
    bool loadJson(const QString &path);
    static double roiMean(const QByteArray &frame, int width, int height);

    QWidget *m_contextPanel = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_stats = nullptr;
    QLabel *m_live = nullptr;
    QPushButton *m_zeroButton = nullptr;
    QPushButton *m_highButton = nullptr;
    QVector<double> m_zeroSamples;
    QVector<double> m_highSamples;
    double m_lastRaw = 0.0;
    double m_zeroRaw = 0.0;
    double m_countsPerC = 0.0;
    bool m_modelEnabled = false;
    bool m_captureZero = false;
    bool m_captureHigh = false;
    QByteArray m_lastPackage;
    int m_frameWidth = 256;
    int m_frameHeight = 192;
    QByteArray m_deviceIdentity;
};

} // namespace serein::scope::t384
