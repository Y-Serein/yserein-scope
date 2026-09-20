#pragma once

#include "Change.h"

#include <QtCore/QString>
#include <QtCore/QVector>

namespace serein::scope::yserein_hmi {

// Maintains an index-pointer stack of applied changes. Each command stores the
// forward change and its inverse, so undo and redo simply return the change the
// caller should apply to the model. Commands pushed while a transaction group is
// open are coalesced into a single undo step when the group closes.
//
// The controller is intentionally decoupled from HmiProjectModel: the caller
// applies the returned change via HmiProjectModel::applyChange and keeps model
// and controller in sync round after round.
class UndoController final {
public:
    explicit UndoController(int limit = 200);

    void clear();

    // Records an applied change. Any pending redo history is dropped, as a new
    // edit forks the history.
    void push(const Change &change, const QString &label = {});

    bool canUndo() const noexcept { return m_index > 0; }
    bool canRedo() const noexcept { return m_index < m_commands.size(); }

    // Returns the change to apply to reverse the last command, or an empty Change
    // when nothing is to undo.
    Change undo();

    // Returns the change to re-apply after an undo, or an empty Change.
    Change redo();

    // Opens a transaction group. While a group is open, pushes are accumulated
    // and committed as one command by endGroup(). Depth counting supports nesting.
    void beginGroup(const QString &label = {});
    bool endGroup();

    bool inGroup() const noexcept { return m_groupDepth > 0; }

    // Number of committed commands available for undo/redo.
    int commandCount() const noexcept { return m_commands.size(); }
    int undoCount() const noexcept { return m_index; }
    int redoCount() const noexcept { return m_commands.size() - m_index; }

    // Human label for the command about to be undone (or re-applied), used by
    // undo/redo UI tooltips. Empty when nothing is available.
    [[nodiscard]] QString undoText() const;
    [[nodiscard]] QString redoText() const;

private:
    struct Command {
        Change forward;
        Change inverse;
    };

    void commit(Change forward);

    int m_limit = 200;
    QVector<Command> m_commands;
    int m_index = 0;

    int m_groupDepth = 0;
    QString m_groupLabel;
    QVector<ChangeOp> m_groupOps;
};

} // namespace serein::scope::yserein_hmi
