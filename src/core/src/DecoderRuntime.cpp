#include <serein/scope/DecoderRuntime.h>

#include <serein/scope/BufferedDecoder.h>

#include "FrontendPacketMailbox.h"

#include <QtCore/QMetaObject>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QQueue>
#include <QtCore/QSettings>
#include <QtCore/QWaitCondition>

#include <algorithm>
#include <exception>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace serein::scope {
namespace {

constexpr auto MaxQueuedBytesKey = "runtime/decoder/max-queued-bytes";
constexpr auto MaxDecoderBufferBytesKey =
    "runtime/decoder/max-buffered-half-packet-bytes";
constexpr auto MaxPendingPacketsKey = "runtime/decoder/max-pending-packets";
constexpr auto MaxPendingPacketBytesKey =
    "runtime/decoder/max-pending-packet-bytes";
constexpr auto MaxBatchesPerDeliveryKey =
    "runtime/decoder/max-batches-per-delivery";

template<typename Value>
Value boundedSetting(const QSettings &settings, const char *key,
                     const Value fallback, const Value minimum,
                     const Value maximum)
{
    bool valid = false;
    const qlonglong raw = settings.value(QString::fromLatin1(key),
                                         qlonglong(fallback)).toLongLong(&valid);
    if (!valid) return fallback;
    return Value(std::clamp<qlonglong>(raw, qlonglong(minimum),
                                      qlonglong(maximum)));
}

} // namespace

DecoderRuntimeLimits DecoderRuntime::configuredLimits()
{
    const QSettings settings;
    return configuredLimits(settings);
}

DecoderRuntimeLimits DecoderRuntime::configuredLimits(const QSettings &settings)
{
    const DecoderRuntimeLimits defaults;
    DecoderRuntimeLimits limits;
    limits.maxQueuedBytes = boundedSetting<qsizetype>(
        settings, MaxQueuedBytesKey, defaults.maxQueuedBytes,
        64 * 1024, 256 * 1024 * 1024);
    limits.maxDecoderBufferBytes = boundedSetting<qsizetype>(
        settings, MaxDecoderBufferBytesKey, defaults.maxDecoderBufferBytes,
        64 * 1024, 256 * 1024 * 1024);
    limits.maxPendingPackets = boundedSetting<qsizetype>(
        settings, MaxPendingPacketsKey, defaults.maxPendingPackets,
        8, 65536);
    limits.maxPendingPacketBytes = boundedSetting<qsizetype>(
        settings, MaxPendingPacketBytesKey, defaults.maxPendingPacketBytes,
        64 * 1024, 512 * 1024 * 1024);
    limits.maxBatchesPerDelivery = boundedSetting<qsizetype>(
        settings, MaxBatchesPerDeliveryKey, defaults.maxBatchesPerDelivery,
        1, 1024);
    return limits;
}

class DecoderRuntime::Impl final {
public:
    struct QueuedChunk {
        quint64 generation = 0;
        QByteArray bytes;
    };

    struct PendingBatch {
        quint64 generation = 0;
        qsizetype payloadBytes = 0;
        PacketList packets;
    };

    Impl(DecoderRuntime *owner, std::unique_ptr<IDataEngine> engine,
         const DecoderRuntimeLimits limits)
        : owner(owner), decoder(std::move(engine), limits.maxDecoderBufferBytes),
          limits(limits)
    {
    }

    ~Impl()
    {
        stop();
    }

    void start()
    {
        QMutexLocker lock(&mutex);
        if (running) return;
        stopping = false;
        faulted = false;
        resetRequested = true;
        ++generation;
        running = true;
        stats.running = true;
        try {
            worker = std::thread([this] { run(); });
        } catch (...) {
            running = false;
            resetRequested = false;
            stats.running = false;
            throw;
        }
    }

    void stop()
    {
        {
            QMutexLocker lock(&mutex);
            if (!running) return;
            stopping = true;
            ++generation;
            clearIngressLocked();
            clearOutputLocked();
            wake.wakeAll();
        }
        if (worker.joinable()) worker.join();
        decoder.reset();
        QMutexLocker lock(&mutex);
        running = false;
        stopping = false;
        faulted = false;
        resetRequested = false;
        stats.running = false;
        stats.faulted = false;
        stats.decoderPendingBytes = 0;
    }

    void reset()
    {
        QMutexLocker lock(&mutex);
        ++generation;
        clearIngressLocked();
        clearOutputLocked();
        faulted = false;
        resetRequested = true;
        ++stats.resetCount;
        stats.faulted = false;
        stats.decoderPendingBytes = 0;
        latestProgressGeneration = generation;
        latestDecoderPendingBytes = 0;
        wake.wakeAll();
    }

    DecoderSubmitResult submit(QByteArray chunk)
    {
        if (chunk.isEmpty()) return DecoderSubmitResult::Accepted;
        QMutexLocker lock(&mutex);
        if (!running || stopping) {
            rejectLocked(chunk.size());
            return DecoderSubmitResult::NotRunning;
        }
        if (faulted) {
            rejectLocked(chunk.size());
            return DecoderSubmitResult::Faulted;
        }
        if (chunk.size() > limits.maxQueuedBytes - queuedBytes) {
            rejectLocked(chunk.size());
            return DecoderSubmitResult::QueueFull;
        }
        queuedBytes += chunk.size();
        stats.queuedBytes = queuedBytes;
        stats.queueHighWaterBytes = std::max(stats.queueHighWaterBytes,
                                             queuedBytes);
        ++stats.acceptedChunks;
        stats.acceptedBytes += quint64(chunk.size());
        queue.enqueue(QueuedChunk{generation, std::move(chunk)});
        wake.wakeOne();
        return DecoderSubmitResult::Accepted;
    }

    bool isRunning() const noexcept
    {
        QMutexLocker lock(&mutex);
        return running && !stopping;
    }

    DecoderRuntimeStats snapshot() const
    {
        QMutexLocker lock(&mutex);
        DecoderRuntimeStats result = stats;
        result.queuedBytes = queuedBytes;
        result.pendingPackets = pendingPackets;
        result.pendingPacketBytes = pendingPacketBytes;
        result.running = running && !stopping;
        result.faulted = faulted;
        return result;
    }

    bool isCurrentGeneration(const quint64 value) const
    {
        QMutexLocker lock(&mutex);
        return running && !stopping && value == generation;
    }

private:
    void run()
    {
        while (true) {
            QueuedChunk chunk;
            bool shouldReset = false;
            {
                QMutexLocker lock(&mutex);
                while (!stopping && queue.isEmpty() && !resetRequested) {
                    wake.wait(&mutex);
                }
                if (stopping) break;
                if (resetRequested) {
                    resetRequested = false;
                    shouldReset = true;
                } else {
                    chunk = std::move(queue.head());
                    queue.dequeue();
                    queuedBytes -= chunk.bytes.size();
                    stats.queuedBytes = queuedBytes;
                }
            }
            if (shouldReset) {
                decoder.reset();
                continue;
            }
            try {
                PacketList packets = decoder.feed(QByteArrayView(chunk.bytes));
                const qsizetype decoderPendingBytes = decoder.pendingBytes();
                {
                    QMutexLocker lock(&mutex);
                    ++stats.decodedChunks;
                }
                if (!packets.isEmpty()) enqueueOutput(chunk.generation,
                                                       std::move(packets));
                updateProgress(chunk.generation, decoderPendingBytes);
            } catch (const std::exception &error) {
                fail(QString::fromUtf8(error.what()));
            } catch (...) {
                fail(QStringLiteral("unknown data engine failure"));
            }
        }
    }

    void fail(const QString &message)
    {
        decoder.reset();
        quint64 errorGeneration = 0;
        {
            QMutexLocker lock(&mutex);
            ++generation;
            errorGeneration = generation;
            clearIngressLocked();
            clearOutputLocked();
            faulted = true;
            resetRequested = false;
            ++stats.decodeErrors;
            stats.faulted = true;
            stats.decoderPendingBytes = 0;
            latestProgressGeneration = generation;
            latestDecoderPendingBytes = 0;
        }
        postError(errorGeneration, message);
    }

    void enqueueOutput(const quint64 outputGeneration, PacketList packets)
    {
        const qsizetype payloadBytes = detail::estimatedPacketBytes(packets);
        bool scheduleDelivery = false;
        bool overflow = false;
        {
            QMutexLocker lock(&mutex);
            if (outputGeneration != generation || stopping) return;
            if (packets.size() > limits.maxPendingPackets - pendingPackets
                || payloadBytes > limits.maxPendingPacketBytes - pendingPacketBytes) {
                overflow = true;
            } else {
                pendingPackets += packets.size();
                pendingPacketBytes += payloadBytes;
                ++stats.emittedBatches;
                stats.emittedPackets += quint64(packets.size());
                stats.pendingPackets = pendingPackets;
                stats.pendingPacketBytes = pendingPacketBytes;
                stats.pendingHighWaterPackets = std::max(
                    stats.pendingHighWaterPackets, pendingPackets);
                stats.pendingHighWaterPacketBytes = std::max(
                    stats.pendingHighWaterPacketBytes, pendingPacketBytes);
                outputs.enqueue(PendingBatch{outputGeneration, payloadBytes,
                                             std::move(packets)});
                if (!deliveryScheduled) {
                    deliveryScheduled = true;
                    scheduleDelivery = true;
                }
            }
        }
        if (overflow) {
            fail(QStringLiteral("decoded packet backlog limit exceeded"));
        } else if (scheduleDelivery) {
            scheduleOutputDrain();
        }
    }

    void scheduleOutputDrain()
    {
        QMetaObject::invokeMethod(owner, [runtime = owner] {
            runtime->drainOutputs();
        }, Qt::QueuedConnection);
    }

    void updateProgress(const quint64 progressGeneration,
                        const qsizetype decoderPendingBytes)
    {
        bool scheduleDelivery = false;
        {
            QMutexLocker lock(&mutex);
            if (progressGeneration != generation || stopping) return;
            stats.decoderPendingBytes = decoderPendingBytes;
            stats.decoderPendingHighWaterBytes = std::max(
                stats.decoderPendingHighWaterBytes, decoderPendingBytes);
            latestProgressGeneration = progressGeneration;
            latestDecoderPendingBytes = decoderPendingBytes;
            if (!progressScheduled) {
                progressScheduled = true;
                scheduleDelivery = true;
            }
        }
        if (scheduleDelivery) {
            QMetaObject::invokeMethod(owner, [runtime = owner] {
                runtime->drainProgress();
            }, Qt::QueuedConnection);
        }
    }

public:
    void drainOutputs()
    {
        qsizetype delivered = 0;
        while (delivered < limits.maxBatchesPerDelivery) {
            PendingBatch batch;
            {
                QMutexLocker lock(&mutex);
                if (outputs.isEmpty()) {
                    deliveryScheduled = false;
                    return;
                }
                batch = std::move(outputs.head());
                outputs.dequeue();
                pendingPackets -= batch.packets.size();
                pendingPacketBytes -= batch.payloadBytes;
                stats.pendingPackets = pendingPackets;
                stats.pendingPacketBytes = pendingPacketBytes;
            }
            owner->deliverPackets(batch.generation, std::move(batch.packets));
            ++delivered;
        }

        bool scheduleAgain = false;
        {
            QMutexLocker lock(&mutex);
            if (outputs.isEmpty()) {
                deliveryScheduled = false;
            } else {
                scheduleAgain = true;
            }
        }
        if (scheduleAgain) scheduleOutputDrain();
    }

    void drainProgress()
    {
        quint64 progressGeneration = 0;
        qsizetype decoderPendingBytes = 0;
        {
            QMutexLocker lock(&mutex);
            if (!progressScheduled) return;
            progressGeneration = latestProgressGeneration;
            decoderPendingBytes = latestDecoderPendingBytes;
            progressScheduled = false;
        }
        owner->deliverProgress(progressGeneration, decoderPendingBytes);
    }

private:
    void postError(const quint64 errorGeneration, const QString &message)
    {
        QMetaObject::invokeMethod(
            owner,
            [runtime = owner, errorGeneration, message] {
                runtime->deliverError(errorGeneration, message);
            },
            Qt::QueuedConnection);
    }

    void clearIngressLocked()
    {
        queue.clear();
        queuedBytes = 0;
        stats.queuedBytes = 0;
    }

    void clearOutputLocked()
    {
        outputs.clear();
        pendingPackets = 0;
        pendingPacketBytes = 0;
        stats.pendingPackets = 0;
        stats.pendingPacketBytes = 0;
    }

    void rejectLocked(const qsizetype bytes)
    {
        ++stats.rejectedChunks;
        stats.rejectedBytes += quint64(bytes);
    }

    DecoderRuntime *owner = nullptr;
    BufferedDecoder decoder;
    DecoderRuntimeLimits limits;
    mutable QMutex mutex;
    QWaitCondition wake;
    QQueue<QueuedChunk> queue;
    QQueue<PendingBatch> outputs;
    std::thread worker;
    DecoderRuntimeStats stats;
    qsizetype queuedBytes = 0;
    qsizetype pendingPackets = 0;
    qsizetype pendingPacketBytes = 0;
    quint64 generation = 0;
    quint64 latestProgressGeneration = 0;
    qsizetype latestDecoderPendingBytes = 0;
    bool running = false;
    bool stopping = false;
    bool faulted = false;
    bool resetRequested = false;
    bool deliveryScheduled = false;
    bool progressScheduled = false;
};

DecoderRuntime::DecoderRuntime(std::unique_ptr<IDataEngine> engine,
                               const DecoderRuntimeLimits limits,
                               QObject *parent)
    : QObject(parent)
{
    if (!engine) {
        throw std::invalid_argument("DecoderRuntime requires an engine");
    }
    if (limits.maxQueuedBytes <= 0 || limits.maxDecoderBufferBytes <= 0
        || limits.maxPendingPackets <= 0
        || limits.maxPendingPacketBytes <= 0
        || limits.maxBatchesPerDelivery <= 0) {
        throw std::invalid_argument("DecoderRuntime limits must be positive");
    }
    m_impl = std::make_unique<Impl>(this, std::move(engine), limits);
}

DecoderRuntime::~DecoderRuntime()
{
    m_impl->stop();
}

void DecoderRuntime::start()
{
    m_impl->start();
}

void DecoderRuntime::stop()
{
    m_impl->stop();
}

void DecoderRuntime::reset()
{
    m_impl->reset();
}

DecoderSubmitResult DecoderRuntime::submit(QByteArray chunk)
{
    return m_impl->submit(std::move(chunk));
}

bool DecoderRuntime::isRunning() const noexcept
{
    return m_impl->isRunning();
}

DecoderRuntimeStats DecoderRuntime::stats() const
{
    return m_impl->snapshot();
}

void DecoderRuntime::deliverPackets(const quint64 generation, PacketList packets)
{
    if (!m_impl->isCurrentGeneration(generation)) return;
    emit packetsDecoded(packets);
}

void DecoderRuntime::deliverProgress(const quint64 generation,
                                     const qsizetype decoderPendingBytes)
{
    if (!m_impl->isCurrentGeneration(generation)) return;
    emit progressChanged(decoderPendingBytes);
}

void DecoderRuntime::deliverError(const quint64 generation,
                                  const QString &message)
{
    if (!m_impl->isCurrentGeneration(generation)) return;
    emit errorOccurred(message);
}

void DecoderRuntime::drainOutputs()
{
    m_impl->drainOutputs();
}

void DecoderRuntime::drainProgress()
{
    m_impl->drainProgress();
}

} // namespace serein::scope
