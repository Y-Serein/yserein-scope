#pragma once

#include <serein/scope/SerialDataSession.h>

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
    void handleDecodedPackets(const serein::scope::PacketList &packets);
    void handleError(const QString &message);
    void handleTimeout();

private:
    enum class Stage {
        Idle,
        TriggerOpening,
        TriggerClosing,
        ReadOpening,
        Reading,
        RestoreHighClosing,
        RestoreHighOpening,
        RestoreHighCommandClosing,
        RestoreUvcOpening,
        RestoreUvcCommandClosing,
        RecoveryFailedClosing,
        Closing,
    };

    void finish(bool success, const QString &message);
    void completePending();
    void failRecovery(const QString &message);
    [[nodiscard]] bool recoveryActive() const noexcept;

    SerialDataSession m_transport;
    QTimer m_timeout;
    Stage m_stage = Stage::Idle;
    Mode m_mode = Mode::Dump;
    QString m_port;
    int m_readBaud = 5000000;
    QList<int> m_payloadSizes;
    int m_payloadIndex = 0;
    bool m_completionEmitted = false;
    bool m_pendingSuccess = false;
    QString m_pendingMessage;
};

} // namespace serein::scope::tn160
