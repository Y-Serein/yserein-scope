#include "ChangeBuilder.h"

#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QStringList>

#include <algorithm>
#include <utility>

namespace serein::scope::yserein_hmi {
namespace {

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

} // namespace

QVector<FieldChange> ChangeBuilder::diffControlFields(const ControlModel &before,
                                                      const ControlModel &after)
{
    QVector<FieldChange> fields;

    if (before.name != after.name)
        fields.push_back({QStringLiteral("name"), before.name, after.name});

    if (before.geometry.x != after.geometry.x)
        fields.push_back({QStringLiteral("geometry.x"), before.geometry.x, after.geometry.x});
    if (before.geometry.y != after.geometry.y)
        fields.push_back({QStringLiteral("geometry.y"), before.geometry.y, after.geometry.y});
    if (before.geometry.width != after.geometry.width)
        fields.push_back(
            {QStringLiteral("geometry.width"), before.geometry.width, after.geometry.width});
    if (before.geometry.height != after.geometry.height)
        fields.push_back(
            {QStringLiteral("geometry.height"), before.geometry.height, after.geometry.height});

    if (before.definitionId != after.definitionId)
        fields.push_back({QStringLiteral("definitionId"),
                          before.definitionId, after.definitionId});

    // Diff `properties` key-by-key. This also covers binding/bindingMode/
    // eventType/eventTarget because ControlModel stores them in `properties`.
    // A null value models key absence, so both adds and removes diff cleanly
    // and invert.
    const auto diffMaps = [&fields](const QJsonObject &a, const QJsonObject &b,
                                    const QString &prefix) {
        QSet<QString> keys;
        for (const QString &key : a.keys()) keys.insert(key);
        for (const QString &key : b.keys()) keys.insert(key);
        QStringList ordered = keys.values();
        std::sort(ordered.begin(), ordered.end());
        for (const QString &key : ordered) {
            const bool inA = a.contains(key);
            const bool inB = b.contains(key);
            const QJsonValue va =
                inA ? a.value(key) : QJsonValue(QJsonValue::Null);
            const QJsonValue vb =
                inB ? b.value(key) : QJsonValue(QJsonValue::Null);
            if (va != vb)
                fields.push_back({prefix + key, va, vb});
        }
    };
    diffMaps(before.properties, after.properties, QStringLiteral("properties."));
    diffMaps(before.extra, after.extra, QStringLiteral("extra."));

    return fields;
}

std::optional<Change> ChangeBuilder::buildControlChange(const QString &pageId,
                                                        const ControlModel &before,
                                                        const ControlModel &after,
                                                        QString *error)
{
    if (before.instanceId != after.instanceId) {
        setError(error, QStringLiteral(
            "构建控件变更失败：前后 instanceId 不一致（%1 vs %2）")
            .arg(before.instanceId, after.instanceId));
        return std::nullopt;
    }

    QVector<FieldChange> fields = diffControlFields(before, after);
    if (fields.isEmpty()) return Change{};

    Change change;
    change.ops.push_back(ControlUpdates{pageId, after.instanceId, std::move(fields)});
    return change;
}

std::optional<Change> ChangeBuilder::buildModelChange(const HmiProjectModel &before,
                                                      const HmiProjectModel &after,
                                                      QString *error)
{
    Change change;

    QHash<QString, int> beforePageIndex;
    for (int i = 0; i < before.pages().size(); ++i)
        beforePageIndex.insert(before.pages().at(i).pageId, i);
    QHash<QString, int> afterPageIndex;
    for (int i = 0; i < after.pages().size(); ++i)
        afterPageIndex.insert(after.pages().at(i).pageId, i);

    // Page renames (no structural impact).
    for (const HmiProjectModel::Page &afterPage : after.pages()) {
        auto it = beforePageIndex.constFind(afterPage.pageId);
        if (it == beforePageIndex.constEnd()) continue;
        const HmiProjectModel::Page &beforePage = before.pages().at(it.value());
        if (beforePage.name != afterPage.name) {
            change.ops.push_back(
                PageRenames{afterPage.pageId, beforePage.name, afterPage.name});
        }
    }

    // Removed pages.
    for (const HmiProjectModel::Page &beforePage : before.pages()) {
        if (!afterPageIndex.contains(beforePage.pageId)) {
            change.ops.push_back(PageRemoves{
                beforePage.pageId, beforePage, beforePageIndex.value(beforePage.pageId)});
        }
    }

    // Added pages.
    for (const HmiProjectModel::Page &afterPage : after.pages()) {
        if (!beforePageIndex.contains(afterPage.pageId)) {
            change.ops.push_back(PageAdds{afterPage, -1});
        }
    }

    // Common pages: diff controls.
    for (const HmiProjectModel::Page &afterPage : after.pages()) {
        auto it = beforePageIndex.constFind(afterPage.pageId);
        if (it == beforePageIndex.constEnd()) continue;
        const HmiProjectModel::Page &beforePage = before.pages().at(it.value());

        QHash<QString, int> beforeIndexById;
        for (int i = 0; i < beforePage.controls.size(); ++i)
            beforeIndexById.insert(beforePage.controls.at(i).instanceId, i);
        QHash<QString, int> afterIndexById;
        for (int i = 0; i < afterPage.controls.size(); ++i)
            afterIndexById.insert(afterPage.controls.at(i).instanceId, i);

        // Erases.
        for (const ControlModel &beforeControl : beforePage.controls) {
            if (!afterIndexById.contains(beforeControl.instanceId)) {
                change.ops.push_back(ControlRemoves{
                    afterPage.pageId, beforeControl.instanceId,
                    beforeControl, beforeIndexById.value(beforeControl.instanceId)});
            }
        }
        // Appends (positioning handled by moves below).
        for (const ControlModel &afterControl : afterPage.controls) {
            if (!beforeIndexById.contains(afterControl.instanceId)) {
                change.ops.push_back(ControlAdds{afterPage.pageId, afterControl, -1});
            }
        }
        // Updates.
        for (const ControlModel &afterControl : afterPage.controls) {
            auto bit = beforeIndexById.constFind(afterControl.instanceId);
            if (bit == beforeIndexById.constEnd()) continue;
            const ControlModel &beforeControl = beforePage.controls.at(bit.value());
            auto update = buildControlChange(
                afterPage.pageId, beforeControl, afterControl, error);
            if (!update) return std::nullopt;
            for (ChangeOp &op : update->ops) change.ops.push_back(std::move(op));
        }
        // Reorders: simulate current id order after erases/appends, then move.
        {
            QVector<QString> staged;
            for (const ControlModel &c : beforePage.controls)
                if (afterIndexById.contains(c.instanceId)) staged.append(c.instanceId);
            for (const ControlModel &c : afterPage.controls)
                if (!beforeIndexById.contains(c.instanceId)) staged.append(c.instanceId);

            // Build the desired order of all controls in after order.
            QVector<QString> desired;
            for (const ControlModel &c : afterPage.controls)
                desired.append(c.instanceId);

            for (int j = 0; j < desired.size(); ++j) {
                if (staged.value(j) == desired.value(j)) continue;
                if (j >= staged.size()) {
                    staged.append(desired.value(j));
                    continue;
                }
                const int i = staged.indexOf(desired.value(j), j);
                if (i < 0) continue;
                staged.move(i, j);
                change.ops.push_back(
                    ControlMoves{afterPage.pageId, desired.value(j), i, j});
            }
        }
    }

    if (change.empty()) return Change{};
    return change;
}

} // namespace serein::scope::yserein_hmi
