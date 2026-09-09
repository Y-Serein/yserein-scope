#pragma once

#include "CoreExport.h"
#include "IDataEngine.h"

#include <QtCore/QObject>

#include <memory>

class QSettings;

namespace serein::scope {

struct DecoderRuntimeLimits {
    qsizetype maxQueuedBytes = 4 * 1024 * 1024;
    qsizetype maxDecoderBufferBytes = 4 * 1024 * 1024;
    qsizetype maxPendingPackets = 256;
    qsizetype maxPendingPacketBytes = 8 * 1024 * 1024;
    qsizetype maxBatchesPerDelivery = 8;
};

struct DecoderRuntimeStats {
    quint64 acceptedChunks = 0;
    quint64 acceptedBytes = 0;
    quint64 rejectedChunks = 0;
    quint64 rejectedBytes = 0;
    quint64 decodedChunks = 0;
    quint64 emittedBatches = 0;
    quint64 emittedPackets = 0;
    quint64 resetCount = 0;
    quint64 decodeErrors = 0;
    qsizetype queuedBytes = 0;
    qsizetype queueHighWaterBytes = 0;
    qsizetype decoderPendingBytes = 0;
    qsizetype decoderPendingHighWaterBytes = 0;
    qsizetype pendingPackets = 0;
    qsizetype pendingHighWaterPackets = 0;
    qsizetype pendingPacketBytes = 0;
    qsizetype pendingHighWaterPacketBytes = 0;
    bool running = false;
    bool faulted = false;
};

enum class DecoderSubmitResult {
    Accepted,
    NotRunning,
    QueueFull,
    Faulted,
};

// Owns one IDataEngine and confines BufferedDecoder::feed() to a dedicated
// worker thread.  Input is ordered and bounded; decoded Packet batches are
// delivered on this QObject's owning thread.  reset() creates a generation
// boundary, so queued results from an earlier stream cannot reach consumers.
class SEREIN_SCOPE_CORE_EXPORT DecoderRuntime final : public QObject {
    Q_OBJECT

public:
    static DecoderRuntimeLimits configuredLimits();
    static DecoderRuntimeLimits configuredLimits(const QSettings &settings);

    explicit DecoderRuntime(std::unique_ptr<IDataEngine> engine,
                            DecoderRuntimeLimits limits = {},
                            QObject *parent = nullptr);
    ~DecoderRuntime() override;

    DecoderRuntime(const DecoderRuntime &) = delete;
    DecoderRuntime &operator=(const DecoderRuntime &) = delete;

    void start();
    void stop();
    void reset();

    [[nodiscard]] DecoderSubmitResult submit(QByteArray chunk);
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] DecoderRuntimeStats stats() const;

signals:
    void packetsDecoded(const serein::scope::PacketList &packets);
    void progressChanged(qsizetype decoderPendingBytes);
    void errorOccurred(const QString &message);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    void deliverPackets(quint64 generation, PacketList packets);
    void deliverProgress(quint64 generation, qsizetype decoderPendingBytes);
    void deliverError(quint64 generation, const QString &message);
    void drainOutputs();
    void drainProgress();
};

} // namespace serein::scope
