#pragma once

#include "Change.h"

#include <optional>

namespace serein::scope::yserein_hmi {

// Builds a minimal `Change` by diffing two project states, or two control
// instances of the same stable id. Diffing is key-level and preserves unknown
// `properties`/`extra` entries, consistent with the model's lossless contract.
//
// `std::nullopt` means the diff could not be produced (for example, the two
// controls do not share an instanceId). An empty `Change` means the two states
// are already identical.
class ChangeBuilder final {
public:
    // Diff two complete model snapshots.
    static std::optional<Change> buildModelChange(const HmiProjectModel &before,
                                                  const HmiProjectModel &after,
                                                  QString *error = nullptr);

    // Diff two controls; both must carry the same instanceId.
    static std::optional<Change> buildControlChange(const QString &pageId,
                                                    const ControlModel &before,
                                                    const ControlModel &after,
                                                    QString *error = nullptr);

private:
    static QVector<FieldChange> diffControlFields(const ControlModel &before,
                                                  const ControlModel &after);
};

} // namespace serein::scope::yserein_hmi
