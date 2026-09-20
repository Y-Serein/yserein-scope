#include "Change.h"

#include <type_traits>
#include <utility>

namespace serein::scope::yserein_hmi {

Change Change::inverted() const
{
    Change result;
    result.label = label;
    result.ops.reserve(ops.size());

    // The inverse of a sequence of operations is the reversed sequence of the
    // per-operation inverses, applied in reverse order.
    for (auto it = ops.crbegin(); it != ops.crend(); ++it) {
        std::visit([&result](const auto &o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, ControlAdds>) {
                result.ops.push_back(
                    ControlRemoves{o.pageId, o.control.instanceId, o.control, o.index});
            } else if constexpr (std::is_same_v<T, ControlRemoves>) {
                result.ops.push_back(ControlAdds{o.pageId, o.previous, o.index});
            } else if constexpr (std::is_same_v<T, ControlUpdates>) {
                QVector<FieldChange> reversed;
                reversed.reserve(o.fields.size());
                for (auto f = o.fields.crbegin(); f != o.fields.crend(); ++f) {
                    FieldChange field;
                    field.key = f->key;
                    field.oldValue = f->newValue;
                    field.newValue = f->oldValue;
                    reversed.push_back(std::move(field));
                }
                result.ops.push_back(ControlUpdates{o.pageId, o.instanceId, std::move(reversed)});
            } else if constexpr (std::is_same_v<T, ControlMoves>) {
                result.ops.push_back(ControlMoves{o.pageId, o.instanceId, o.newIndex, o.oldIndex});
            } else if constexpr (std::is_same_v<T, PageAdds>) {
                result.ops.push_back(PageRemoves{o.page.pageId, o.page, o.index});
            } else if constexpr (std::is_same_v<T, PageRemoves>) {
                result.ops.push_back(PageAdds{o.previous, o.index});
            } else if constexpr (std::is_same_v<T, PageRenames>) {
                result.ops.push_back(PageRenames{o.pageId, o.newName, o.oldName});
            }
        }, *it);
    }
    return result;
}

} // namespace serein::scope::yserein_hmi
