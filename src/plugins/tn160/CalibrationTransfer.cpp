#include "CalibrationTransfer.h"
#include "Tn160ProtocolEngines.h"

#include <QtCore/QTimer>

namespace serein::scope::tn160 {
namespace {

constexpr int ModeCommandDrainMs = 120;
constexpr int ReopenDelayMs = 350;
constexpr int MonitorBaud = 2000000;
constexpr quint8 MonitorCommand = 0x11;
constexpr quint8 ExitToUvcCommand = 0x99;

int recoveryHighBaud(const int readBaud)
{
#ifdef Q_OS_WIN
    // The Windows CH346 driver maps the TN160 5 Mbaud receive setting to a
    // divisor whose reliable transmit-side match is 6 Mbaud.  Both settings
    // decode the same 19255-byte stream, while only 6 Mbaud has been observed
    // to deliver the 0x11 recovery byte back to the device.
    if (readBaud == 5000000) return 6000000;
#endif
    return readBaud;
}

} // namespace

CalibrationTransfer::CalibrationTransfer(QObject *parent)
    : QObject(parent),
      m_transport(std::make_unique<Tn160DiscardEngine>(),
                  DecoderRuntime::configuredLimits(), this)
{
    m_timeout.setSingleShot(true);
    connect(&m_transport, &SerialDataSession::stateChanged,
            this, &CalibrationTransfer::handleState);
    connect(&m_transport, &SerialDataSession::packetsDecoded,
            this, &CalibrationTransfer::handleDecodedPackets);
    connect(&m_transport, &SerialDataSession::errorOccurred,
            this, &CalibrationTransfer::handleError);
    connect(&m_timeout, &QTimer::timeout,
            this, &CalibrationTransfer::handleTimeout);
}

void CalibrationTransfer::start(const Mode mode, const QString &port,
                                const int triggerBaud, const int readBaud)
{
    if (isActive()) {
        emit completed(false, tr("已有标定传输正在运行"));
        return;
    }
    m_mode = mode;
    m_port = port;
    m_readBaud = readBaud;
    m_payloadIndex = 0;
    m_completionEmitted = false;
    m_pendingSuccess = false;
    m_pendingMessage.clear();
    m_payloadSizes = mode == Mode::Calibrate
        ? QList<int>{38400, 38400, 38400, 76800, 10}
        : QList<int>{38400, 76800, 10};
    QString engineError;
    if (!m_transport.replaceEngine(
            std::make_unique<Tn160CalibrationStreamEngine>(m_payloadSizes),
            &engineError)) {
        emit completed(false, tr("无法启动标定数据解析：%1").arg(engineError));
        return;
    }
    m_stage = Stage::TriggerOpening;
    emit progress(tr("触发命令 @ %1 baud").arg(triggerBaud));
    m_transport.open({port, triggerBaud});
}

void CalibrationTransfer::cancel()
{
    if (!isActive()) {
        return;
    }
    m_timeout.stop();
    m_stage = Stage::Closing;
    if (m_transport.isActive()) {
        m_transport.close();
    } else {
        m_stage = Stage::Idle;
    }
}

bool CalibrationTransfer::isActive() const noexcept
{
    return m_stage != Stage::Idle;
}

void CalibrationTransfer::handleState(const QString &state)
{
    if (state == QStringLiteral("connected")) {
        if (m_stage == Stage::TriggerOpening) {
            const char command = m_mode == Mode::Calibrate ? char(0xcc) : char(0xdd);
            m_stage = Stage::TriggerClosing;
            m_transport.closeAfterWrites(QByteArray(1, command),
                                         ModeCommandDrainMs);
            emit progress(tr("命令 0x%1 已发送")
                              .arg(quint8(command), 2, 16, QLatin1Char('0')));
        } else if (m_stage == Stage::ReadOpening) {
            m_stage = Stage::Reading;
            m_timeout.start(130000);
            emit progress(tr("数据通道已连接 @ %1 baud，等待 payload").arg(m_readBaud));
        } else if (m_stage == Stage::RestoreHighOpening) {
            m_stage = Stage::RestoreHighCommandClosing;
            m_transport.closeAfterWrites(QByteArray(1, char(MonitorCommand)),
                                         ModeCommandDrainMs);
            emit progress(tr("恢复 0x11 已发送 @ %1 baud")
                              .arg(recoveryHighBaud(m_readBaud)));
        } else if (m_stage == Stage::RestoreUvcOpening) {
            m_stage = Stage::RestoreUvcCommandClosing;
            m_transport.closeAfterWrites(QByteArray(1, char(ExitToUvcCommand)),
                                         ModeCommandDrainMs);
            emit progress(tr("恢复 0x99 已发送 @ %1 baud").arg(MonitorBaud));
        }
        return;
    }

    if (state != QStringLiteral("disconnected")) {
        return;
    }
    if (m_stage == Stage::TriggerClosing) {
        m_stage = Stage::ReadOpening;
        emit progress(tr("切换到 %1 baud 接收数据").arg(m_readBaud));
        QTimer::singleShot(350, this, [this] {
            if (m_stage == Stage::ReadOpening) {
                m_transport.open({m_port, m_readBaud});
            }
        });
    } else if (m_stage == Stage::RestoreHighClosing) {
        m_stage = Stage::RestoreHighOpening;
        emit progress(tr("打开恢复通道 @ %1 baud")
                          .arg(recoveryHighBaud(m_readBaud)));
        QTimer::singleShot(ReopenDelayMs, this, [this] {
            if (m_stage == Stage::RestoreHighOpening) {
                m_transport.open({m_port, recoveryHighBaud(m_readBaud)});
            }
        });
    } else if (m_stage == Stage::RestoreHighCommandClosing) {
        m_stage = Stage::RestoreUvcOpening;
        emit progress(tr("切回控制通道 @ %1 baud").arg(MonitorBaud));
        QTimer::singleShot(ReopenDelayMs, this, [this] {
            if (m_stage == Stage::RestoreUvcOpening) {
                m_transport.open({m_port, MonitorBaud});
            }
        });
    } else if (m_stage == Stage::RestoreUvcCommandClosing) {
        completePending();
    } else if (m_stage == Stage::RecoveryFailedClosing) {
        completePending();
    } else if (m_stage == Stage::Closing) {
        m_stage = Stage::Idle;
    }
}

void CalibrationTransfer::handleDecodedPackets(const PacketList &packets)
{
    if (m_stage != Stage::Reading) return;
    for (const Packet &packet : packets) {
        if (const auto *payload = std::get_if<ImagePacket>(&packet);
            payload && payload->source
                    == QString::fromLatin1(Tn160CalibrationStreamSource)) {
            const int index = payload->width - 1;
            if (index != m_payloadIndex || index < 0
                || index >= m_payloadSizes.size()
                || payload->pixels.size() != m_payloadSizes.at(index)) {
                finish(false, tr("标定 payload 顺序或长度异常"));
                return;
            }
            emit payloadReady(index, payload->pixels);
            emit progress(tr("payload %1/%2 · %3 bytes")
                              .arg(index + 1)
                              .arg(m_payloadSizes.size())
                              .arg(payload->pixels.size()));
            ++m_payloadIndex;
            m_timeout.start(130000);
            continue;
        }
        if (const auto *event = std::get_if<EventPacket>(&packet);
            event && event->category
                    == QString::fromLatin1(Tn160CalibrationStreamSource)) {
            const QString kind = event->fields.value(QStringLiteral("kind")).toString();
            if (kind == QStringLiteral("protocol-error")) {
                finish(false, tr("收到多余的数据 payload"));
                return;
            }
            const int code = event->fields.value(QStringLiteral("code")).toInt();
            const int count = event->fields.value(
                QStringLiteral("payload_count")).toInt();
            if (kind == QStringLiteral("completed") && code == 0
                && count == m_payloadSizes.size()
                && m_payloadIndex == m_payloadSizes.size()) {
                finish(true, event->message.isEmpty() ? tr("传输完成")
                                                       : event->message);
            } else {
                finish(false, tr("设备返回错误 %1：%2")
                                  .arg(code).arg(event->message));
            }
            return;
        }
    }
}

void CalibrationTransfer::handleError(const QString &message)
{
    if (recoveryActive()) {
        failRecovery(message);
        return;
    }
    if (isActive()) {
        finish(false, message);
    }
}

void CalibrationTransfer::handleTimeout()
{
    finish(false, tr("等待标定数据超时"));
}

void CalibrationTransfer::finish(const bool success, const QString &message)
{
    if (m_completionEmitted || recoveryActive()) {
        return;
    }
    m_timeout.stop();
    m_pendingSuccess = success;
    m_pendingMessage = message;
    emit progress(tr("恢复设备到 2 Mbaud/UVC"));
    if (m_transport.isActive()) {
        m_stage = Stage::RestoreHighClosing;
        m_transport.close();
    } else {
        m_stage = Stage::RestoreHighOpening;
        QTimer::singleShot(ReopenDelayMs, this, [this] {
            if (m_stage == Stage::RestoreHighOpening) {
                m_transport.open({m_port, recoveryHighBaud(m_readBaud)});
            }
        });
    }
}

void CalibrationTransfer::completePending()
{
    if (m_completionEmitted) return;
    m_completionEmitted = true;
    m_stage = Stage::Idle;
    emit completed(m_pendingSuccess, m_pendingMessage);
}

void CalibrationTransfer::failRecovery(const QString &message)
{
    if (m_stage == Stage::RecoveryFailedClosing) return;
    m_timeout.stop();
    m_pendingSuccess = false;
    m_pendingMessage = m_pendingMessage.isEmpty()
        ? tr("设备模式恢复失败：%1").arg(message)
        : tr("%1；设备模式恢复失败：%2").arg(m_pendingMessage, message);
    if (m_transport.isActive()) {
        m_stage = Stage::RecoveryFailedClosing;
        m_transport.close();
    } else {
        completePending();
    }
}

bool CalibrationTransfer::recoveryActive() const noexcept
{
    switch (m_stage) {
    case Stage::RestoreHighClosing:
    case Stage::RestoreHighOpening:
    case Stage::RestoreHighCommandClosing:
    case Stage::RestoreUvcOpening:
    case Stage::RestoreUvcCommandClosing:
    case Stage::RecoveryFailedClosing:
        return true;
    default:
        return false;
    }
}

} // namespace serein::scope::tn160
