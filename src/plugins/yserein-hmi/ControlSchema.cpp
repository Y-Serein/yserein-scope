#include "ControlSchema.h"

#include "ControlModel.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QList>

#include <algorithm>
#include <cmath>
#include <utility>

namespace serein::scope::yserein_hmi {

namespace {

QSet<QString> makeModes(std::initializer_list<const char *> values)
{
    QSet<QString> result;
    for (const char *value : values) result.insert(QString::fromUtf8(value));
    return result;
}

QSet<QString> makeEvents(std::initializer_list<const char *> values)
{
    QSet<QString> result;
    for (const char *value : values) result.insert(QString::fromUtf8(value));
    return result;
}

// Value equals display label (used for mode / event enums whose stored value
// is identical to the user-facing text).
QVector<PropertyChoice> makeValueChoices(std::initializer_list<const char *> values)
{
    QVector<PropertyChoice> result;
    for (const char *value : values) {
        result.append({QJsonValue(QString::fromUtf8(value)), QString::fromUtf8(value)});
    }
    return result;
}

QVector<PropertyChoice> makeLabeledChoices(std::initializer_list<std::pair<const char *, const char *>> values)
{
    QVector<PropertyChoice> result;
    for (const auto &value : values) {
        result.append({QJsonValue(QString::fromUtf8(value.first)), QString::fromUtf8(value.second)});
    }
    return result;
}

QVector<PropertyChoice> makeIntChoices(std::initializer_list<std::pair<int, const char *>> values)
{
    QVector<PropertyChoice> result;
    for (const auto &value : values) {
        result.append({QJsonValue(value.first), QString::fromUtf8(value.second)});
    }
    return result;
}

PropertySpec makeSpec(const QString &key, const QString &label, PropertyGroup group,
                      PropertyEditorKind editor, const QJsonValue &def,
                      double minValue = 0.0, double maxValue = 0.0, double step = 1.0,
                      int decimals = 0, int maxLength = 0,
                      QVector<PropertyChoice> choices = {})
{
    PropertySpec spec;
    spec.key = key;
    spec.label = label;
    spec.group = group;
    spec.editor = editor;
    spec.defaultValue = def;
    spec.minValue = minValue;
    spec.maxValue = maxValue;
    spec.step = step;
    spec.decimals = decimals;
    spec.maxLength = maxLength;
    spec.choices = std::move(choices);
    return spec;
}

bool isFinite(const QJsonValue &value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}

} // namespace

bool ControlSchema::allowsBindingMode(const QString &mode) const noexcept
{
    return allowedBindingModes.contains(mode);
}

bool ControlSchema::allowsEventType(const QString &eventType) const noexcept
{
    return allowedEventTypes.contains(eventType);
}

const PropertySpec *ControlSchema::findSpec(const QString &key) const noexcept
{
    for (const PropertySpec &spec : specs) {
        if (spec.key == key) return &spec;
    }
    return nullptr;
}

const ControlSchemaRegistry &ControlSchemaRegistry::instance()
{
    static const ControlSchemaRegistry registry;
    return registry;
}

ControlSchemaRegistry::ControlSchemaRegistry()
{
    m_schemas.reserve(21);

    auto add = [this](ControlSchema schema) {
        // The declared property surface is the single source of truth for the
        // whitelist and defaults consumed by the model layer.
        for (const PropertySpec &spec : schema.specs) {
            if (spec.group == PropertyGroup::Geometry) continue;
            schema.propertyKeys.append(spec.key);
            schema.defaultProperties.insert(spec.key, spec.defaultValue);
        }
        m_byKeyword.insert(schema.keyword, m_schemas.size());
        if (!schema.definitionId.isEmpty()) {
            m_byDefinitionId.insert(schema.definitionId, m_schemas.size());
        }
        m_schemas.push_back(std::move(schema));
    };

    const double kNoBound = 1000000000.0;
    const QVector<PropertySpec> kGeometry = {
        makeSpec(QStringLiteral("x"), QStringLiteral("X 位置"), PropertyGroup::Geometry,
                 PropertyEditorKind::IntegerSpinbox, 0.0, 0.0, 8192.0, 1.0),
        makeSpec(QStringLiteral("y"), QStringLiteral("Y 位置"), PropertyGroup::Geometry,
                 PropertyEditorKind::IntegerSpinbox, 0.0, 0.0, 8192.0, 1.0),
        makeSpec(QStringLiteral("width"), QStringLiteral("宽度"), PropertyGroup::Geometry,
                 PropertyEditorKind::IntegerSpinbox, 150.0, 8.0, 8192.0, 8.0),
        makeSpec(QStringLiteral("height"), QStringLiteral("高度"), PropertyGroup::Geometry,
                 PropertyEditorKind::IntegerSpinbox, 48.0, 8.0, 8192.0, 8.0),
    };

    auto bindingSpecs = [](std::initializer_list<const char *> modes) {
        return QVector<PropertySpec>{
            makeSpec(QStringLiteral("binding"), QStringLiteral("数据绑定"), PropertyGroup::Binding,
                     PropertyEditorKind::LineEdit, QString()),
            makeSpec(QStringLiteral("bindingMode"), QStringLiteral("绑定方向"), PropertyGroup::Binding,
                     PropertyEditorKind::ComboBox, QString::fromLatin1(*modes.begin()),
                     0.0, 0.0, 1.0, 0, 0, makeValueChoices(modes)),
        };
    };

    auto eventSpecs = [](std::initializer_list<const char *> events) {
        return QVector<PropertySpec>{
            makeSpec(QStringLiteral("eventType"), QStringLiteral("触发事件"), PropertyGroup::Event,
                     PropertyEditorKind::ComboBox, QString::fromLatin1(*events.begin()),
                     0.0, 0.0, 1.0, 0, 0, makeValueChoices(events)),
            makeSpec(QStringLiteral("eventTarget"), QStringLiteral("事件目标"), PropertyGroup::Event,
                     PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 64),
        };
    };

    auto baseSpecs = [&](std::initializer_list<const char *> modes,
                         std::initializer_list<const char *> events) {
        QVector<PropertySpec> out = kGeometry;
        out += bindingSpecs(modes);
        out += eventSpecs(events);
        return out;
    };

    // ---------------------------- 文本 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"none"}, {"none"});
        s.append(makeSpec(QStringLiteral("text"), QStringLiteral("显示文本"), PropertyGroup::Content,
                          PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 128));
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("绑定值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#0f172a"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("text"),
            .definitionId = QStringLiteral("yserein-hmi.text"),
            .displayName = QStringLiteral("文本"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("text"),
            .bindingRequired = false,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"none"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
        });
    }

    // ---------------------------- 数值显示 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("绑定值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("previewValue"), QStringLiteral("模拟值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("decimals"), QStringLiteral("小数位"), PropertyGroup::Content,
                          PropertyEditorKind::IntegerSpinbox, 0, 0.0, 6.0, 1.0));
        s.append(makeSpec(QStringLiteral("unit"), QStringLiteral("单位"), PropertyGroup::Appearance,
                          PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 12));
        s.append(makeSpec(QStringLiteral("fontPixelSize"), QStringLiteral("字号"), PropertyGroup::Appearance,
                          PropertyEditorKind::IntegerSpinbox, 32, 12.0, 72.0, 1.0));
        s.append(makeSpec(QStringLiteral("alignment"), QStringLiteral("对齐"), PropertyGroup::Appearance,
                          PropertyEditorKind::ComboBox, 1, 0.0, 0.0, 1.0, 0, 0,
                          makeIntChoices({{0, "左对齐"}, {2, "居中"}, {1, "右对齐"}})));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("数值颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#0f172a"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("bindColor"), QStringLiteral("绑定颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#94a3b8"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("number"),
            .definitionId = QStringLiteral("yserein-hmi.numeric-display"),
            .displayName = QStringLiteral("数值显示"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("number"),
            .bindingRequired = true,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const int decimals = control.properties.value(QStringLiteral("decimals")).toInt(0);
                if (decimals < 0 || decimals > 6) {
                    if (error) *error = QStringLiteral("数值显示的小数位必须在 0 到 6 之间");
                    return false;
                }
                const int fontPixelSize =
                    control.properties.value(QStringLiteral("fontPixelSize")).toInt(32);
                if (fontPixelSize < 12 || fontPixelSize > 72) {
                    if (error) *error = QStringLiteral("数值显示的字号必须在 12 到 72 之间");
                    return false;
                }
                const int alignment =
                    control.properties.value(QStringLiteral("alignment")).toInt(1);
                if (alignment < 0 || alignment > 2) {
                    if (error) *error = QStringLiteral("数值显示的对齐方式无效");
                    return false;
                }
                const QString unit =
                    control.properties.value(QStringLiteral("unit")).toString().trimmed();
                if (unit.size() > 12) {
                    if (error) *error = QStringLiteral("数值显示的单位最长 12 个字符");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 按钮 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"none"}, {"none", "gotoPage", "command"});
        s.append(makeSpec(QStringLiteral("text"), QStringLiteral("按钮文本"), PropertyGroup::Content,
                          PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 64));
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("绑定值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("backgroundColor"), QStringLiteral("按钮颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#2563eb"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#ffffff"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("button"),
            .definitionId = QStringLiteral("yserein-hmi.button"),
            .displayName = QStringLiteral("按钮"),
            .category = QStringLiteral("控制组件"),
            .renderKind = QStringLiteral("button"),
            .bindingRequired = false,
            .interactiveDefault = true,
            .allowedBindingModes = makeModes({"none"}),
            .allowedEventTypes = makeEvents({"none", "gotoPage", "command"}),
            .specs = s,
        });
    }

    // ---------------------------- 进度条 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("进度值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("minValue"), QStringLiteral("最小值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("maxValue"), QStringLiteral("最大值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 100.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#334155"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("accentColor"), QStringLiteral("进度颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#3b82f6"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("trackColor"), QStringLiteral("轨道颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#e2e8f0"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("progress"),
            .definitionId = QStringLiteral("yserein-hmi.progress"),
            .displayName = QStringLiteral("进度条"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("progress"),
            .bindingRequired = true,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
        });
    }

    // ---------------------------- 状态灯 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("状态值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("activeColor"), QStringLiteral("点亮颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#22c55e"), 0.0, 0.0, 1.0, 0, 7));
        s.append(makeSpec(QStringLiteral("inactiveColor"), QStringLiteral("熄灭颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#cbd5e1"), 0.0, 0.0, 1.0, 0, 7));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#0f172a"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("status"),
            .definitionId = QStringLiteral("yserein-hmi.status-lamp"),
            .displayName = QStringLiteral("状态灯"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("status"),
            .bindingRequired = true,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
        });
    }

    // ---------------------------- 开关 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"readWrite"}, {"none"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("开关值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("onColor"), QStringLiteral("开启颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#22c55e"), 0.0, 0.0, 1.0, 0, 7));
        s.append(makeSpec(QStringLiteral("offColor"), QStringLiteral("关闭颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#cbd5e1"), 0.0, 0.0, 1.0, 0, 7));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#0f172a"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("switch"),
            .definitionId = QStringLiteral("yserein-hmi.switch"),
            .displayName = QStringLiteral("开关"),
            .category = QStringLiteral("控制组件"),
            .renderKind = QStringLiteral("switch"),
            .bindingRequired = true,
            .interactiveDefault = true,
            .allowedBindingModes = makeModes({"readWrite"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
        });
    }

    // ---------------------------- 图片 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"none"}, {"none"});
        s.append(makeSpec(QStringLiteral("assetId"), QStringLiteral("资源 ID"), PropertyGroup::Content,
                          PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 64));
        s.append(makeSpec(QStringLiteral("scaleMode"), QStringLiteral("缩放模式"), PropertyGroup::Appearance,
                          PropertyEditorKind::ComboBox, QStringLiteral("fit"), 0.0, 0.0, 1.0, 0, 0,
                          makeLabeledChoices({{"fit", "适应"}, {"fill", "填充"}, {"stretch", "拉伸"}, {"none", "原图"}})));
        add({
            .keyword = QStringLiteral("image"),
            .definitionId = QStringLiteral("yserein-hmi.image"),
            .displayName = QStringLiteral("图片"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("image"),
            .bindingRequired = false,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"none"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const QString assetId =
                    control.properties.value(QStringLiteral("assetId")).toString().trimmed();
                if (assetId.isEmpty()) {
                    if (error) *error = QStringLiteral("图片必须选择资源 ID");
                    return false;
                }
                const QString scaleMode =
                    control.properties.value(QStringLiteral("scaleMode")).toString(QStringLiteral("fit"));
                if (scaleMode != QStringLiteral("fit") && scaleMode != QStringLiteral("fill")
                    && scaleMode != QStringLiteral("stretch") && scaleMode != QStringLiteral("none")) {
                    if (error) *error = QStringLiteral("图片缩放模式无效");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 曲线 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("samples"), QStringLiteral("采样点"), PropertyGroup::Content,
                          PropertyEditorKind::LineEdit, QJsonArray(), 0.0, 0.0, 1.0, 0, 4096));
        s.append(makeSpec(QStringLiteral("minValue"), QStringLiteral("最小值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("maxValue"), QStringLiteral("最大值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 100.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("windowLength"), QStringLiteral("窗口长度"), PropertyGroup::Content,
                          PropertyEditorKind::IntegerSpinbox, 30, 2.0, 4096.0, 1.0));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#334155"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("accentColor"), QStringLiteral("曲线颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#3b82f6"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("gridColor"), QStringLiteral("网格颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#e2e8f0"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("chart"),
            .definitionId = QStringLiteral("yserein-hmi.chart"),
            .displayName = QStringLiteral("曲线"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("chart"),
            .bindingRequired = true,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const QJsonValue samplesValue =
                    control.properties.value(QStringLiteral("samples"));
                const QJsonArray samples = samplesValue.toArray();
                if (!samplesValue.isArray() || samples.isEmpty()) {
                    if (error) *error = QStringLiteral("曲线至少需要一个采样点");
                    return false;
                }
                const double minValue =
                    control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
                const double maxValue =
                    control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
                if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
                    if (error) *error = QStringLiteral("曲线范围无效");
                    return false;
                }
                const int window =
                    control.properties.value(QStringLiteral("windowLength")).toInt(30);
                if (window < 2 || window > 4096) {
                    if (error) *error = QStringLiteral("曲线窗口长度必须在 2 到 4096 之间");
                    return false;
                }
                for (const QJsonValue &sample : samples) {
                    if (!isFinite(sample)) {
                        if (error) *error = QStringLiteral("曲线采样点必须是有限数值");
                        return false;
                    }
                }
                return true;
            },
        });
    }

    // ---------------------------- 文本输入 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"write", "readWrite"}, {"none", "submit", "command"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("输入值"), PropertyGroup::Content,
                          PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 256));
        s.append(makeSpec(QStringLiteral("maxLength"), QStringLiteral("长度限制"), PropertyGroup::Content,
                          PropertyEditorKind::IntegerSpinbox, 32, 1.0, 256.0, 1.0));
        s.append(makeSpec(QStringLiteral("placeholder"), QStringLiteral("提示文本"), PropertyGroup::Appearance,
                          PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 64));
        s.append(makeSpec(QStringLiteral("backgroundColor"), QStringLiteral("背景色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#ffffff"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#334155"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("accentColor"), QStringLiteral("光标颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#3b82f6"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("borderColor"), QStringLiteral("边框颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#cbd5e1"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("textInput"),
            .definitionId = QStringLiteral("yserein-hmi.text-input"),
            .displayName = QStringLiteral("文本输入"),
            .category = QStringLiteral("输入组件"),
            .renderKind = QStringLiteral("textInput"),
            .bindingRequired = true,
            .interactiveDefault = true,
            .allowedBindingModes = makeModes({"write", "readWrite"}),
            .allowedEventTypes = makeEvents({"none", "submit", "command"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const int maxLength =
                    control.properties.value(QStringLiteral("maxLength")).toInt(32);
                if (maxLength < 1 || maxLength > 256) {
                    if (error) *error = QStringLiteral("文本输入长度限制必须在 1 到 256 之间");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 圆盘仪表 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("当前值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("minValue"), QStringLiteral("最小值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("maxValue"), QStringLiteral("最大值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 100.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("unit"), QStringLiteral("单位"), PropertyGroup::Appearance,
                          PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 12));
        s.append(makeSpec(QStringLiteral("threshold"), QStringLiteral("报警阈值"), PropertyGroup::Appearance,
                          PropertyEditorKind::DoubleSpinbox, 90.0, -kNoBound, kNoBound, 1.0, 2));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#334155"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("accentColor"), QStringLiteral("指针颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#3b82f6"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("trackColor"), QStringLiteral("轨道颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#e2e8f0"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("gauge"),
            .definitionId = QStringLiteral("yserein-hmi.gauge"),
            .displayName = QStringLiteral("圆盘仪表"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("gauge"),
            .bindingRequired = true,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const double minValue =
                    control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
                const double maxValue =
                    control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
                if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
                    if (error) *error = QStringLiteral("圆盘仪表范围无效");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 数码管 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("当前值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("digits"), QStringLiteral("位数"), PropertyGroup::Appearance,
                          PropertyEditorKind::IntegerSpinbox, 6, 1.0, 8.0, 1.0));
        s.append(makeSpec(QStringLiteral("color"), QStringLiteral("数字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#ff3b30"), 0.0, 0.0, 1.0, 0, 7));
        s.append(makeSpec(QStringLiteral("backgroundColor"), QStringLiteral("背景颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#0d0d0d"), 0.0, 0.0, 1.0, 0, 7));
        add({
            .keyword = QStringLiteral("sevenSegment"),
            .definitionId = QStringLiteral("yserein-hmi.seven-segment"),
            .displayName = QStringLiteral("数码管"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("sevenSegment"),
            .bindingRequired = true,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const int digits = control.properties.value(QStringLiteral("digits")).toInt(6);
                if (digits < 1 || digits > 8) {
                    if (error) *error = QStringLiteral("数码管位数必须在 1 到 8 之间");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 滑动条 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"readWrite"}, {"none", "command"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("当前值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("minValue"), QStringLiteral("最小值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("maxValue"), QStringLiteral("最大值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 100.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("step"), QStringLiteral("步进"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 1.0, 0.001, kNoBound, 0.1, 3));
        s.append(makeSpec(QStringLiteral("orientation"), QStringLiteral("方向"), PropertyGroup::Appearance,
                          PropertyEditorKind::ComboBox, QStringLiteral("horizontal"), 0.0, 0.0, 1.0, 0, 0,
                          makeLabeledChoices({{"horizontal", "水平"}, {"vertical", "垂直"}})));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#334155"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("accentColor"), QStringLiteral("滑块颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#3b82f6"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("trackColor"), QStringLiteral("轨道颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#e2e8f0"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("slider"),
            .definitionId = QStringLiteral("yserein-hmi.slider"),
            .displayName = QStringLiteral("滑动条"),
            .category = QStringLiteral("控制组件"),
            .renderKind = QStringLiteral("slider"),
            .bindingRequired = true,
            .interactiveDefault = true,
            .allowedBindingModes = makeModes({"readWrite"}),
            .allowedEventTypes = makeEvents({"none", "command"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const double minValue =
                    control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
                const double maxValue =
                    control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
                const double step =
                    control.properties.value(QStringLiteral("step")).toDouble(1.0);
                if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
                    if (error) *error = QStringLiteral("滑动条范围无效");
                    return false;
                }
                if (!std::isfinite(step) || step <= 0.0) {
                    if (error) *error = QStringLiteral("滑动条步进必须大于 0");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 旋钮 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"readWrite"}, {"none", "command"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("当前值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("minValue"), QStringLiteral("最小值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("maxValue"), QStringLiteral("最大值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 100.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("unit"), QStringLiteral("单位"), PropertyGroup::Appearance,
                          PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 12));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#334155"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("accentColor"), QStringLiteral("旋钮颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#3b82f6"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("trackColor"), QStringLiteral("轨道颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#e2e8f0"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("knob"),
            .definitionId = QStringLiteral("yserein-hmi.knob"),
            .displayName = QStringLiteral("旋钮"),
            .category = QStringLiteral("控制组件"),
            .renderKind = QStringLiteral("knob"),
            .bindingRequired = true,
            .interactiveDefault = true,
            .allowedBindingModes = makeModes({"readWrite"}),
            .allowedEventTypes = makeEvents({"none", "command"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const double minValue =
                    control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
                const double maxValue =
                    control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
                if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
                    if (error) *error = QStringLiteral("旋钮范围无效");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 报警指示 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("报警值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("message"), QStringLiteral("报警文本"), PropertyGroup::Content,
                          PropertyEditorKind::LineEdit, QString(), 0.0, 0.0, 1.0, 0, 64));
        s.append(makeSpec(QStringLiteral("severity"), QStringLiteral("级别"), PropertyGroup::Appearance,
                          PropertyEditorKind::ComboBox, QStringLiteral("warn"), 0.0, 0.0, 1.0, 0, 0,
                          makeLabeledChoices({{"info", "提示"}, {"warn", "警告"}, {"critical", "严重"}})));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#0f172a"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("alarm"),
            .definitionId = QStringLiteral("yserein-hmi.alarm"),
            .displayName = QStringLiteral("报警指示"),
            .category = QStringLiteral("控制组件"),
            .renderKind = QStringLiteral("alarm"),
            .bindingRequired = true,
            .interactiveDefault = true,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const QString severity =
                    control.properties.value(QStringLiteral("severity")).toString(QStringLiteral("warn"));
                if (severity != QStringLiteral("info") && severity != QStringLiteral("warn")
                    && severity != QStringLiteral("critical")) {
                    if (error) *error = QStringLiteral("报警级别无效");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 报警列表 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("maxRows"), QStringLiteral("最大行数"), PropertyGroup::Content,
                          PropertyEditorKind::IntegerSpinbox, 10, 1.0, 50.0, 1.0));
        s.append(makeSpec(QStringLiteral("refreshInterval"), QStringLiteral("刷新间隔(ms)"), PropertyGroup::Appearance,
                          PropertyEditorKind::IntegerSpinbox, 1000, 100.0, 60000.0, 1.0));
        s.append(makeSpec(QStringLiteral("backgroundColor"), QStringLiteral("背景色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#ffffff"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#334155"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("warnColor"), QStringLiteral("告警颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#f5a524"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("borderColor"), QStringLiteral("边框颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#cbd5e1"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("alarmList"),
            .definitionId = QStringLiteral("yserein-hmi.alarm-list"),
            .displayName = QStringLiteral("报警列表"),
            .category = QStringLiteral("数据组件"),
            .renderKind = QStringLiteral("alarmList"),
            .bindingRequired = false,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
        });
    }

    // ---------------------------- 历史趋势 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("minValue"), QStringLiteral("最小值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("maxValue"), QStringLiteral("最大值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 100.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("timeSpan"), QStringLiteral("时间跨度(秒)"), PropertyGroup::Content,
                          PropertyEditorKind::IntegerSpinbox, 60, 1.0, 86400.0, 1.0));
        s.append(makeSpec(QStringLiteral("sampleInterval"), QStringLiteral("采样间隔(秒)"), PropertyGroup::Appearance,
                          PropertyEditorKind::IntegerSpinbox, 1, 1.0, 3600.0, 1.0));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#334155"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("accentColor"), QStringLiteral("曲线颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#3b82f6"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("gridColor"), QStringLiteral("网格颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#e2e8f0"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("trend"),
            .definitionId = QStringLiteral("yserein-hmi.trend"),
            .displayName = QStringLiteral("历史趋势"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("trend"),
            .bindingRequired = true,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const double minValue =
                    control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
                const double maxValue =
                    control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
                if (!std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) {
                    if (error) *error = QStringLiteral("历史趋势范围无效");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 数据表格 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("rows"), QStringLiteral("行数"), PropertyGroup::Content,
                          PropertyEditorKind::IntegerSpinbox, 5, 1.0, 64.0, 1.0));
        s.append(makeSpec(QStringLiteral("columns"), QStringLiteral("列数"), PropertyGroup::Content,
                          PropertyEditorKind::IntegerSpinbox, 4, 1.0, 16.0, 1.0));
        s.append(makeSpec(QStringLiteral("headerVisible"), QStringLiteral("显示表头"), PropertyGroup::Appearance,
                          PropertyEditorKind::CheckBox, true));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#334155"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("accentColor"), QStringLiteral("表头颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#2563eb"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("backgroundColor"), QStringLiteral("单元格颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#ffffff"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("gridColor"), QStringLiteral("网格颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#e2e8f0"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("table"),
            .definitionId = QStringLiteral("yserein-hmi.data-table"),
            .displayName = QStringLiteral("数据表格"),
            .category = QStringLiteral("数据组件"),
            .renderKind = QStringLiteral("table"),
            .bindingRequired = false,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
        });
    }

    // ---------------------------- 时间显示 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"none"}, {"none"});
        s.append(makeSpec(QStringLiteral("format24"), QStringLiteral("24 小时制"), PropertyGroup::Appearance,
                          PropertyEditorKind::CheckBox, true));
        s.append(makeSpec(QStringLiteral("fontPixelSize"), QStringLiteral("字号"), PropertyGroup::Appearance,
                          PropertyEditorKind::IntegerSpinbox, 32, 12.0, 72.0, 1.0));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#0f172a"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("timeDisplay"),
            .definitionId = QStringLiteral("yserein-hmi.time-display"),
            .displayName = QStringLiteral("时间显示"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("time"),
            .bindingRequired = false,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"none"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
        });
    }

    // ---------------------------- 日期显示 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"none"}, {"none"});
        s.append(makeSpec(QStringLiteral("format"), QStringLiteral("日期格式"), PropertyGroup::Content,
                          PropertyEditorKind::LineEdit, QStringLiteral("yyyy-MM-dd"), 0.0, 0.0, 1.0, 0, 16));
        s.append(makeSpec(QStringLiteral("fontPixelSize"), QStringLiteral("字号"), PropertyGroup::Appearance,
                          PropertyEditorKind::IntegerSpinbox, 32, 12.0, 72.0, 1.0));
        s.append(makeSpec(QStringLiteral("textColor"), QStringLiteral("文字颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#0f172a"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("dateDisplay"),
            .definitionId = QStringLiteral("yserein-hmi.date-display"),
            .displayName = QStringLiteral("日期显示"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("date"),
            .bindingRequired = false,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"none"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
        });
    }

    // ---------------------------- 多态指示灯 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"read"}, {"none"});
        s.append(makeSpec(QStringLiteral("value"), QStringLiteral("状态值"), PropertyGroup::Content,
                          PropertyEditorKind::DoubleSpinbox, 0.0, -kNoBound, kNoBound, 0.1, 2));
        s.append(makeSpec(QStringLiteral("states"), QStringLiteral("状态数"), PropertyGroup::Appearance,
                          PropertyEditorKind::IntegerSpinbox, 3, 2.0, 16.0, 1.0));
        s.append(makeSpec(QStringLiteral("activeState"), QStringLiteral("当前状态"), PropertyGroup::Appearance,
                          PropertyEditorKind::IntegerSpinbox, 0, 0.0, 15.0, 1.0));
        s.append(makeSpec(QStringLiteral("activeColor"), QStringLiteral("点亮颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#22c55e"), 0.0, 0.0, 1.0, 0, 9));
        s.append(makeSpec(QStringLiteral("inactiveColor"), QStringLiteral("熄灭颜色"), PropertyGroup::Appearance,
                          PropertyEditorKind::Color, QStringLiteral("#cbd5e1"), 0.0, 0.0, 1.0, 0, 9));
        add({
            .keyword = QStringLiteral("multiLamp"),
            .definitionId = QStringLiteral("yserein-hmi.multi-lamp"),
            .displayName = QStringLiteral("多态指示灯"),
            .category = QStringLiteral("显示组件"),
            .renderKind = QStringLiteral("multiLamp"),
            .bindingRequired = true,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"read"}),
            .allowedEventTypes = makeEvents({"none"}),
            .specs = s,
            .validateProperties = [](const ControlModel &control, QString *error) {
                const int states = control.properties.value(QStringLiteral("states")).toInt(3);
                if (states < 2 || states > 16) {
                    if (error) *error = QStringLiteral("多态指示灯状态数必须在 2 到 16 之间");
                    return false;
                }
                const int activeState =
                    control.properties.value(QStringLiteral("activeState")).toInt(0);
                if (activeState < 0 || activeState >= states) {
                    if (error) *error = QStringLiteral("多态指示灯当前状态无效");
                    return false;
                }
                return true;
            },
        });
    }

    // ---------------------------- 未知/未来 provider 的回退 ----------------------------
    {
        QVector<PropertySpec> s = baseSpecs({"none", "read", "write", "readWrite"},
                                            {"none", "gotoPage", "command", "submit"});
        add({
            .keyword = QStringLiteral("opaque"),
            .definitionId = QString(),
            .displayName = QStringLiteral("未知类型"),
            .category = QStringLiteral("未识别"),
            .renderKind = QStringLiteral("opaque"),
            .bindingRequired = false,
            .interactiveDefault = false,
            .allowedBindingModes = makeModes({"none", "read", "write", "readWrite"}),
            .allowedEventTypes = makeEvents({"none", "gotoPage", "command", "submit"}),
            .specs = s,
        });
    }
}

const ControlSchema *ControlSchemaRegistry::findByKeyword(const QString &keyword) const noexcept
{
    const auto it = m_byKeyword.constFind(keyword);
    if (it == m_byKeyword.constEnd()) return nullptr;
    return &m_schemas.at(it.value());
}

const ControlSchema *ControlSchemaRegistry::findByDefinitionId(
    const QString &definitionId) const noexcept
{
    const auto it = m_byDefinitionId.constFind(definitionId);
    if (it == m_byDefinitionId.constEnd()) return nullptr;
    return &m_schemas.at(it.value());
}

QString ControlSchemaRegistry::keywordForDefinitionId(const QString &definitionId) const noexcept
{
    if (const ControlSchema *schema = findByDefinitionId(definitionId)) {
        return schema->keyword;
    }
    return QStringLiteral("opaque");
}

const ControlSchema *ControlSchemaRegistry::fallback() const noexcept
{
    for (const ControlSchema &schema : m_schemas) {
        if (schema.keyword == QStringLiteral("opaque")) return &schema;
    }
    return nullptr;
}

std::vector<const ControlSchema *> ControlSchemaRegistry::authorable() const noexcept
{
    std::vector<const ControlSchema *> result;
    result.reserve(m_schemas.size());
    for (const ControlSchema &schema : m_schemas) {
        if (schema.keyword == QStringLiteral("opaque")) continue;
        result.push_back(&schema);
    }
    return result;
}

} // namespace serein::scope::yserein_hmi
