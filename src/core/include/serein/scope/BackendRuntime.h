#pragma once

#include "CoreExport.h"

#include <QtCore/QObject>

#include <functional>
#include <memory>

class QSettings;

namespace serein::scope {

enum class BackendTaskPriority {
    Control,
    Normal,
    // Runs from the control queue, but only after every task accepted before
    // it. Later tasks remain behind the fence. This is used for lifetime and
    // graceful-close boundaries that must not overtake accepted data work.
    OrderedControl,
};

struct BackendRuntimeLimits {
    qsizetype maxControlTasks = 256;
    qsizetype maxNormalTasks = 4096;
    qsizetype maxTasksPerDrain = 64;
    qsizetype maxConsecutiveControlTasks = 8;
    int taskDrainIntervalMs = 2;
    qsizetype maxIoTasks = 64;
    int ioThreadCount = 4;
};

struct BackendRuntimeStats {
    quint64 acceptedTasks = 0;
    quint64 rejectedTasks = 0;
    quint64 executedTasks = 0;
    quint64 failedTasks = 0;
    quint64 acceptedIoTasks = 0;
    quint64 rejectedIoTasks = 0;
    quint64 completedIoTasks = 0;
    quint64 failedIoTasks = 0;
    quint64 maxTaskQueueWaitUsec = 0;
    quint64 maxTaskExecutionUsec = 0;
    quint64 maxIoExecutionUsec = 0;
    qsizetype queuedControlTasks = 0;
    qsizetype queuedNormalTasks = 0;
    qsizetype taskQueueHighWater = 0;
    qsizetype pendingIoTasks = 0;
    qsizetype ioQueueHighWater = 0;
    qsizetype activeSerialSessions = 0;
    bool running = false;
};

// Process-wide backend composition root, following the same front/back
// boundary used by YSerein Swarm. The QObject itself remains on the frontend
// thread; all accepted backend tasks execute on one dedicated backend thread.
// Blocking helpers are private and reserved for lifetime boundaries where the
// caller must know that a session has actually been created or destroyed.
class SEREIN_SCOPE_CORE_EXPORT BackendRuntime final : public QObject {
    Q_OBJECT

public:
    static BackendRuntime &instance();
    static BackendRuntimeLimits configuredLimits();
    static BackendRuntimeLimits configuredLimits(const QSettings &settings);

    BackendRuntime(const BackendRuntime &) = delete;
    BackendRuntime &operator=(const BackendRuntime &) = delete;

    bool start(BackendRuntimeLimits limits = {});
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool isBackendThread() const noexcept;
    [[nodiscard]] BackendRuntimeStats stats() const;
    [[nodiscard]] BackendRuntimeLimits limits() const;

    [[nodiscard]] bool runInBack(std::function<void()> task,
                                 BackendTaskPriority priority =
                                     BackendTaskPriority::Normal);
    [[nodiscard]] bool runIo(std::function<void()> task);

signals:
    void statsChanged(const serein::scope::BackendRuntimeStats &stats);
    void runtimeError(const QString &message);

private:
    friend class SerialDataSession;

    explicit BackendRuntime(QObject *parent = nullptr);
    ~BackendRuntime() override;

    class Impl;
    std::unique_ptr<Impl> m_impl;

    bool runBlocking(const std::function<void()> &task);
    bool runOrderedBlocking(const std::function<void()> &task);
    void registerSerialSession();
    void unregisterSerialSession();
};

} // namespace serein::scope

Q_DECLARE_METATYPE(serein::scope::BackendRuntimeStats)
