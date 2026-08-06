#include "CalibrationTransfer.h"

#include <QtCore/QTimer>

namespace serein::scope::tn160 {

CalibrationTransfer::CalibrationTransfer(QObject *parent)
    : QObject(parent), m_transport(this)
{
    m_timeout.setSingleShot(true);
    connect(&m_transport, &SerialTransport::stateChanged,
            this, &CalibrationTransfer::handleState);
    connect(&m_transport, &SerialTransport::dataReceived,
            this, &CalibrationTransfer::handleData);
    connect(&m_transport, &SerialTransport::errorOccurred,
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
    m_buffer.clear();
    m_payloadIndex = 0;
    m_completionEmitted = false;
    m_payloadSizes = mode == Mode::Calibrate
        ? QList<int>{38400, 38400, 38400, 76800, 10}
        : QList<int>{38400, 76800, 10};
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
            m_stage = Stage::TriggerSent;
            m_transport.write(QByteArray(1, command));
            emit progress(tr("命令 0x%1 已发送")
                              .arg(quint8(command), 2, 16, QLatin1Char('0')));
            QTimer::singleShot(120, this, [this] {
                if (m_stage == Stage::TriggerSent) {
                    m_stage = Stage::TriggerClosing;
                    m_transport.closeAfterWrites();
                }
            });
        } else if (m_stage == Stage::ReadOpening) {
            m_stage = Stage::Reading;
            m_timeout.start(130000);
            emit progress(tr("数据通道已连接 @ %1 baud，等待 payload").arg(m_readBaud));
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
    } else if (m_stage == Stage::Closing) {
        m_stage = Stage::Idle;
    }
}

void CalibrationTransfer::handleData(const QByteArray &data)
{
    if (m_stage != Stage::Reading) {
        return;
    }
    m_buffer.append(data);
    parseBuffer();
}

void CalibrationTransfer::parseBuffer()
{
    static const QByteArray sync("\xff\xff", 2);
    while (true) {
        const qsizetype start = m_buffer.indexOf(sync);
        if (start < 0) {
            if (m_buffer.size() > 1) m_buffer = m_buffer.right(1);
            return;
        }
        if (start > 0) m_buffer.remove(0, start);
        if (m_buffer.size() < 3) return;

        const quint8 type = quint8(m_buffer.at(2));
        if (type == 0xdd) {
            if (m_payloadIndex >= m_payloadSizes.size()) {
                finish(false, tr("收到多余的数据 payload"));
                return;
            }
            const int size = m_payloadSizes.at(m_payloadIndex);
            if (m_buffer.size() < 3 + size) return;
            const QByteArray payload = m_buffer.mid(3, size);
            m_buffer.remove(0, 3 + size);
            emit payloadReady(m_payloadIndex, payload);
            emit progress(tr("payload %1/%2 · %3 bytes")
                              .arg(m_payloadIndex + 1)
                              .arg(m_payloadSizes.size())
                              .arg(size));
            ++m_payloadIndex;
            m_timeout.start(130000);
            continue;
        }
        if (type == 0xee) {
            if (m_buffer.size() < 5) return;
            const quint8 code = quint8(m_buffer.at(3));
            const quint8 length = quint8(m_buffer.at(4));
            if (m_buffer.size() < 5 + length) return;
            const QString message = QString::fromLatin1(m_buffer.constData() + 5, length);
            m_buffer.remove(0, 5 + length);
            if (code == 0 && m_payloadIndex == m_payloadSizes.size()) {
                finish(true, message.isEmpty() ? tr("传输完成") : message);
            } else {
                finish(false, tr("设备返回错误 %1：%2").arg(code).arg(message));
            }
            return;
        }
        m_buffer.remove(0, 1);
    }
}

void CalibrationTransfer::handleError(const QString &message)
{
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
    if (m_completionEmitted) {
        return;
    }
    m_completionEmitted = true;
    m_timeout.stop();
    emit completed(success, message);
    if (m_transport.isActive()) {
        m_stage = Stage::Closing;
        m_transport.close();
    } else {
        m_stage = Stage::Idle;
    }
}

} // namespace serein::scope::tn160
