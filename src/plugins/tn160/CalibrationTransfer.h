#pragma once

#include <serein/scope/SerialTransport.h>

#include <QtCore/QObject>
#include <QtCore/QTimer>

namespace serein::scope::tn160 {

class CalibrationTransfer final : public QObject {
    Q_OBJECT

public:
    enum class Mode { Calibrate, Dump };

    explicit CalibrationTransfer(QObject *parent = nullptr);
    void start(Mode mode, const QString &port, int triggerBaud, int readBaud);
    void cancel();
    [[nodiscard]] bool isActive() const noexcept;

signals:
    void progress(const QString &message);
    void payloadReady(int index, const QByteArray &payload);
    void completed(bool success, const QString &message);

private slots:
    void handleState(const QString &state);
    void handleData(const QByteArray &data);
    void handleError(const QString &message);
    void handleTimeout();

private:
    enum class Stage {
        Idle,
        TriggerOpening,
        TriggerSent,
        TriggerClosing,
        ReadOpening,
        Reading,
        Closing,
    };

    void parseBuffer();
    void finish(bool success, const QString &message);

    SerialTransport m_transport;
    QTimer m_timeout;
    Stage m_stage = Stage::Idle;
    Mode m_mode = Mode::Dump;
    QString m_port;
    int m_readBaud = 5000000;
    QByteArray m_buffer;
    QList<int> m_payloadSizes;
    int m_payloadIndex = 0;
    bool m_completionEmitted = false;
};

} // namespace serein::scope::tn160
