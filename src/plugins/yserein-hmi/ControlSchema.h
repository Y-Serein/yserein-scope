#pragma once

#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>

#include <functional>
#include <vector>

namespace serein::scope::yserein_hmi {

class ControlModel;

// Which collapsible section of the Inspector a property belongs to. Geometry
// lives in the control-level `geometry` object, everything else lives in the
// `properties` object. The Inspector renders sections in this order.
enum class PropertyGroup {
    Geometry,   // x, y, width, height
    Content,    // text, value, placeholder, samples ...
    Appearance, // fontPixelSize, colors, scaleMode, digits ...
    Binding,    // binding, bindingMode
    Event,      // eventType, eventTarget
};

// Which widget the Inspector should use to edit a property.
enum class PropertyEditorKind {
    IntegerSpinbox,
    DoubleSpinbox,
    LineEdit,
    ComboBox,
    CheckBox,
    Color,
};

struct PropertyChoice {
    QJsonValue value;   // stored value (string, number, or bool)
    QString label;      // text shown in the Inspector
};

// One editable surface of a control type. The schema registry builds an
// ordered list of these per type; the Inspector and any future generic editor
// derive entirely from this list instead of bespoke per-type widget code.
struct PropertySpec {
    QString key;
    QString label;
    PropertyGroup group = PropertyGroup::Content;
    PropertyEditorKind editor = PropertyEditorKind::LineEdit;
    QJsonValue defaultValue;   // stored in `properties` for non-geometry keys
    double minValue = 0.0;     // IntegerSpinbox / DoubleSpinbox bounds
    double maxValue = 0.0;
    double step = 1.0;
    int decimals = 0;          // DoubleSpinbox precision
    int maxLength = 0;         // LineEdit limit
    QVector<PropertyChoice> choices;  // ComboBox options
};

// Declarative description of one control definition. This is the single source
// of truth for the property surface of a control type: which attributes are
// editable, what their defaults are, which binding modes and event types the
// type supports, and any type-specific validation. Inspector generation, model
// defaults and validation all derive from this table instead of bespoke if/else
// chains scattered across the codebase.
struct ControlSchema {
    QString keyword;            // stable type name, e.g. "number"
    QString definitionId;       // plugin component id, e.g. "yserein-hmi.numeric-display"
    QString displayName;        // user-facing name, e.g. "数值显示"
    QString category;           // library grouping, e.g. "显示组件"
    QString renderKind;         // draw behavior hint, e.g. "gauge"
    bool bindingRequired = false;
    bool interactiveDefault = false;

    QStringList propertyKeys = {};   // editable attribute whitelist within `properties`
    QJsonObject defaultProperties = {}; // defaults for authorable attributes
    QSet<QString> allowedBindingModes; // subset of none/read/write/readWrite
    QSet<QString> allowedEventTypes;   // subset of none/gotoPage/command/submit

    QVector<PropertySpec> specs; // ordered Inspector surface, including geometry

    // Type-specific validation beyond the generic attribute checks.
    std::function<bool(const ControlModel &, QString *)> validateProperties = nullptr;

    [[nodiscard]] bool allowsBindingMode(const QString &mode) const noexcept;
    [[nodiscard]] bool allowsEventType(const QString &eventType) const noexcept;
    [[nodiscard]] const PropertySpec *findSpec(const QString &key) const noexcept;
};

// Immutable lookup table of the schemas this plugin publishes. Unknown
// definition ids fall back to an opaque schema so future providers stay
// lossless; authorable controls are exposed separately from the fallback.
class ControlSchemaRegistry final {
public:
    static const ControlSchemaRegistry &instance();

    [[nodiscard]] const ControlSchema *findByKeyword(const QString &keyword) const noexcept;
    [[nodiscard]] const ControlSchema *findByDefinitionId(
        const QString &definitionId) const noexcept;
    [[nodiscard]] QString keywordForDefinitionId(const QString &definitionId) const noexcept;
    [[nodiscard]] const ControlSchema *fallback() const noexcept;
    [[nodiscard]] std::vector<const ControlSchema *> authorable() const noexcept;
    [[nodiscard]] QStringList categories() const noexcept;
    [[nodiscard]] std::vector<const ControlSchema *> byCategory(
        const QString &category) const noexcept;

private:
    ControlSchemaRegistry();

    QHash<QString, qsizetype> m_byKeyword;
    QHash<QString, qsizetype> m_byDefinitionId;
    std::vector<ControlSchema> m_schemas; // stable pointers after reserve()
};

} // namespace serein::scope::yserein_hmi
