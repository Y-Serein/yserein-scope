#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <optional>

namespace serein::scope::yserein_hmi {

class ControlSchema;

// Local geometry of a control, in target-device logical pixels. Penpot keeps a
// local rect plus a derived selrect/points/matrix; until rotation/scale exist,
// the local rect is authoritative and the device-space selrect is derived by
// the canvas at paint time.
struct ControlGeometry {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] QJsonObject toJsonObject() const;
    [[nodiscard]] bool validate(QString *error = nullptr) const;
    static std::optional<ControlGeometry> fromJsonObject(const QJsonObject &object,
                                                         QString *error = nullptr);
};

// Typed, polymorphic control instance. All editable attributes live in the flat
// `properties` map described by the ControlSchema for this component id; keys
// the current schema does not know about are preserved verbatim so a load/save
// round trip stays lossless for future providers.
class ControlModel final {
public:
    QString instanceId;
    QString definitionId;
    QString type;       // schema keyword, denormalized from definitionId
    QString name;       // user identifier, unique within a page
    ControlGeometry geometry;
    QJsonObject properties;
    QJsonObject extra;  // unknown top-level keys preserved for round trips

    [[nodiscard]] QJsonObject toJsonObject(const ControlSchema *schema = nullptr) const;
    [[nodiscard]] bool normalize(const ControlSchema *schema, QString *error = nullptr);
    [[nodiscard]] bool validate(const ControlSchema *schema, QString *error = nullptr) const;
    static std::optional<ControlModel> fromJsonObject(const QJsonObject &object,
                                                      const ControlSchema *schema,
                                                      QString *error = nullptr);

    // Runtime accessors over the generic attributes inside `properties`.
    [[nodiscard]] QString binding() const;
    [[nodiscard]] QString bindingMode() const;
    [[nodiscard]] QString eventType() const;
    [[nodiscard]] QString eventTarget() const;
    void setBinding(const QString &value);
    void setBindingMode(const QString &value);
    void setEventType(const QString &value);
    void setEventTarget(const QString &value);

private:
    [[nodiscard]] const ControlSchema *resolveSchema(const ControlSchema *hint) const;
};

} // namespace serein::scope::yserein_hmi
