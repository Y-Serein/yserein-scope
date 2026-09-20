#pragma once

#include "ControlModel.h"
#include "HmiProjectModel.h"

#include <QtCore/QJsonValue>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <variant>

namespace serein::scope::yserein_hmi {

// An operation that inserts a control into a page at a given z-order index.
struct ControlAdds {
    QString pageId;
    ControlModel control; // full snapshot; must carry a stable instanceId
    int index = -1;       // -1 appends to the end of the page's control list
};

// An operation that erases a control; `previous` is kept so an inverse can
// restore the exact z-order and content.
struct ControlRemoves {
    QString pageId;
    QString instanceId;
    ControlModel previous; // snapshot at the time the diff was built
    int index = -1;        // z-order the control occupied before removal
};

// One scalar field change on a control. `key` is dotted: "name",
// "geometry.x"/"geometry.y"/"geometry.width"/"geometry.height",
// "definitionId", "properties.<key>" or "extra.<key>". A null `newValue`
// removes a properties/extra entry; a null `oldValue` means it was absent.
struct FieldChange {
    QString key;
    QJsonValue oldValue;
    QJsonValue newValue;
};

// Applies a batch of field changes to an existing control.
struct ControlUpdates {
    QString pageId;
    QString instanceId;
    QVector<FieldChange> fields;
};

// Reorders one control to a new z-order index within its page.
struct ControlMoves {
    QString pageId;
    QString instanceId;
    int oldIndex = -1;
    int newIndex = -1;
};

// Inserts a page; carries the full page snapshot so undo can restore it.
struct PageAdds {
    HmiProjectModel::Page page;
    int index = -1;
};

struct PageRemoves {
    QString pageId;
    HmiProjectModel::Page previous; // snapshot so inverse can restore it
    int index = -1;
};

struct PageRenames {
    QString pageId;
    QString oldName;
    QString newName;
};

using ChangeOp = std::variant<ControlAdds, ControlRemoves, ControlUpdates, ControlMoves,
                              PageAdds, PageRemoves, PageRenames>;

// A sequence of primitive operations that transforms one valid project state
// into another. `Change::inverted()` produces the reverse sequence, which makes
// undo and redo a simple matter of applying the returned change.
class Change final {
public:
    QVector<ChangeOp> ops;
    QString label; // human-readable command label for undo UI / debugging

    bool empty() const noexcept { return ops.isEmpty(); }
    Change inverted() const;
};

} // namespace serein::scope::yserein_hmi
