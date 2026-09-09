#include <serein/scope/SerialDataSession.h>

#include <serein/scope/BackendRuntime.h>

#include "FrontendPacketMailbox.h"

#include <QtCore/QMetaObject>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace serein::scope {
namespace {

class ManagedSerialDataSession final : public QObject {
    Q_OBJECT

public:
    ManagedSerialDataSession(std::unique_ptr<IDataEngine> engine,
                             const DecoderRuntimeLimits limits,
                             QObject *parent = nullptr)
        : QObject(parent), m_transport(this), m_limits(limits)
    {
        m_snapshot.decodingEnabled = bool(engine);
        connect(&m_transport, &SerialTransport::stateChanged,
                this, &ManagedSerialDataSession::handleState);
        connect(&m_transport, &SerialTransport::dataReceived,
                this, &ManagedSerialDataSession::ingest);
        connect(&m_transport, &SerialTransport::bytesWritten,
                this, [this](const qint64 count) {
                    if (count > 0) m_snapshot.transmittedBytes += quint64(count);
                    emit bytesWritten(count);
                    publishSnapshot();
                });
        connect(&m_transport, &SerialTransport::errorOccurred,
                this, &ManagedSerialDataSession::errorOccurred);
        if (engine) createRuntime(std::move(engine));
    }

    ~ManagedSerialDataSession() override
    {
        shutdown();
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return m_transport.isActive();
    }

    [[nodiscard]] SerialDataSessionSnapshot snapshot() const
    {
        SerialDataSessionSnapshot result = m_snapshot;
        result.transport = m_transport.stats();
        if (m_runtime) result.decoder = m_runtime->stats();
        return result;
    }

    bool replaceEngine(std::unique_ptr<IDataEngine> engine, QString *error)
    {
        if (!engine) {
            if (error) *error = tr("数据引擎不能为空");
            return false;
        }
        if (m_shuttingDown) {
            if (error) *error = tr("设备会话正在关闭");
            return false;
        }
        if (m_runtime) m_runtime->stop();
        createRuntime(std::move(engine));
        m_snapshot.decodingEnabled = true;
        publishSnapshot();
        return true;
    }

    void shutdown(const QByteArray &finalWrite = {})
    {
        if (m_shuttingDown) return;
        m_shuttingDown = true;
        m_snapshot.decodingEnabled = false;
        if (m_runtime) m_runtime->stop();
        if (!finalWrite.isEmpty() && m_transport.isActive()) {
            m_transport.write(finalWrite);
            m_transport.closeAfterWrites();
        } else {
            m_transport.closeAfterWrites();
        }
    }

    void open(const SerialConfig &config)
    {
        if (m_shuttingDown) return;
        m_snapshot.config = config;
        m_snapshot.receivedBytes = 0;
        m_snapshot.transmittedBytes = 0;
        resetDecoder();
        m_transport.open(config);
        publishSnapshot();
    }

    void close()
    {
        if (m_shuttingDown) return;
        resetDecoder();
        m_transport.closeAfterWrites();
    }

    void closeAfterWrites(const QByteArray &finalWrite, const int drainDelayMs)
    {
        if (m_shuttingDown) return;
        resetDecoder();
        if (!finalWrite.isEmpty() && m_transport.isActive()) {
            m_transport.write(finalWrite);
        }
        if (!finalWrite.isEmpty() && drainDelayMs > 0
            && m_transport.isActive()) {
            QTimer::singleShot(drainDelayMs, this, [this] {
                if (!m_shuttingDown) m_transport.closeAfterWrites();
            });
            return;
        }
        m_transport.closeAfterWrites();
    }

    void write(const QByteArray &data)
    {
        if (!m_shuttingDown) m_transport.write(data);
    }

    void resetDecoder()
    {
        if (m_runtime) m_runtime->reset();
        m_snapshot.decoder = m_runtime ? m_runtime->stats() : DecoderRuntimeStats{};
        publishSnapshot();
    }

    void setDecodingEnabled(const bool enabled)
    {
        if (m_shuttingDown || m_snapshot.decodingEnabled == enabled) return;
        if (enabled && !m_runtime) {
            emit errorOccurred(tr("当前会话没有数据引擎，不能启用解码"));
            return;
        }
        m_snapshot.decodingEnabled = enabled;
        resetDecoder();
    }

    void ingest(const QByteArray &data)
    {
        if (m_shuttingDown || data.isEmpty()) return;
        m_snapshot.receivedBytes += quint64(data.size());
        emit bytesReceived(data.size());
        if (!m_snapshot.decodingEnabled || !m_runtime) {
            emit rawDataReceived(data);
            publishSnapshot();
            return;
        }
        const DecoderSubmitResult result = m_runtime->submit(data);
        if (result == DecoderSubmitResult::QueueFull) {
            m_snapshot.decodingEnabled = false;
            emit errorOccurred(tr("解码输入队列已满，无法继续无损接收"));
        } else if (result == DecoderSubmitResult::Faulted) {
            m_snapshot.decodingEnabled = false;
            emit errorOccurred(tr("解码服务处于故障状态"));
        } else if (result == DecoderSubmitResult::NotRunning) {
            m_snapshot.decodingEnabled = false;
            emit errorOccurred(tr("解码服务未运行"));
        }
        if (result != DecoderSubmitResult::Accepted) m_transport.close();
        publishSnapshot();
    }

    void stopForFrontendOverflow()
    {
        if (m_shuttingDown || !m_snapshot.decodingEnabled) return;
        m_snapshot.decodingEnabled = false;
        if (m_runtime) m_runtime->stop();
        m_transport.close();
        emit errorOccurred(tr("前端数据队列已满，连接已停止以避免无界内存增长"));
        publishSnapshot();
    }

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
    void createRuntime(std::unique_ptr<IDataEngine> engine)
    {
        m_runtime = std::make_unique<DecoderRuntime>(std::move(engine), m_limits);
        connect(m_runtime.get(), &DecoderRuntime::packetsDecoded,
                this, [this](const PacketList &packets) {
                    emit packetsDecoded(packets);
                    publishSnapshot();
                });
        connect(m_runtime.get(), &DecoderRuntime::progressChanged,
                this, [this](const qsizetype pendingBytes) {
                    emit decoderProgressChanged(pendingBytes);
                    publishSnapshot();
                });
        connect(m_runtime.get(), &DecoderRuntime::errorOccurred,
                this, [this](const QString &message) {
                    emit errorOccurred(message);
                    publishSnapshot();
                });
        m_runtime->start();
    }

    void handleState(const QString &state)
    {
        m_snapshot.transportState = state;
        emit stateChanged(state);
        publishSnapshot();
    }

    void publishSnapshot()
    {
        if (m_snapshotScheduled || m_shuttingDown) return;
        m_snapshotScheduled = true;
        QMetaObject::invokeMethod(this, [this] {
            m_snapshotScheduled = false;
            m_snapshot.transport = m_transport.stats();
            m_snapshot.decoder = m_runtime ? m_runtime->stats() : DecoderRuntimeStats{};
            emit snapshotChanged(m_snapshot);
        }, Qt::QueuedConnection);
    }

    SerialTransport m_transport;
    std::unique_ptr<DecoderRuntime> m_runtime;
    DecoderRuntimeLimits m_limits;
    SerialDataSessionSnapshot m_snapshot;
    bool m_snapshotScheduled = false;
    bool m_shuttingDown = false;
};

} // namespace

class SerialDataSession::Impl final {
public:
    explicit Impl(const DecoderRuntimeLimits &limits)
        : packetMailbox(limits.maxPendingPackets,
                        limits.maxPendingPacketBytes,
                        limits.maxBatchesPerDelivery)
    {
    }

    QPointer<ManagedSerialDataSession> managed;
    detail::FrontendPacketMailbox packetMailbox;
    mutable QMutex snapshotMutex;
    QMutex commandMutex;
    SerialDataSessionSnapshot snapshot;
    std::atomic_bool active{false};
    bool acceptingWrites = true;
    std::atomic_bool shuttingDown{false};
    bool registered = false;
};

SerialDataSession::SerialDataSession(std::unique_ptr<IDataEngine> engine,
                                     const DecoderRuntimeLimits limits,
                                     QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(limits))
{
    qRegisterMetaType<SerialDataSessionSnapshot>();
    BackendRuntime &backend = BackendRuntime::instance();
    auto holder = std::make_shared<std::unique_ptr<IDataEngine>>(std::move(engine));
    ManagedSerialDataSession *managed = nullptr;
    if (!backend.runBlocking([&managed, holder, limits] {
            managed = new ManagedSerialDataSession(std::move(*holder), limits);
        }) || !managed) {
        throw std::runtime_error("failed to create backend serial session");
    }
    m_impl->managed = managed;
    backend.registerSerialSession();
    m_impl->registered = true;

    connect(managed, &ManagedSerialDataSession::stateChanged,
            this, [this](const QString &state) {
                m_impl->active.store(state != QStringLiteral("disconnected"),
                                     std::memory_order_release);
                emit stateChanged(state);
            }, Qt::QueuedConnection);
    // Do not use one queued Qt MetaCall per PacketList here. The mailbox owns
    // both packet and byte capacity across the Backend -> UI boundary, while
    // at most one lightweight wake-up event is pending on the UI thread.
    connect(managed, &ManagedSerialDataSession::packetsDecoded,
            this, [this, managed](PacketList packets) {
                bool scheduleDrain = false;
                const auto result = m_impl->packetMailbox.enqueue(
                    std::move(packets), &scheduleDrain);
                if (result == detail::FrontendPacketMailbox::EnqueueResult::Overflow) {
                    QMetaObject::invokeMethod(managed, [managed] {
                        if (managed) managed->stopForFrontendOverflow();
                    }, Qt::QueuedConnection);
                    return;
                }
                if (scheduleDrain) {
                    QMetaObject::invokeMethod(this,
                                              &SerialDataSession::drainFrontendPackets,
                                              Qt::QueuedConnection);
                }
            }, Qt::DirectConnection);
    connect(managed, &ManagedSerialDataSession::rawDataReceived,
            this, &SerialDataSession::rawDataReceived, Qt::QueuedConnection);
    connect(managed, &ManagedSerialDataSession::bytesReceived,
            this, &SerialDataSession::bytesReceived, Qt::QueuedConnection);
    connect(managed, &ManagedSerialDataSession::bytesWritten,
            this, &SerialDataSession::bytesWritten, Qt::QueuedConnection);
    connect(managed, &ManagedSerialDataSession::decoderProgressChanged,
            this, &SerialDataSession::decoderProgressChanged, Qt::QueuedConnection);
    connect(managed, &ManagedSerialDataSession::errorOccurred,
            this, &SerialDataSession::errorOccurred, Qt::QueuedConnection);
    connect(managed, &ManagedSerialDataSession::snapshotChanged,
            this, [this](const SerialDataSessionSnapshot &snapshot) {
                {
                    QMutexLocker lock(&m_impl->snapshotMutex);
                    m_impl->snapshot = snapshot;
                }
                emit snapshotChanged(snapshot);
            }, Qt::QueuedConnection);

    backend.runBlocking([this, managed] {
        const SerialDataSessionSnapshot initial = managed->snapshot();
        QMutexLocker lock(&m_impl->snapshotMutex);
        m_impl->snapshot = initial;
    });
}

SerialDataSession::~SerialDataSession()
{
    m_impl->packetMailbox.close();
    shutdown();
    BackendRuntime &backend = BackendRuntime::instance();
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    backend.runBlocking([managed] {
        if (!managed) return;
        managed->shutdown();
        delete managed.data();
    });
    m_impl->managed.clear();
    if (m_impl->registered) {
        backend.unregisterSerialSession();
        m_impl->registered = false;
    }
}

bool SerialDataSession::isActive() const noexcept
{
    return m_impl->active.load(std::memory_order_acquire);
}

bool SerialDataSession::replaceEngine(std::unique_ptr<IDataEngine> engine,
                                      QString *error)
{
    if (!engine) {
        if (error) *error = tr("数据引擎不能为空");
        return false;
    }
    if (m_impl->shuttingDown.load(std::memory_order_acquire)) {
        if (error) *error = tr("设备会话正在关闭");
        return false;
    }
    m_impl->packetMailbox.pauseAndClear();
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    auto holder = std::make_shared<std::unique_ptr<IDataEngine>>(std::move(engine));
    bool result = false;
    QString backendError;
    if (!BackendRuntime::instance().runOrderedBlocking(
            [&result, &backendError, managed, holder] {
                if (managed) {
                    result = managed->replaceEngine(std::move(*holder), &backendError);
                }
            })) {
        backendError = tr("后台服务不可用");
    }
    if (!m_impl->shuttingDown.load(std::memory_order_acquire)) {
        m_impl->packetMailbox.resume();
    }
    if (error) *error = backendError;
    return result;
}

SerialDataSessionSnapshot SerialDataSession::snapshot() const
{
    QMutexLocker lock(&m_impl->snapshotMutex);
    return m_impl->snapshot;
}

FrontendPacketMailboxStats SerialDataSession::frontendMailboxStats() const
{
    const detail::FrontendPacketMailboxSnapshot snapshot =
        m_impl->packetMailbox.snapshot();
    return {
        snapshot.acceptedBatches,
        snapshot.acceptedPackets,
        snapshot.rejectedBatches,
        snapshot.rejectedPackets,
        snapshot.pendingBatches,
        snapshot.pendingPackets,
        snapshot.pendingPacketBytes,
        snapshot.highWaterPackets,
        snapshot.highWaterPacketBytes,
    };
}

void SerialDataSession::shutdown(const QByteArray &finalWrite)
{
    {
        QMutexLocker commandLock(&m_impl->commandMutex);
        if (m_impl->shuttingDown.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        m_impl->acceptingWrites = false;
    }
    m_impl->packetMailbox.pauseAndClear();
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    BackendRuntime::instance().runOrderedBlocking([managed, finalWrite] {
        if (managed) managed->shutdown(finalWrite);
    });
    m_impl->active.store(false, std::memory_order_release);
}

void SerialDataSession::open(const SerialConfig &config)
{
    m_impl->packetMailbox.pauseAndClear();
    Impl *impl = m_impl.get();
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    bool posted = false;
    {
        QMutexLocker commandLock(&m_impl->commandMutex);
        posted = postToBackend([managed, config, impl] {
            if (managed) managed->open(config);
            if (!impl->shuttingDown.load(std::memory_order_acquire)) {
                impl->packetMailbox.resume();
            }
        }, BackendTaskPriority::OrderedControl);
        if (posted) m_impl->acceptingWrites = true;
    }
    if (!posted && !m_impl->shuttingDown.load(std::memory_order_acquire)) {
        m_impl->packetMailbox.resume();
    }
}

void SerialDataSession::close()
{
    m_impl->packetMailbox.pauseAndClear();
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    bool posted = false;
    {
        QMutexLocker commandLock(&m_impl->commandMutex);
        m_impl->acceptingWrites = false;
        posted = postToBackend([managed] {
            if (managed) managed->close();
        }, BackendTaskPriority::OrderedControl);
    }
    if (!posted) {
        if (!m_impl->shuttingDown.load(std::memory_order_acquire)) {
            m_impl->packetMailbox.resume();
        }
    }
}

void SerialDataSession::closeAfterWrites(const QByteArray &finalWrite,
                                         const int drainDelayMs)
{
    m_impl->packetMailbox.pauseAndClear();
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    bool posted = false;
    {
        QMutexLocker commandLock(&m_impl->commandMutex);
        m_impl->acceptingWrites = false;
        posted = postToBackend([managed, finalWrite, drainDelayMs] {
            if (managed) {
                managed->closeAfterWrites(finalWrite,
                                          std::max(0, drainDelayMs));
            }
        }, BackendTaskPriority::OrderedControl);
    }
    if (!posted) {
        if (!m_impl->shuttingDown.load(std::memory_order_acquire)) {
            m_impl->packetMailbox.resume();
        }
    }
}

void SerialDataSession::write(const QByteArray &data)
{
    if (data.isEmpty()) return;
    QMutexLocker commandLock(&m_impl->commandMutex);
    if (!m_impl->acceptingWrites) {
        QMetaObject::invokeMethod(this, [this] {
            emit errorOccurred(tr("设备会话已进入关闭边界，写入未接受"));
        }, Qt::QueuedConnection);
        return;
    }
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    postToBackend([managed, data] {
        if (managed) managed->write(data);
    });
}

void SerialDataSession::resetDecoder()
{
    m_impl->packetMailbox.pauseAndClear();
    Impl *impl = m_impl.get();
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    if (!postToBackend([managed, impl] {
        if (managed) managed->resetDecoder();
        if (!impl->shuttingDown.load(std::memory_order_acquire)) {
            impl->packetMailbox.resume();
        }
    }, BackendTaskPriority::OrderedControl)) {
        if (!m_impl->shuttingDown.load(std::memory_order_acquire)) {
            m_impl->packetMailbox.resume();
        }
    }
}

void SerialDataSession::setDecodingEnabled(const bool enabled)
{
    m_impl->packetMailbox.pauseAndClear();
    Impl *impl = m_impl.get();
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    if (!postToBackend([managed, enabled, impl] {
        if (managed) managed->setDecodingEnabled(enabled);
        if (enabled && !impl->shuttingDown.load(std::memory_order_acquire)) {
            impl->packetMailbox.resume();
        }
    }, BackendTaskPriority::OrderedControl)) {
        if (!m_impl->shuttingDown.load(std::memory_order_acquire)) {
            m_impl->packetMailbox.resume();
        }
    }
}

void SerialDataSession::ingest(const QByteArray &data)
{
    QPointer<ManagedSerialDataSession> managed = m_impl->managed;
    postToBackend([managed, data] {
        if (managed) managed->ingest(data);
    });
}

bool SerialDataSession::postToBackend(std::function<void()> task,
                                      const BackendTaskPriority priority)
{
    if (m_impl->shuttingDown.load(std::memory_order_acquire)) return false;
    if (BackendRuntime::instance().runInBack(
            std::move(task), priority)) {
        return true;
    }
    QMetaObject::invokeMethod(this, [this] {
        emit errorOccurred(tr("后台任务队列已满，操作未提交"));
    }, Qt::QueuedConnection);
    return false;
}

void SerialDataSession::drainFrontendPackets()
{
    if (m_impl->shuttingDown.load(std::memory_order_acquire)) {
        m_impl->packetMailbox.clear();
        (void)m_impl->packetMailbox.finishDrain();
        return;
    }
    for (qsizetype delivered = 0;
         delivered < m_impl->packetMailbox.maxBatchesPerDrain(); ++delivered) {
        PacketList packets = m_impl->packetMailbox.takeNext();
        if (packets.isEmpty()) break;
        emit packetsDecoded(packets);
    }
    if (m_impl->packetMailbox.finishDrain()) {
        QMetaObject::invokeMethod(this, &SerialDataSession::drainFrontendPackets,
                                  Qt::QueuedConnection);
    }
}

} // namespace serein::scope

#include "SerialDataSession.moc"
