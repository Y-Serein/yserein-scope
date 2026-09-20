#include "UndoController.h"

#include <algorithm>

namespace serein::scope::yserein_hmi {

UndoController::UndoController(const int limit)
    : m_limit(limit < 1 ? 1 : limit)
{
}

void UndoController::clear()
{
    m_commands.clear();
    m_index = 0;
    m_groupDepth = 0;
    m_groupOps.clear();
}

void UndoController::push(const Change &change, const QString &label)
{
    if (change.empty()) return;

    if (m_groupDepth > 0) {
        m_groupOps.reserve(m_groupOps.size() + change.ops.size());
        for (const ChangeOp &op : change.ops) m_groupOps.push_back(op);
        if (m_groupLabel.isEmpty()) m_groupLabel = label;
        return;
    }

    commit(Change{change.ops, label});
}

void UndoController::commit(Change forward)
{
    // A new edit truncates any pending redo branch.
    m_commands.resize(m_index);

    Command command{Change{forward.ops, forward.label}, forward.inverted()};

    // Enforce the depth limit by dropping the oldest commands.
    if (m_commands.size() >= m_limit) m_commands.removeFirst();

    m_commands.push_back(std::move(command));
    m_index = m_commands.size();
}

Change UndoController::undo()
{
    if (!canUndo()) return Change{};
    --m_index;
    // Returning a copy; the caller applies it and further append calls mutate.
    return m_commands.at(m_index).inverse;
}

Change UndoController::redo()
{
    if (!canRedo()) return Change{};
    Change change = m_commands.at(m_index).forward;
    ++m_index;
    return change;
}

QString UndoController::undoText() const
{
    if (!canUndo()) return {};
    return m_commands.at(m_index - 1).forward.label;
}

QString UndoController::redoText() const
{
    if (!canRedo()) return {};
    return m_commands.at(m_index).forward.label;
}

void UndoController::beginGroup(const QString &label)
{
    if (m_groupDepth == 0) {
        m_groupOps.clear();
        m_groupLabel = label;
    }
    ++m_groupDepth;
}

bool UndoController::endGroup()
{
    if (m_groupDepth == 0) return false;
    --m_groupDepth;
    if (m_groupDepth == 0 && !m_groupOps.isEmpty()) {
        Change forward;
        forward.ops = std::move(m_groupOps);
        forward.label = m_groupLabel;
        m_groupOps.clear();
        m_groupLabel.clear();
        commit(std::move(forward));
    }
    return true;
}

} // namespace serein::scope::yserein_hmi
