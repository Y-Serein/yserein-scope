#pragma once

#include "CoreExport.h"

#include <QtCore/QtTypes>

#include <functional>
#include <memory>

namespace serein::scope {

// Tracks I/O jobs submitted by one owner such as a plugin workspace. Closing
// the owner stops new submissions and waits for accepted jobs, so plugin code
// cannot remain active after its DLL is unloaded.
class SEREIN_SCOPE_CORE_EXPORT BackendIoTaskGroup final {
public:
    BackendIoTaskGroup();
    ~BackendIoTaskGroup();

    BackendIoTaskGroup(const BackendIoTaskGroup &) = delete;
    BackendIoTaskGroup &operator=(const BackendIoTaskGroup &) = delete;

    [[nodiscard]] bool run(std::function<void()> task);
    void stopAccepting();
    void waitForDone();
    void closeAndWait();

    [[nodiscard]] qsizetype pendingTasks() const;
    [[nodiscard]] bool isAccepting() const;

private:
    class State;
    std::shared_ptr<State> m_state;
};

} // namespace serein::scope
