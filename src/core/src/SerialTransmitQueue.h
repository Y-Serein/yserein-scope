#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QQueue>

#include <optional>

namespace serein::scope::detail {

// SerialWorker's bounded single-consumer queue. stopAccepting() and enqueue()
// share the same lock so a graceful close has an exact acceptance boundary:
// every write accepted before the boundary remains available to the worker,
// and every later write is rejected.
class SerialTransmitQueue final {
public:
    explicit SerialTransmitQueue(const qsizetype capacityBytes)
        : m_capacityBytes(capacityBytes)
    {
    }

    [[nodiscard]] bool enqueue(const QByteArray &data)
    {
        if (data.isEmpty()) return true;
        QMutexLocker lock(&m_mutex);
        if (!m_accepting || data.size() > m_capacityBytes - m_pendingBytes) {
            return false;
        }
        m_payloads.enqueue(data);
        m_pendingBytes += data.size();
        return true;
    }

    void stopAccepting()
    {
        QMutexLocker lock(&m_mutex);
        m_accepting = false;
    }

    [[nodiscard]] std::optional<QByteArray> takeNext()
    {
        QMutexLocker lock(&m_mutex);
        if (m_payloads.isEmpty()) return std::nullopt;
        QByteArray payload = m_payloads.dequeue();
        m_pendingBytes -= payload.size();
        return payload;
    }

    [[nodiscard]] bool hasPending() const
    {
        QMutexLocker lock(&m_mutex);
        return !m_payloads.isEmpty();
    }

private:
    mutable QMutex m_mutex;
    QQueue<QByteArray> m_payloads;
    qsizetype m_capacityBytes = 0;
    qsizetype m_pendingBytes = 0;
    bool m_accepting = true;
};

} // namespace serein::scope::detail
