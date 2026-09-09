#pragma once

#include <serein/scope/Packet.h>

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QQueue>

#include <algorithm>
#include <limits>
#include <type_traits>

namespace serein::scope::detail {

inline qsizetype estimatedPacketBytes(const PacketList &packets)
{
    quint64 bytes = 0;
    const auto stringBytes = [](const QString &value) {
        return quint64(value.size()) * sizeof(QChar);
    };
    for (const Packet &packet : packets) {
        bytes += sizeof(Packet);
        std::visit([&](const auto &value) {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, SamplePacket>) {
                bytes += quint64(value.values.size()) * sizeof(double);
                bytes += stringBytes(value.source);
                for (const QString &label : value.labels) bytes += stringBytes(label);
            } else if constexpr (std::is_same_v<Value, ImagePacket>) {
                bytes += quint64(value.pixels.size());
                bytes += stringBytes(value.pixelFormat) + stringBytes(value.source);
            } else if constexpr (std::is_same_v<Value, EventPacket>) {
                bytes += stringBytes(value.level) + stringBytes(value.category)
                    + stringBytes(value.message);
                for (auto it = value.fields.cbegin(); it != value.fields.cend(); ++it) {
                    bytes += stringBytes(it.key()) + stringBytes(it.value().toString());
                }
            } else if constexpr (std::is_same_v<Value, TextPacket>) {
                bytes += stringBytes(value.text) + stringBytes(value.stream);
            }
        }, packet);
    }
    return qsizetype(std::min<quint64>(
        bytes, quint64(std::numeric_limits<qsizetype>::max())));
}

struct FrontendPacketMailboxSnapshot {
    quint64 acceptedBatches = 0;
    quint64 acceptedPackets = 0;
    quint64 rejectedBatches = 0;
    quint64 rejectedPackets = 0;
    qsizetype pendingBatches = 0;
    qsizetype pendingPackets = 0;
    qsizetype pendingPacketBytes = 0;
    qsizetype highWaterPackets = 0;
    qsizetype highWaterPacketBytes = 0;
};

class FrontendPacketMailbox final {
public:
    enum class EnqueueResult { Accepted, Overflow, Paused, Closed };

    FrontendPacketMailbox(const qsizetype maxPackets,
                          const qsizetype maxPacketBytes,
                          const qsizetype maxBatchesPerDrain)
        : m_maxPackets(maxPackets), m_maxPacketBytes(maxPacketBytes),
          m_maxBatchesPerDrain(maxBatchesPerDrain)
    {
    }

    [[nodiscard]] EnqueueResult enqueue(PacketList packets,
                                        bool *scheduleDrain)
    {
        if (scheduleDrain) *scheduleDrain = false;
        if (packets.isEmpty()) return EnqueueResult::Accepted;
        const qsizetype packetBytes = estimatedPacketBytes(packets);
        QMutexLocker lock(&m_mutex);
        if (m_closed) return EnqueueResult::Closed;
        if (m_paused) return EnqueueResult::Paused;
        if (packets.size() > m_maxPackets - m_stats.pendingPackets
            || packetBytes > m_maxPacketBytes - m_stats.pendingPacketBytes) {
            ++m_stats.rejectedBatches;
            m_stats.rejectedPackets += quint64(packets.size());
            return EnqueueResult::Overflow;
        }
        m_batches.enqueue(Batch{packetBytes, std::move(packets)});
        ++m_stats.acceptedBatches;
        m_stats.acceptedPackets += quint64(m_batches.back().packets.size());
        m_stats.pendingBatches = m_batches.size();
        m_stats.pendingPackets += m_batches.back().packets.size();
        m_stats.pendingPacketBytes += packetBytes;
        m_stats.highWaterPackets = std::max(m_stats.highWaterPackets,
                                            m_stats.pendingPackets);
        m_stats.highWaterPacketBytes = std::max(m_stats.highWaterPacketBytes,
                                                m_stats.pendingPacketBytes);
        if (!m_drainScheduled) {
            m_drainScheduled = true;
            if (scheduleDrain) *scheduleDrain = true;
        }
        return EnqueueResult::Accepted;
    }

    [[nodiscard]] PacketList takeNext()
    {
        QMutexLocker lock(&m_mutex);
        if (m_batches.isEmpty()) return {};
        Batch batch = std::move(m_batches.head());
        m_batches.dequeue();
        m_stats.pendingBatches = m_batches.size();
        m_stats.pendingPackets -= batch.packets.size();
        m_stats.pendingPacketBytes -= batch.packetBytes;
        return std::move(batch.packets);
    }

    // true means the current scheduled drain still owns pending data and the
    // UI should post exactly one continuation event.
    [[nodiscard]] bool finishDrain()
    {
        QMutexLocker lock(&m_mutex);
        if (!m_batches.isEmpty()) return true;
        m_drainScheduled = false;
        return false;
    }

    void clear()
    {
        QMutexLocker lock(&m_mutex);
        m_batches.clear();
        m_stats.pendingBatches = 0;
        m_stats.pendingPackets = 0;
        m_stats.pendingPacketBytes = 0;
    }

    // Generation/lifecycle barrier. enqueue() and this method share the same
    // mutex, so a producer already inside enqueue is either cleared here or a
    // later producer observes Paused. No old batch can race through a reset.
    void pauseAndClear()
    {
        QMutexLocker lock(&m_mutex);
        m_paused = true;
        m_batches.clear();
        m_stats.pendingBatches = 0;
        m_stats.pendingPackets = 0;
        m_stats.pendingPacketBytes = 0;
    }

    void resume()
    {
        QMutexLocker lock(&m_mutex);
        if (!m_closed) m_paused = false;
    }

    void close()
    {
        QMutexLocker lock(&m_mutex);
        m_closed = true;
        m_batches.clear();
        m_stats.pendingBatches = 0;
        m_stats.pendingPackets = 0;
        m_stats.pendingPacketBytes = 0;
    }

    [[nodiscard]] FrontendPacketMailboxSnapshot snapshot() const
    {
        QMutexLocker lock(&m_mutex);
        return m_stats;
    }

    [[nodiscard]] qsizetype maxBatchesPerDrain() const noexcept
    {
        return m_maxBatchesPerDrain;
    }

private:
    struct Batch {
        qsizetype packetBytes = 0;
        PacketList packets;
    };

    mutable QMutex m_mutex;
    QQueue<Batch> m_batches;
    FrontendPacketMailboxSnapshot m_stats;
    qsizetype m_maxPackets = 0;
    qsizetype m_maxPacketBytes = 0;
    qsizetype m_maxBatchesPerDrain = 0;
    bool m_drainScheduled = false;
    bool m_paused = false;
    bool m_closed = false;
};

} // namespace serein::scope::detail
