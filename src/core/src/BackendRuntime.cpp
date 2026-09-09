#include <serein/scope/BackendRuntime.h>

#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QPointer>
#include <QtCore/QQueue>
#include <QtCore/QSettings>
#include <QtCore/QSemaphore>
#include <QtCore/QThread>
#include <QtCore/QThreadPool>
#include <QtCore/QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <utility>

namespace serein::scope {
namespace {

using SteadyClock = std::chrono::steady_clock;

constexpr auto MaxControlTasksKey = "runtime/backend/max-control-tasks";
constexpr auto MaxNormalTasksKey = "runtime/backend/max-normal-tasks";
constexpr auto MaxTasksPerDrainKey = "runtime/backend/max-tasks-per-drain";
constexpr auto MaxConsecutiveControlTasksKey =
    "runtime/backend/max-consecutive-control-tasks";
constexpr auto TaskDrainIntervalKey = "runtime/backend/task-drain-interval-ms";
constexpr auto MaxIoTasksKey = "runtime/backend/max-io-tasks";
constexpr auto IoThreadCountKey = "runtime/backend/io-thread-count";

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

quint64 elapsedUsec(const SteadyClock::time_point started)
{
    return quint64(std::chrono::duration_cast<std::chrono::microseconds>(
        SteadyClock::now() - started).count());
}

class BackendWorker final : public QObject {
    Q_OBJECT

public:
    struct QueuedTask {
        std::function<void()> function;
        SteadyClock::time_point acceptedAt;
        quint64 sequence = 0;
        bool orderedAfterEarlier = false;
    };

    explicit BackendWorker(const BackendRuntimeLimits limits)
        : m_limits(limits)
    {
    }

    bool enqueue(std::function<void()> task, const BackendTaskPriority priority)
    {
        if (!task) return false;
        QMutexLocker lock(&m_queueMutex);
        if (!m_accepting) {
            ++m_stats.rejectedTasks;
            return false;
        }
        const bool normal = priority == BackendTaskPriority::Normal;
        QQueue<QueuedTask> &queue = normal ? m_normalTasks : m_controlTasks;
        const qsizetype limit = normal ? m_limits.maxNormalTasks
                                       : m_limits.maxControlTasks;
        if (queue.size() >= limit) {
            ++m_stats.rejectedTasks;
            return false;
        }
        queue.enqueue(QueuedTask{std::move(task), SteadyClock::now(),
                                m_nextTaskSequence++,
                                priority == BackendTaskPriority::OrderedControl});
        ++m_stats.acceptedTasks;
        updateQueueStatsLocked();
        return true;
    }

    void setup()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        {
            QMutexLocker lock(&m_queueMutex);
            m_accepting = true;
            m_stats.running = true;
        }
        m_taskDrain = new QTimer(this);
        m_taskDrain->setTimerType(Qt::PreciseTimer);
        m_taskDrain->setInterval(m_limits.taskDrainIntervalMs);
        connect(m_taskDrain, &QTimer::timeout, this, &BackendWorker::drainTasks);
        m_taskDrain->start();

        m_statsTimer = new QTimer(this);
        m_statsTimer->setInterval(1000);
        connect(m_statsTimer, &QTimer::timeout, this, &BackendWorker::publishStats);
        m_statsTimer->start();
        publishStats();
    }

    void shutdown()
    {
        Q_ASSERT(QThread::currentThread() == thread());
        {
            QMutexLocker lock(&m_queueMutex);
            m_accepting = false;
        }
        if (m_taskDrain) m_taskDrain->stop();
        if (m_statsTimer) m_statsTimer->stop();
        while (executeNextTask()) {
        }
        {
            QMutexLocker lock(&m_queueMutex);
            m_stats.running = false;
        }
        publishStats();
    }

    BackendRuntimeStats snapshot() const
    {
        QMutexLocker lock(&m_queueMutex);
        BackendRuntimeStats result = m_stats;
        result.queuedControlTasks = m_controlTasks.size();
        result.queuedNormalTasks = m_normalTasks.size();
        return result;
    }

    void registerSerialSession()
    {
        {
            QMutexLocker lock(&m_queueMutex);
            ++m_stats.activeSerialSessions;
        }
        publishStats();
    }

    void unregisterSerialSession()
    {
        {
            QMutexLocker lock(&m_queueMutex);
            if (m_stats.activeSerialSessions > 0) --m_stats.activeSerialSessions;
        }
        publishStats();
    }

signals:
    void statsReady(const serein::scope::BackendRuntimeStats &stats);
    void taskFailed(const QString &message);

private slots:
    void drainTasks()
    {
        for (qsizetype count = 0; count < m_limits.maxTasksPerDrain; ++count) {
            if (!executeNextTask()) break;
        }
    }

    void publishStats()
    {
        emit statsReady(snapshot());
    }

private:
    bool executeNextTask()
    {
        QueuedTask task;
        {
            QMutexLocker lock(&m_queueMutex);
            const bool orderedControlAtHead = !m_controlTasks.isEmpty()
                && m_controlTasks.head().orderedAfterEarlier;
            const bool orderedControlMustWait = orderedControlAtHead
                && !m_normalTasks.isEmpty()
                && m_normalTasks.head().sequence
                    < m_controlTasks.head().sequence;
            const bool chooseControl = !m_controlTasks.isEmpty()
                && !orderedControlMustWait
                && (orderedControlAtHead || m_normalTasks.isEmpty()
                    || m_consecutiveControlTasks
                           < m_limits.maxConsecutiveControlTasks);
            if (chooseControl) {
                task = std::move(m_controlTasks.head());
                m_controlTasks.dequeue();
                ++m_consecutiveControlTasks;
            } else if (!m_normalTasks.isEmpty()) {
                task = std::move(m_normalTasks.head());
                m_normalTasks.dequeue();
                m_consecutiveControlTasks = 0;
            } else if (!m_controlTasks.isEmpty()) {
                task = std::move(m_controlTasks.head());
                m_controlTasks.dequeue();
                ++m_consecutiveControlTasks;
            } else {
                updateQueueStatsLocked();
                return false;
            }
            m_stats.maxTaskQueueWaitUsec = std::max(
                m_stats.maxTaskQueueWaitUsec, elapsedUsec(task.acceptedAt));
            updateQueueStatsLocked();
        }

        const auto started = SteadyClock::now();
        QString failure;
        try {
            task.function();
        } catch (const std::exception &error) {
            failure = QString::fromUtf8(error.what());
        } catch (...) {
            failure = tr("后台任务发生未知异常");
        }
        {
            QMutexLocker lock(&m_queueMutex);
            m_stats.maxTaskExecutionUsec = std::max(
                m_stats.maxTaskExecutionUsec, elapsedUsec(started));
            if (failure.isEmpty()) {
                ++m_stats.executedTasks;
            } else {
                ++m_stats.failedTasks;
            }
        }
        if (!failure.isEmpty()) emit taskFailed(failure);
        return true;
    }

    void updateQueueStatsLocked()
    {
        m_stats.queuedControlTasks = m_controlTasks.size();
        m_stats.queuedNormalTasks = m_normalTasks.size();
        m_stats.taskQueueHighWater = std::max(
            m_stats.taskQueueHighWater,
            m_controlTasks.size() + m_normalTasks.size());
    }

    BackendRuntimeLimits m_limits;
    mutable QMutex m_queueMutex;
    QQueue<QueuedTask> m_controlTasks;
    QQueue<QueuedTask> m_normalTasks;
    BackendRuntimeStats m_stats;
    QTimer *m_taskDrain = nullptr;
    QTimer *m_statsTimer = nullptr;
    bool m_accepting = false;
    qsizetype m_consecutiveControlTasks = 0;
    quint64 m_nextTaskSequence = 0;
};

} // namespace

class BackendRuntime::Impl final {
public:
    explicit Impl(BackendRuntime *owner)
        : owner(owner)
    {
    }

    BackendRuntime *owner = nullptr;
    mutable QMutex mutex;
    QThread *thread = nullptr;
    BackendWorker *worker = nullptr;
    std::shared_ptr<QThreadPool> ioPool;
    BackendRuntimeLimits limits;
    BackendRuntimeStats cachedStats;
    std::atomic<qsizetype> pendingIo{0};
    std::atomic<qsizetype> ioHighWater{0};
    std::atomic<quint64> acceptedIo{0};
    std::atomic<quint64> rejectedIo{0};
    std::atomic<quint64> completedIo{0};
    std::atomic<quint64> failedIo{0};
    std::atomic<quint64> maxIoExecutionUsec{0};
    bool stopping = false;
};

BackendRuntime &BackendRuntime::instance()
{
    static BackendRuntime runtime;
    return runtime;
}

BackendRuntimeLimits BackendRuntime::configuredLimits()
{
    const QSettings settings;
    return configuredLimits(settings);
}

BackendRuntimeLimits BackendRuntime::configuredLimits(const QSettings &settings)
{
    const BackendRuntimeLimits defaults;
    BackendRuntimeLimits limits;
    limits.maxControlTasks = boundedSetting<qsizetype>(
        settings, MaxControlTasksKey, defaults.maxControlTasks, 8, 4096);
    limits.maxNormalTasks = boundedSetting<qsizetype>(
        settings, MaxNormalTasksKey, defaults.maxNormalTasks, 32, 65536);
    limits.maxTasksPerDrain = boundedSetting<qsizetype>(
        settings, MaxTasksPerDrainKey, defaults.maxTasksPerDrain, 1, 1024);
    limits.maxConsecutiveControlTasks = boundedSetting<qsizetype>(
        settings, MaxConsecutiveControlTasksKey,
        defaults.maxConsecutiveControlTasks, 1, 64);
    limits.taskDrainIntervalMs = boundedSetting<int>(
        settings, TaskDrainIntervalKey, defaults.taskDrainIntervalMs, 1, 100);
    limits.maxIoTasks = boundedSetting<qsizetype>(
        settings, MaxIoTasksKey, defaults.maxIoTasks, 1, 4096);
    limits.ioThreadCount = boundedSetting<int>(
        settings, IoThreadCountKey, defaults.ioThreadCount, 1, 32);
    return limits;
}

BackendRuntime::BackendRuntime(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(this))
{
    qRegisterMetaType<BackendRuntimeStats>();
}

BackendRuntime::~BackendRuntime()
{
    stop();
}

bool BackendRuntime::start(const BackendRuntimeLimits limits)
{
    if (limits.maxControlTasks <= 0 || limits.maxNormalTasks <= 0
        || limits.maxTasksPerDrain <= 0
        || limits.maxConsecutiveControlTasks <= 0
        || limits.taskDrainIntervalMs <= 0
        || limits.maxIoTasks <= 0 || limits.ioThreadCount <= 0) {
        emit runtimeError(tr("后台运行参数必须为正数"));
        return false;
    }

    QMutexLocker lock(&m_impl->mutex);
    if (m_impl->thread) return true;
    if (m_impl->stopping) return false;
    m_impl->limits = limits;
    m_impl->cachedStats = {};
    m_impl->pendingIo.store(0, std::memory_order_release);
    m_impl->ioHighWater.store(0, std::memory_order_release);
    m_impl->acceptedIo.store(0, std::memory_order_release);
    m_impl->rejectedIo.store(0, std::memory_order_release);
    m_impl->completedIo.store(0, std::memory_order_release);
    m_impl->failedIo.store(0, std::memory_order_release);
    m_impl->maxIoExecutionUsec.store(0, std::memory_order_release);

    auto *thread = new QThread(this);
    thread->setObjectName(QStringLiteral("Serein Scope Backend"));
    auto *worker = new BackendWorker(limits);
    worker->moveToThread(thread);
    connect(worker, &BackendWorker::statsReady, this,
            [this](BackendRuntimeStats stats) {
                stats.acceptedIoTasks = m_impl->acceptedIo.load(std::memory_order_acquire);
                stats.rejectedIoTasks = m_impl->rejectedIo.load(std::memory_order_acquire);
                stats.completedIoTasks = m_impl->completedIo.load(std::memory_order_acquire);
                stats.failedIoTasks = m_impl->failedIo.load(std::memory_order_acquire);
                stats.pendingIoTasks = m_impl->pendingIo.load(std::memory_order_acquire);
                stats.ioQueueHighWater = m_impl->ioHighWater.load(std::memory_order_acquire);
                stats.maxIoExecutionUsec = m_impl->maxIoExecutionUsec.load(
                    std::memory_order_acquire);
                {
                    QMutexLocker statsLock(&m_impl->mutex);
                    m_impl->cachedStats = stats;
                }
                emit statsChanged(stats);
            }, Qt::QueuedConnection);
    connect(worker, &BackendWorker::taskFailed,
            this, &BackendRuntime::runtimeError, Qt::QueuedConnection);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);

    m_impl->thread = thread;
    m_impl->worker = worker;
    m_impl->ioPool = std::make_shared<QThreadPool>();
    m_impl->ioPool->setObjectName(QStringLiteral("Serein Scope IO"));
    m_impl->ioPool->setMaxThreadCount(limits.ioThreadCount);
    thread->start();
    return QMetaObject::invokeMethod(worker, &BackendWorker::setup,
                                     Qt::BlockingQueuedConnection);
}

void BackendRuntime::stop()
{
    BackendWorker *worker = nullptr;
    QThread *thread = nullptr;
    std::shared_ptr<QThreadPool> ioPool;
    {
        QMutexLocker lock(&m_impl->mutex);
        if (!m_impl->thread) return;
        m_impl->stopping = true;
        worker = m_impl->worker;
        thread = m_impl->thread;
        ioPool = m_impl->ioPool;
        m_impl->ioPool.reset();
        m_impl->worker = nullptr;
        m_impl->thread = nullptr;
    }

    if (ioPool) {
        ioPool->waitForDone();
    }
    if (worker && thread->isRunning()) {
        QMetaObject::invokeMethod(worker, &BackendWorker::shutdown,
                                  Qt::BlockingQueuedConnection);
    }
    thread->quit();
    thread->wait();
    {
        QMutexLocker lock(&m_impl->mutex);
        m_impl->cachedStats.running = false;
        m_impl->cachedStats.pendingIoTasks = 0;
        m_impl->stopping = false;
    }
    delete thread;
}

bool BackendRuntime::isRunning() const noexcept
{
    QMutexLocker lock(&m_impl->mutex);
    return m_impl->thread && m_impl->thread->isRunning();
}

bool BackendRuntime::isBackendThread() const noexcept
{
    QMutexLocker lock(&m_impl->mutex);
    return m_impl->thread && QThread::currentThread() == m_impl->thread;
}

BackendRuntimeStats BackendRuntime::stats() const
{
    QMutexLocker lock(&m_impl->mutex);
    BackendRuntimeStats result = m_impl->cachedStats;
    result.acceptedIoTasks = m_impl->acceptedIo.load(std::memory_order_acquire);
    result.rejectedIoTasks = m_impl->rejectedIo.load(std::memory_order_acquire);
    result.completedIoTasks = m_impl->completedIo.load(std::memory_order_acquire);
    result.failedIoTasks = m_impl->failedIo.load(std::memory_order_acquire);
    result.pendingIoTasks = m_impl->pendingIo.load(std::memory_order_acquire);
    result.ioQueueHighWater = m_impl->ioHighWater.load(std::memory_order_acquire);
    result.maxIoExecutionUsec = m_impl->maxIoExecutionUsec.load(
        std::memory_order_acquire);
    result.running = m_impl->thread && m_impl->thread->isRunning();
    return result;
}

BackendRuntimeLimits BackendRuntime::limits() const
{
    QMutexLocker lock(&m_impl->mutex);
    return m_impl->limits;
}

bool BackendRuntime::runInBack(std::function<void()> task,
                               const BackendTaskPriority priority)
{
    if (!isRunning() && !start()) return false;
    QMutexLocker lock(&m_impl->mutex);
    BackendWorker *worker = m_impl->worker;
    return !m_impl->stopping && worker
        && worker->enqueue(std::move(task), priority);
}

bool BackendRuntime::runIo(std::function<void()> task)
{
    if (!task || (!isRunning() && !start())) return false;
    QMutexLocker lock(&m_impl->mutex);
    const std::shared_ptr<QThreadPool> pool = m_impl->ioPool;
    if (m_impl->stopping || !pool) return false;

    const qsizetype previous = m_impl->pendingIo.fetch_add(1, std::memory_order_acq_rel);
    if (previous >= m_impl->limits.maxIoTasks) {
        m_impl->pendingIo.fetch_sub(1, std::memory_order_acq_rel);
        m_impl->rejectedIo.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    m_impl->acceptedIo.fetch_add(1, std::memory_order_relaxed);
    qsizetype high = m_impl->ioHighWater.load(std::memory_order_relaxed);
    while (high < previous + 1
           && !m_impl->ioHighWater.compare_exchange_weak(
               high, previous + 1, std::memory_order_release,
               std::memory_order_relaxed)) {
    }
    pool->start([impl = m_impl.get(), owner = QPointer<BackendRuntime>(this),
                 task = std::move(task)]() mutable {
        const auto started = SteadyClock::now();
        QString failure;
        try {
            task();
        } catch (const std::exception &error) {
            failure = QString::fromUtf8(error.what());
        } catch (...) {
            failure = QObject::tr("后台 I/O 任务发生未知异常");
        }
        const quint64 durationUsec = elapsedUsec(started);
        quint64 high = impl->maxIoExecutionUsec.load(std::memory_order_relaxed);
        while (high < durationUsec
               && !impl->maxIoExecutionUsec.compare_exchange_weak(
                   high, durationUsec, std::memory_order_release,
                   std::memory_order_relaxed)) {
        }
        if (!failure.isEmpty()) {
            impl->failedIo.fetch_add(1, std::memory_order_relaxed);
            if (owner) {
                QMetaObject::invokeMethod(owner, [owner, failure] {
                    if (owner) emit owner->runtimeError(failure);
                }, Qt::QueuedConnection);
            }
        }
        impl->completedIo.fetch_add(1, std::memory_order_relaxed);
        impl->pendingIo.fetch_sub(1, std::memory_order_acq_rel);
    });
    return true;
}

bool BackendRuntime::runBlocking(const std::function<void()> &task)
{
    if (!task || (!isRunning() && !start())) return false;
    QMutexLocker lock(&m_impl->mutex);
    BackendWorker *worker = m_impl->worker;
    if (m_impl->stopping || !worker) return false;
    if (QThread::currentThread() == worker->thread()) {
        task();
        return true;
    }
    return QMetaObject::invokeMethod(worker, task, Qt::BlockingQueuedConnection);
}

bool BackendRuntime::runOrderedBlocking(const std::function<void()> &task)
{
    if (!task || (!isRunning() && !start())) return false;
    QSemaphore completed;
    std::exception_ptr failure;
    {
        QMutexLocker lock(&m_impl->mutex);
        BackendWorker *worker = m_impl->worker;
        if (m_impl->stopping || !worker) return false;
        if (QThread::currentThread() == worker->thread()) {
            task();
            return true;
        }
        if (!worker->enqueue([&task, &failure, &completed] {
                try {
                    task();
                } catch (...) {
                    failure = std::current_exception();
                }
                completed.release();
            }, BackendTaskPriority::OrderedControl)) {
            return false;
        }
    }
    completed.acquire();
    if (failure) std::rethrow_exception(failure);
    return true;
}

void BackendRuntime::registerSerialSession()
{
    runBlocking([this] {
        if (m_impl->worker) m_impl->worker->registerSerialSession();
    });
}

void BackendRuntime::unregisterSerialSession()
{
    runBlocking([this] {
        if (m_impl->worker) m_impl->worker->unregisterSerialSession();
    });
}

} // namespace serein::scope

#include "BackendRuntime.moc"
