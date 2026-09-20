#include "ControlModel.h"

#include "ControlSchema.h"

#include <QtCore/QJsonObject>

#include <cmath>
#include <utility>

namespace serein::scope::yserein_hmi {
namespace {

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

bool validStableId(const QString &value)
{
    if (value.isEmpty()) return false;
    for (const QChar character : value) {
        if (!(character.isLetterOrNumber() || character == QLatin1Char('_')
              || character == QLatin1Char('-') || character == QLatin1Char('.'))) {
            return false;
        }
    }
    return true;
}

bool validIdentifier(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return pattern.match(value.trimmed()).hasMatch();
}

bool validBinding(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z_][A-Za-z0-9_.]*$"));
    return pattern.match(value.trimmed()).hasMatch();
}

bool finiteNumber(const QJsonValue &value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}

} // namespace

bool ControlGeometry::isValid() const noexcept
{
    return width > 0.0 && height > 0.0
        && std::isfinite(x) && std::isfinite(y)
        && std::isfinite(width) && std::isfinite(height);
}

QJsonObject ControlGeometry::toJsonObject() const
{
    return QJsonObject{
        {QStringLiteral("x"), x},
        {QStringLiteral("y"), y},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
    };
}

bool ControlGeometry::validate(QString *error) const
{
    if (width <= 0.0 || height <= 0.0) {
        setError(error, QStringLiteral("控件 geometry 尺寸必须大于 0"));
        return false;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        setError(error, QStringLiteral("控件 geometry 坐标必须是有限数值"));
        return false;
    }
    return true;
}

std::optional<ControlGeometry> ControlGeometry::fromJsonObject(
    const QJsonObject &object, QString *error)
{
    ControlGeometry geometry;
    if (!finiteNumber(object.value(QStringLiteral("x")))
        || !finiteNumber(object.value(QStringLiteral("y")))
        || !finiteNumber(object.value(QStringLiteral("width")))
        || !finiteNumber(object.value(QStringLiteral("height")))) {
        setError(error, QStringLiteral("控件 geometry 字段必须是有限数值"));
        return std::nullopt;
    }
    geometry.x = object.value(QStringLiteral("x")).toDouble();
    geometry.y = object.value(QStringLiteral("y")).toDouble();
    geometry.width = object.value(QStringLiteral("width")).toDouble();
    geometry.height = object.value(QStringLiteral("height")).toDouble();
    if (!geometry.validate(error)) return std::nullopt;
    return geometry;
}

const ControlSchema *ControlModel::resolveSchema(const ControlSchema *hint) const
{
    if (hint) return hint;
    const ControlSchemaRegistry &registry = ControlSchemaRegistry::instance();
    if (const ControlSchema *schema =
            registry.findByDefinitionId(definitionId)) {
        return schema;
    }
    return registry.fallback();
}

QJsonObject ControlModel::toJsonObject(const ControlSchema */*schema*/) const
{
    QJsonObject result = extra;
    result.insert(QStringLiteral("instanceId"), instanceId);
    result.insert(QStringLiteral("definitionId"), definitionId);
    result.insert(QStringLiteral("type"), type);
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("geometry"), geometry.toJsonObject());
    result.insert(QStringLiteral("properties"), properties);
    return result;
}

bool ControlModel::normalize(const ControlSchema *schema, QString *error)
{
    const ControlSchema &schemaRef = *resolveSchema(schema);
    Q_UNUSED(error);
    for (auto it = schemaRef.defaultProperties.constBegin();
         it != schemaRef.defaultProperties.constEnd(); ++it) {
        if (!properties.contains(it.key())) {
            properties.insert(it.key(), it.value());
        }
    }
    return true;
}

bool ControlModel::validate(const ControlSchema *schema, QString *error) const
{
    if (instanceId.trimmed().isEmpty()) {
        setError(error, QStringLiteral("控件的 instanceId 不能为空"));
        return false;
    }
    if (!validStableId(instanceId)) {
        setError(error, QStringLiteral("控件 instanceId 含非法字符：%1").arg(instanceId));
        return false;
    }
    if (definitionId.trimmed().isEmpty()) {
        setError(error, QStringLiteral("控件 %1 的 definitionId 不能为空").arg(instanceId));
        return false;
    }
    if (!validStableId(definitionId)) {
        setError(error, QStringLiteral("控件 %1 的 definitionId 含非法字符").arg(instanceId));
        return false;
    }

    const ControlSchema &schemaRef = *resolveSchema(schema);

    if (name.trimmed().isEmpty() || !validIdentifier(name)) {
        setError(error, QStringLiteral("控件 %1 的名称无效").arg(instanceId));
        return false;
    }
    if (!geometry.validate(error)) return false;

    // Binding and binding mode are generic, schema-described attributes.
    const QString binding = properties.value(QStringLiteral("binding")).toString().trimmed();
    const QString mode = properties
        .value(QStringLiteral("bindingMode"))
        .toString(schemaRef.defaultProperties.value(QStringLiteral("bindingMode"))
                      .toString(QStringLiteral("none")));
    if (!schemaRef.allowsBindingMode(mode)) {
        setError(error, QStringLiteral("控件 %1 的绑定方向与类型不匹配：%2")
                            .arg(instanceId, mode));
        return false;
    }
    if (!binding.isEmpty() && !validBinding(binding)) {
        setError(error, QStringLiteral("控件 %1 的数据绑定无效：%2").arg(instanceId, binding));
        return false;
    }
    if (mode == QStringLiteral("none")) {
        if (!binding.isEmpty()) {
            setError(error, QStringLiteral("控件 %1 该类型不支持数据绑定").arg(instanceId));
            return false;
        }
    } else if (binding.isEmpty()) {
        setError(error, QStringLiteral("控件 %1 的数据绑定不能为空").arg(instanceId));
        return false;
    }
    if (schemaRef.bindingRequired && binding.isEmpty()) {
        setError(error, QStringLiteral("控件 %1 的数据绑定不能为空").arg(instanceId));
        return false;
    }

    // Event type is generic; the cross-project page existence is validated in
    // the owning HmiProjectModel because it needs the full page set.
    const QString eventType = properties
        .value(QStringLiteral("eventType"))
        .toString(QStringLiteral("none"));
    if (!schemaRef.allowsEventType(eventType)) {
        setError(error, QStringLiteral("控件 %1 的事件与类型不匹配：%2")
                            .arg(instanceId, eventType));
        return false;
    }
    const QString eventTarget = properties
        .value(QStringLiteral("eventTarget")).toString().trimmed();
    if (eventType == QStringLiteral("gotoPage")) {
        if (!validStableId(eventTarget)) {
            setError(error, QStringLiteral("控件 %1 的页面跳转目标无效").arg(instanceId));
            return false;
        }
    } else if (eventType == QStringLiteral("command") && eventTarget.isEmpty()) {
        setError(error, QStringLiteral("控件 %1 的命令事件不能为空").arg(instanceId));
        return false;
    }

    // Visible/locked are generic boolean attributes.
    for (const QString &flag : {QStringLiteral("visible"), QStringLiteral("locked")}) {
        if (properties.contains(flag) && !properties.value(flag).isBool()) {
            setError(error, QStringLiteral("控件 %1 的属性 %2 必须是布尔值")
                                .arg(instanceId, flag));
            return false;
        }
    }

    // The schema optionally declares a 'value' / 'previewValue' slot. When it
    // does, the stored JSON must be a finite number for every type except text
    // input (whose slot is a string). When the schema does not declare one the
    // control is data-driven and the slot is absent.
    if (schemaRef.findSpec(QStringLiteral("value"))
        || schemaRef.findSpec(QStringLiteral("previewValue"))) {
        if (schemaRef.keyword != QStringLiteral("textInput")) {
            const QJsonValue value = properties.contains(QStringLiteral("value"))
                ? properties.value(QStringLiteral("value"))
                : properties.value(QStringLiteral("previewValue"));
            if (!finiteNumber(value)) {
                setError(error, QStringLiteral("控件 %1 的模拟值无效").arg(instanceId));
                return false;
            }
        }
    }

    if (schemaRef.validateProperties
        && !schemaRef.validateProperties(*this, error)) {
        return false;
    }
    return true;
}

std::optional<ControlModel> ControlModel::fromJsonObject(
    const QJsonObject &object, const ControlSchema *schema, QString *error)
{
    const QJsonValue instanceIdValue = object.value(QStringLiteral("instanceId"));
    if (!instanceIdValue.isString() || instanceIdValue.toString().trimmed().isEmpty()) {
        setError(error, QStringLiteral("控件缺少有效 instanceId"));
        return std::nullopt;
    }
    const QJsonValue definitionIdValue = object.value(QStringLiteral("definitionId"));
    if (!definitionIdValue.isString() || definitionIdValue.toString().trimmed().isEmpty()) {
        setError(error, QStringLiteral("控件缺少有效 definitionId"));
        return std::nullopt;
    }

    ControlModel control;
    control.instanceId = instanceIdValue.toString().trimmed();
    control.definitionId = definitionIdValue.toString().trimmed();

    const ControlSchema *effective = schema
        ? schema
        : ControlSchemaRegistry::instance().findByDefinitionId(control.definitionId);
    if (!effective) effective = ControlSchemaRegistry::instance().fallback();
    control.type = effective->keyword;

    control.name = object.value(QStringLiteral("name")).toString().trimmed();
    const auto geometry =
        ControlGeometry::fromJsonObject(object.value(QStringLiteral("geometry")).toObject(),
                                        error);
    if (!geometry) return std::nullopt;
    control.geometry = *geometry;

    const QJsonValue propertiesValue = object.value(QStringLiteral("properties"));
    if (propertiesValue.isObject()) {
        control.properties = propertiesValue.toObject();
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() != QStringLiteral("instanceId")
            && it.key() != QStringLiteral("definitionId")
            && it.key() != QStringLiteral("type")
            && it.key() != QStringLiteral("name")
            && it.key() != QStringLiteral("geometry")
            && it.key() != QStringLiteral("properties")) {
            control.extra.insert(it.key(), it.value());
        }
    }
    (void)control.normalize(effective, nullptr);
    if (!control.validate(effective, error)) return std::nullopt;
    return control;
}

QString ControlModel::binding() const
{
    return properties.value(QStringLiteral("binding")).toString();
}

QString ControlModel::bindingMode() const
{
    return properties.value(QStringLiteral("bindingMode")).toString();
}

QString ControlModel::eventType() const
{
    return properties.value(QStringLiteral("eventType")).toString();
}

QString ControlModel::eventTarget() const
{
    return properties.value(QStringLiteral("eventTarget")).toString();
}

void ControlModel::setBinding(const QString &value)
{
    properties.insert(QStringLiteral("binding"), value);
}

void ControlModel::setBindingMode(const QString &value)
{
    properties.insert(QStringLiteral("bindingMode"), value);
}

void ControlModel::setEventType(const QString &value)
{
    properties.insert(QStringLiteral("eventType"), value);
}

void ControlModel::setEventTarget(const QString &value)
{
    properties.insert(QStringLiteral("eventTarget"), value);
}

} // namespace serein::scope::yserein_hmi
