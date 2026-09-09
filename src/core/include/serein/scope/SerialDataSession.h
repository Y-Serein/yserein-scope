#pragma once

#include "CoreExport.h"
#include "BackendRuntime.h"
#include "DecoderRuntime.h"
#include "SerialTransport.h"

#include <QtCore/QObject>

#include <functional>
#include <memory>

namespace serein::scope {

struct SerialDataSessionSnapshot {
    QString transportState = QStringLiteral("disconnected");
    SerialConfig config;
    bool decodingEnabled = true;
    quint64 receivedBytes = 0;
    quint64 transmittedBytes = 0;
    SerialTransportStats transport;
    DecoderRuntimeStats decoder;
};

struct FrontendPacketMailboxStats {
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

// Frontend proxy for a backend-owned serial/decode session. Its public API is
// intentionally unchanged so Widgets and Qt Quick can consume the same
// session contract. SerialTransport and DecoderRuntime live on the process
// BackendRuntime thread instead of inside a QWidget/controller thread.
class SEREIN_SCOPE_CORE_EXPORT SerialDataSession final : public QObject {
    Q_OBJECT

public:
    explicit SerialDataSession(std::unique_ptr<IDataEngine> engine,
                               DecoderRuntimeLimits limits =
                                   DecoderRuntime::configuredLimits(),
                               QObject *parent = nullptr);
    ~SerialDataSession() override;

    SerialDataSession(const SerialDataSession &) = delete;
    SerialDataSession &operator=(const SerialDataSession &) = delete;

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] bool replaceEngine(std::unique_ptr<IDataEngine> engine,
                                     QString *error = nullptr);
    [[nodiscard]] SerialDataSessionSnapshot snapshot() const;
    [[nodiscard]] FrontendPacketMailboxStats frontendMailboxStats() const;

    // Stops decoder work before closing the serial lease.  finalWrite is
    // drained through the native driver before close when supplied.
    void shutdown(const QByteArray &finalWrite = {});

public slots:
    void open(const serein::scope::SerialConfig &config);
    void close();
    // Queues an optional final control write after every earlier accepted
    // session task, rejects later writes, then drains the native driver before
    // closing.
    void closeAfterWrites(const QByteArray &finalWrite = {},
                          int drainDelayMs = 0);
    void write(const QByteArray &data);
    void resetDecoder();
    void setDecodingEnabled(bool enabled);
    // Replay/tests can use the same ordered ingress path as SerialTransport.
    void ingest(const QByteArray &data);

signals:
    void stateChanged(const QString &state);
    void packetsDecoded(const serein::scope::PacketList &packets);
    void rawDataReceived(const QByteArray &data);
    void bytesReceived(qint64 count);
    void bytesWritten(qint64 count);
    void decoderProgressChanged(qsizetype pendingBytes);
    void errorOccurred(const QString &message);
    void snapshotChanged(const serein::scope::SerialDataSessionSnapshot &snapshot);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    bool postToBackend(std::function<void()> task,
                       BackendTaskPriority priority = BackendTaskPriority::Normal);
    void drainFrontendPackets();
};

} // namespace serein::scope

Q_DECLARE_METATYPE(serein::scope::SerialDataSessionSnapshot)
