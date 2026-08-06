#pragma once

#include "CalibrationTransfer.h"

#include <serein/scope/PluginInterface.h>

#include <QtCore/QPoint>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace serein::scope::tn160 {

class ThermalView;

class CalibrationPage final : public QWidget {
    Q_OBJECT

public:
    explicit CalibrationPage(CalibrationTransfer::Mode mode, IHostServices *host,
                             QWidget *parent = nullptr);
    void shutdown();

public slots:
    void refreshPorts();
    void startTransfer();
    void setPort(const QString &port);
    void setTriggerBaud(int baud);
    void setReadBaud(int baud);
    [[nodiscard]] bool transferActive() const noexcept;

signals:
    void transferStateChanged(const QString &state, const QString &text);

private slots:
    void appendProgress(const QString &message);
    void receivePayload(int index, const QByteArray &payload);
    void transferCompleted(bool success, const QString &message);

private:
    void renderResults();
    static QVector<quint16> decodeU16(const QByteArray &payload, bool bigEndian);
    static QVector<float> decodeFloat32Le(const QByteArray &payload);
    static QVector<QPoint> decodeBadPoints(const QByteArray &payload);
    static QByteArray mapToGray(const QVector<double> &values,
                                double lowerFraction = 0.0,
                                double upperFraction = 1.0);
    static QString scalarStats(const QString &name, const QVector<double> &values);

    CalibrationTransfer::Mode m_mode;
    IHostServices *m_host;
    CalibrationTransfer m_transfer;
    QVector<QByteArray> m_payloads;
    QComboBox *m_port = nullptr;
    QSpinBox *m_triggerBaud = nullptr;
    QSpinBox *m_readBaud = nullptr;
    QPushButton *m_run = nullptr;
    QLabel *m_state = nullptr;
    ThermalView *m_lowImage = nullptr;
    ThermalView *m_highImage = nullptr;
    ThermalView *m_deltaImage = nullptr;
    ThermalView *m_backgroundImage = nullptr;
    ThermalView *m_gainImage = nullptr;
    QLabel *m_badPointSummary = nullptr;
};

} // namespace serein::scope::tn160
