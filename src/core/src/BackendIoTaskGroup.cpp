#include <serein/scope/BackendIoTaskGroup.h>

#include <serein/scope/BackendRuntime.h>

#include <condition_variable>
#include <mutex>
#include <utility>

namespace serein::scope {

class BackendIoTaskGroup::State final {
public:
    void finish()
    {
        std::lock_guard lock(mutex);
        if (pending > 0) --pending;
        if (pending == 0) completed.notify_all();
    }

    mutable std::mutex mutex;
    std::condition_variable completed;
    qsizetype pending = 0;
    bool accepting = true;
};

BackendIoTaskGroup::BackendIoTaskGroup()
    : m_state(std::make_shared<State>())
{
}

BackendIoTaskGroup::~BackendIoTaskGroup()
{
    closeAndWait();
}

bool BackendIoTaskGroup::run(std::function<void()> task)
{
    if (!task) return false;
    const std::shared_ptr<State> state = m_state;
    {
        std::lock_guard lock(state->mutex);
        if (!state->accepting) return false;
        ++state->pending;
    }

    const bool accepted = BackendRuntime::instance().runIo(
        [state, task = std::move(task)]() mutable {
            struct Completion final {
                std::shared_ptr<State> state;
                ~Completion() { state->finish(); }
            } completion{state};
            task();
        });
    if (!accepted) state->finish();
    return accepted;
}

void BackendIoTaskGroup::stopAccepting()
{
    std::lock_guard lock(m_state->mutex);
    m_state->accepting = false;
}

void BackendIoTaskGroup::waitForDone()
{
    std::unique_lock lock(m_state->mutex);
    m_state->completed.wait(lock, [this] { return m_state->pending == 0; });
}

void BackendIoTaskGroup::closeAndWait()
{
    stopAccepting();
    waitForDone();
}

qsizetype BackendIoTaskGroup::pendingTasks() const
{
    std::lock_guard lock(m_state->mutex);
    return m_state->pending;
}

bool BackendIoTaskGroup::isAccepting() const
{
    std::lock_guard lock(m_state->mutex);
    return m_state->accepting;
}

} // namespace serein::scope
