#include "DesignerCanvas.h"

#include "ControlSchema.h"

#include <QtCore/QSizeF>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QMimeData>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QUuid>
#include <QtCore/QTime>
#include <QtCore/QDate>
#include <QtGui/QFont>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPaintEvent>
#include <QtGui/QPen>
#include <QtGui/QImage>
#include <QtGui/QKeyEvent>
#include <QtGui/QCursor>
#include <QtWidgets/QInputDialog>

#include <algorithm>
#include <cmath>
#include <utility>

namespace serein::scope::yserein_hmi {
namespace {

constexpr QSizeF DeviceSize(800.0, 480.0);
constexpr double SnapGrid = 8.0;
constexpr double MrPi = 3.14159265358979323846;
constexpr double HandleSize = 8.0;
constexpr double HandleHitRadius = 7.0;

QColor blend(const QColor &color, const QColor &background, const double amount)
{
    const double inverse = 1.0 - amount;
    return QColor::fromRgbF(color.redF() * amount + background.redF() * inverse,
                            color.greenF() * amount + background.greenF() * inverse,
                            color.blueF() * amount + background.blueF() * inverse,
                            color.alphaF() * amount + background.alphaF() * inverse);
}

// Draws a dense, smooth spline through sampled points (Catmull-Rom), so a
// chart/trend trace reads as a real resolution-mapped waveform rather than a
// few coarse line segments.
void appendSmoothPath(QPainterPath &path, const QVector<QPointF> &points)
{
    if (points.isEmpty()) return;
    path.moveTo(points.constFirst());
    if (points.size() == 1) return;
    for (int i = 0; i < points.size() - 1; ++i) {
        const QPointF p0 = points.at(i == 0 ? 0 : i - 1);
        const QPointF p1 = points.at(i);
        const QPointF p2 = points.at(i + 1);
        const QPointF p3 = points.at(std::min<qsizetype>(i + 2, points.size() - 1));
        path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
    }
}

// Authoring-time default footprint for a control type, in native device pixels.
QSizeF defaultControlSize(const QString &type)
{
    if (type == QStringLiteral("image")) return QSizeF(160.0, 96.0);
    if (type == QStringLiteral("chart")) return QSizeF(300.0, 160.0);
    if (type == QStringLiteral("text-input")) return QSizeF(220.0, 48.0);
    if (type == QStringLiteral("gauge")) return QSizeF(200.0, 140.0);
    if (type == QStringLiteral("knob")) return QSizeF(140.0, 140.0);
    if (type == QStringLiteral("slider")) return QSizeF(220.0, 48.0);
    if (type == QStringLiteral("trend")) return QSizeF(300.0, 160.0);
    if (type == QStringLiteral("table")) return QSizeF(320.0, 160.0);
    if (type == QStringLiteral("alarm")) return QSizeF(260.0, 48.0);
    if (type == QStringLiteral("alarmList")) return QSizeF(280.0, 120.0);
    if (type == QStringLiteral("multiLamp")) return QSizeF(220.0, 48.0);
    return QSizeF(150.0, 48.0);
}

// Maps the canvas-side type keyword (which keeps the legacy "text-input"
// spelling for the input control) to the canonical schema keyword.
QString canonicalKeyword(const QString &type)
{
    if (type == QStringLiteral("text-input")) return QStringLiteral("textInput");
    return type;
}

QString controlLabel(const QString &type)
{
    if (const ControlSchema *schema =
            ControlSchemaRegistry::instance().findByKeyword(canonicalKeyword(type))) {
        return schema->displayName;
    }
    return {};
}

bool defaultInteractiveForType(const QString &type)
{
    if (const ControlSchema *schema =
            ControlSchemaRegistry::instance().findByKeyword(canonicalKeyword(type))) {
        return schema->interactiveDefault;
    }
    return false;
}

QString stableId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString normalizedPageName(const QString &name)
{
    return name.trimmed().left(32);
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

QString definitionIdForType(const QString &type)
{
    if (const ControlSchema *schema =
            ControlSchemaRegistry::instance().findByKeyword(canonicalKeyword(type))) {
        return schema->definitionId;
    }
    return {};
}

QString typeForDefinitionId(const QString &definitionId)
{
    if (const ControlSchema *schema =
            ControlSchemaRegistry::instance().findByDefinitionId(definitionId)) {
        return schema->keyword == QStringLiteral("textInput")
            ? QStringLiteral("text-input")
            : schema->keyword;
    }
    return {};
}

QString controlPrefix(const QString &type)
{
    if (type == QStringLiteral("text-input")) return QStringLiteral("input");
    if (const ControlSchema *schema =
            ControlSchemaRegistry::instance().findByKeyword(canonicalKeyword(type))) {
        return schema->keyword;
    }
    return QStringLiteral("control");
}

QString defaultBindingModeForType(const QString &type)
{
    if (const ControlSchema *schema =
            ControlSchemaRegistry::instance().findByKeyword(canonicalKeyword(type))) {
        return schema->defaultProperties.value(QStringLiteral("bindingMode")).toString();
    }
    return QStringLiteral("read");
}

} // namespace

DesignerCanvas::DesignerCanvas(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("hmiDesignerCanvas"));
    setMinimumSize(520, 360);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    m_deviceSize = DeviceSize;
    m_pageIds = QStringList{stableId(), stableId(), stableId()};
    m_pageNames = QStringList{tr("首页"), tr("参数"), tr("告警")};
    rebuildPage(0);
}

QString DesignerCanvas::selectedId() const
{
    if (const NumericDisplayControl *numeric = selectedNumericControl()) {
        return numeric->state().id;
    }
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).id : QString{};
}

QString DesignerCanvas::selectedType() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).type : QString{};
}

QString DesignerCanvas::selectedBinding() const
{
    if (const NumericDisplayControl *numeric = selectedNumericControl()) {
        return numeric->state().binding;
    }
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).binding : QString{};
}

QString DesignerCanvas::selectedTextValue() const
{
    if (const NumericDisplayControl *numeric = selectedNumericControl()) {
        return numeric->formattedValue();
    }
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).textValue : QString{};
}

double DesignerCanvas::selectedControlValue() const
{
    if (const NumericDisplayControl *numeric = selectedNumericControl()) {
        return numeric->state().previewValue;
    }
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).value : 0.0;
}

QRectF DesignerCanvas::selectedGeometry() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).rect : QRectF{};
}

QString DesignerCanvas::selectedBindingMode() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).bindingMode : QString{};
}

QString DesignerCanvas::selectedEventType() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).eventType : QString{};
}

QString DesignerCanvas::selectedEventTarget() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).eventTarget : QString{};
}

bool DesignerCanvas::selectedVisible() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).visible : false;
}

bool DesignerCanvas::selectedLocked() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).locked : false;
}

QString DesignerCanvas::selectedAssetId() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).assetId : QString{};
}

QString DesignerCanvas::selectedScaleMode() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).scaleMode : QString{};
}

int DesignerCanvas::selectedInputMaxLength() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).inputMaxLength : 32;
}

double DesignerCanvas::selectedChartMin() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).chartMin : 0.0;
}

double DesignerCanvas::selectedChartMax() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).chartMax : 100.0;
}

int DesignerCanvas::selectedChartWindowLength() const
{
    return m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        ? m_controls.at(m_selectedIndex).chartWindowLength : 30;
}

QJsonArray DesignerCanvas::selectedChartSamples() const
{
    QJsonArray result;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return result;
    for (const double sample : m_controls.at(m_selectedIndex).chartSamples) result.append(sample);
    return result;
}

bool DesignerCanvas::gridVisible() const
{
    return m_gridVisible;
}

bool DesignerCanvas::previewMode() const
{
    return m_previewMode;
}

int DesignerCanvas::pageCount() const noexcept
{
    return m_pageNames.size();
}

QStringList DesignerCanvas::pageIds() const
{
    return m_pageIds;
}

QStringList DesignerCanvas::pageNames() const
{
    return m_pageNames;
}

int DesignerCanvas::currentPage() const noexcept
{
    return m_pageIndex;
}

QJsonObject DesignerCanvas::pageState() const
{
    QVector<QVector<Control>> pages = m_pageControls;
    if (m_pageIndex >= 0 && m_pageIndex < pages.size()) {
        pages[m_pageIndex] = m_controls;
    }

    QJsonArray serializedPages;
    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        QJsonArray serializedControls;
        for (const Control &control : pages.at(pageIndex)) {
            const QJsonObject geometry{
                {QStringLiteral("x"), control.rect.x()},
                {QStringLiteral("y"), control.rect.y()},
                {QStringLiteral("width"), control.rect.width()},
                {QStringLiteral("height"), control.rect.height()},
            };
            QJsonObject properties = control.properties;
            // Typed rendering fields stay authoritative for the controls the
            // canvas draws, while the schema map preserves keys it does not
            // know about (e.g. gauge thresholds, trend intervals).
            properties.insert(QStringLiteral("binding"), control.numeric
                     ? control.numeric->state().binding : control.binding);
            properties.insert(QStringLiteral("text"), control.textValue);
            if (control.type == QStringLiteral("text-input")) {
                properties.insert(QStringLiteral("value"), control.textValue);
            } else if (!control.numeric) {
                properties.insert(QStringLiteral("value"), control.value);
            }
            properties.insert(QStringLiteral("interactive"), control.interactive);
            properties.insert(QStringLiteral("bindingMode"), control.bindingMode);
            properties.insert(QStringLiteral("eventType"), control.eventType);
            properties.insert(QStringLiteral("eventTarget"), control.eventTarget);
            properties.insert(QStringLiteral("visible"), control.visible);
            properties.insert(QStringLiteral("locked"), control.locked);
            if (control.numeric) {
                const NumericDisplayControl::State &numeric = control.numeric->state();
                properties.insert(QStringLiteral("previewValue"), numeric.previewValue);
                properties.insert(QStringLiteral("decimals"), numeric.decimals);
                properties.insert(QStringLiteral("unit"), numeric.unit);
                properties.insert(QStringLiteral("fontPixelSize"), numeric.fontPixelSize);
                properties.insert(QStringLiteral("alignment"), numeric.alignmentIndex);
            }
            if (control.type == QStringLiteral("image")) {
                properties.insert(QStringLiteral("assetId"), control.assetId);
                properties.insert(QStringLiteral("scaleMode"), control.scaleMode);
            }
            if (control.type == QStringLiteral("chart")) {
                QJsonArray samples;
                for (const double sample : control.chartSamples) samples.append(sample);
                properties.insert(QStringLiteral("samples"), samples);
                properties.insert(QStringLiteral("minValue"), control.chartMin);
                properties.insert(QStringLiteral("maxValue"), control.chartMax);
                properties.insert(QStringLiteral("windowLength"), control.chartWindowLength);
            }
            if (control.type == QStringLiteral("text-input")) {
                properties.insert(QStringLiteral("maxLength"), control.inputMaxLength);
                properties.insert(QStringLiteral("placeholder"), control.label);
            }
            serializedControls.append(QJsonObject{
                {QStringLiteral("instanceId"), control.instanceId},
                {QStringLiteral("definitionId"), definitionIdForType(control.type)},
                {QStringLiteral("name"), control.numeric
                     ? control.numeric->state().id : control.id},
                {QStringLiteral("label"), control.label},
                {QStringLiteral("geometry"), geometry},
                {QStringLiteral("properties"), properties},
            });
        }
        serializedPages.append(QJsonObject{
            {QStringLiteral("pageId"), m_pageIds.value(pageIndex)},
            {QStringLiteral("name"), m_pageNames.value(pageIndex)},
            {QStringLiteral("controls"), serializedControls},
        });
    }
    return {
        {QStringLiteral("currentPage"), m_pageIndex},
        {QStringLiteral("pages"), serializedPages},
        {QStringLiteral("background"), m_background.toJson()},
        {QStringLiteral("device"), QJsonObject{
            {QStringLiteral("width"), m_deviceSize.width()},
            {QStringLiteral("height"), m_deviceSize.height()},
            {QStringLiteral("rotation"), m_rotation},
        }},
    };
}

bool DesignerCanvas::restorePageState(const QJsonObject &state, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    const QJsonObject deviceObject = state.value(QStringLiteral("device")).toObject();
    const double deviceWidth = deviceObject.value(QStringLiteral("width"))
                                   .toDouble(DeviceSize.width());
    const double deviceHeight = deviceObject.value(QStringLiteral("height"))
                                    .toDouble(DeviceSize.height());
    if (deviceWidth >= 1.0 && deviceHeight >= 1.0) {
        m_deviceSize = QSizeF(deviceWidth, deviceHeight);
    }
    const int deviceRotation = deviceObject.value(QStringLiteral("rotation")).toInt(0);
    m_rotation = ((deviceRotation % 360) + 360) % 360;
    m_background = PageBackground::fromJson(state.value(QStringLiteral("background")).toObject());
    const QJsonValue pagesValue = state.value(QStringLiteral("pages"));
    const QJsonArray serializedPages = pagesValue.toArray();
    if (!pagesValue.isArray() || serializedPages.isEmpty()) {
        return fail(tr("工程至少需要一个页面"));
    }

    QStringList pageIds;
    QStringList pageNames;
    QVector<QVector<Control>> pages;
    QSet<QString> uniquePageIds;
    // Instance IDs are stable project identities, not page-local labels. Keep
    // one set for the whole document so a save cannot later fail after the
    // canvas accepted a duplicate on another page.
    QSet<QString> uniqueInstanceIds;
    for (const QJsonValue &pageValue : serializedPages) {
        if (!pageValue.isObject()) return fail(tr("页面记录格式无效"));
        const QJsonObject pageObject = pageValue.toObject();
        const QString pageId = pageObject.value(QStringLiteral("pageId"))
                                   .toString().trimmed();
        const QString pageName = normalizedPageName(
            pageObject.value(QStringLiteral("name")).toString());
        if (pageId.isEmpty() || uniquePageIds.contains(pageId)) {
            return fail(tr("页面 ID 缺失或重复"));
        }
        if (pageName.isEmpty() || pageNames.contains(pageName, Qt::CaseInsensitive)) {
            return fail(tr("页面名称缺失或重复"));
        }
        const QJsonValue controlsValue = pageObject.value(QStringLiteral("controls"));
        if (!controlsValue.isArray()) return fail(tr("页面控件列表格式无效"));

        QVector<Control> controls;
        QSet<QString> instanceIds;
        QSet<QString> names;
        for (const QJsonValue &controlValue : controlsValue.toArray()) {
            if (!controlValue.isObject()) return fail(tr("控件记录格式无效"));
            const QJsonObject controlObject = controlValue.toObject();
            const QString definitionId = controlObject
                .value(QStringLiteral("definitionId")).toString();
            const QString type = typeForDefinitionId(definitionId);
            if (type.isEmpty()) {
                return fail(tr("当前版本无法加载控件定义：%1").arg(definitionId));
            }
            const QString instanceId = controlObject
                .value(QStringLiteral("instanceId")).toString().trimmed();
            if (instanceId.isEmpty() || instanceIds.contains(instanceId)
                || uniqueInstanceIds.contains(instanceId)) {
                return fail(tr("控件实例 ID 缺失或重复"));
            }
            const QJsonObject geometry = controlObject
                .value(QStringLiteral("geometry")).toObject();
            const QRectF rect(geometry.value(QStringLiteral("x")).toDouble(),
                              geometry.value(QStringLiteral("y")).toDouble(),
                              geometry.value(QStringLiteral("width")).toDouble(),
                              geometry.value(QStringLiteral("height")).toDouble());
            if (!rect.isValid() || rect.left() < 0.0 || rect.top() < 0.0
                || rect.right() > m_deviceSize.width()
                || rect.bottom() > m_deviceSize.height()) {
                return fail(tr("控件几何超出 %1 × %2 设计画布")
                                .arg(m_deviceSize.width())
                                .arg(m_deviceSize.height()));
            }

            const QJsonValue propertiesValue = controlObject
                .value(QStringLiteral("properties"));
            if (!propertiesValue.isObject()) {
                return fail(tr("控件属性记录格式无效"));
            }
            const QJsonObject properties = propertiesValue.toObject();
            if ((properties.contains(QStringLiteral("bindingMode"))
                 && !properties.value(QStringLiteral("bindingMode")).isString())
                || (properties.contains(QStringLiteral("eventType"))
                    && !properties.value(QStringLiteral("eventType")).isString())
                || (properties.contains(QStringLiteral("eventTarget"))
                    && !properties.value(QStringLiteral("eventTarget")).isString())
                || (properties.contains(QStringLiteral("visible"))
                    && !properties.value(QStringLiteral("visible")).isBool())
                || (properties.contains(QStringLiteral("locked"))
                    && !properties.value(QStringLiteral("locked")).isBool())) {
                return fail(tr("控件属性类型无效"));
            }
            const QString name = controlObject.value(QStringLiteral("name"))
                                     .toString().trimmed();
            const QString binding = properties.value(QStringLiteral("binding"))
                                        .toString().trimmed();
            QJsonValue serializedValue = properties.value(QStringLiteral("value"));
            if (type == QStringLiteral("number")) {
                // For numeric-display controls the schema distinguishes the
                // binding value ("value") from the simulated preview
                // ("previewValue"); the canvas renders the preview.
                const QJsonValue preview = properties.value(QStringLiteral("previewValue"));
                if (preview.isDouble()) serializedValue = preview;
            }
            if (type != QStringLiteral("text-input")
                && !serializedValue.isUndefined()
                && (!serializedValue.isDouble()
                    || !std::isfinite(serializedValue.toDouble()))) {
                return fail(tr("控件模拟值无效"));
            }
            Control control{
                .instanceId = instanceId,
                .id = name,
                .type = type,
                .label = controlObject.value(QStringLiteral("label")).toString(),
                .binding = binding,
                .bindingMode = properties.value(QStringLiteral("bindingMode"))
                    .toString().trimmed().isEmpty()
                    ? defaultBindingModeForType(type)
                    : properties.value(QStringLiteral("bindingMode"))
                        .toString().trimmed(),
                .eventType = properties.value(QStringLiteral("eventType"))
                                 .toString().trimmed(),
                .eventTarget = properties.value(QStringLiteral("eventTarget"))
                                   .toString().trimmed(),
                .textValue = properties.value(QStringLiteral("text")).toString(),
                .value = serializedValue.isDouble() ? serializedValue.toDouble() : 0.0,
                .rect = rect,
                .fill = QColor(QStringLiteral("#203346")),
                .text = QColor(QStringLiteral("#f2c96b")),
                .interactive = properties.contains(QStringLiteral("interactive"))
                    ? properties.value(QStringLiteral("interactive")).toBool()
                    : defaultInteractiveForType(type),
                .visible = !properties.contains(QStringLiteral("visible"))
                    || properties.value(QStringLiteral("visible")).toBool(),
                .locked = properties.value(QStringLiteral("locked")).toBool(),
                .assetId = properties.value(QStringLiteral("assetId")).toString().trimmed(),
                .scaleMode = properties.value(QStringLiteral("scaleMode"))
                                 .toString(QStringLiteral("fit")),
                .inputMaxLength = properties.value(QStringLiteral("maxLength")).toInt(32),
                .chartMin = properties.value(QStringLiteral("minValue")).toDouble(0.0),
                .chartMax = properties.value(QStringLiteral("maxValue")).toDouble(100.0),
                .chartWindowLength = properties.value(QStringLiteral("windowLength")).toInt(30),
                .chartSamples = {},
                .properties = properties,
            };
            if (type == QStringLiteral("chart")) {
                const QJsonValue samplesValue = properties.value(QStringLiteral("samples"));
                const QJsonArray samples = samplesValue.toArray();
                if (!samplesValue.isArray() || samples.isEmpty()
                    || !std::isfinite(control.chartMin)
                    || !std::isfinite(control.chartMax)
                    || control.chartMax <= control.chartMin
                    || control.chartWindowLength < 2
                    || control.chartWindowLength > 4096) {
                    return fail(tr("曲线范围、窗口或采样数据无效"));
                }
                for (const QJsonValue &sample : samples) {
                    if (!sample.isDouble() || !std::isfinite(sample.toDouble())) {
                        return fail(tr("曲线采样点无效"));
                    }
                    control.chartSamples.append(sample.toDouble());
                }
                if (control.chartSamples.size() > control.chartWindowLength) {
                    control.chartSamples = control.chartSamples.sliced(
                        control.chartSamples.size() - control.chartWindowLength);
                }
            } else if (type == QStringLiteral("image")) {
                if (control.assetId.isEmpty()) return fail(tr("图片控件缺少资源 ID"));
                if (control.scaleMode != QStringLiteral("fit")
                    && control.scaleMode != QStringLiteral("fill")
                    && control.scaleMode != QStringLiteral("stretch")
                    && control.scaleMode != QStringLiteral("none")) {
                    return fail(tr("图片缩放模式无效"));
                }
            } else if (type == QStringLiteral("text-input")) {
                if (control.inputMaxLength < 1 || control.inputMaxLength > 256) {
                    return fail(tr("文本输入长度限制无效"));
                }
                control.textValue = control.textValue.left(control.inputMaxLength);
            }
            if (control.bindingMode != QStringLiteral("none")
                && control.bindingMode != QStringLiteral("read")
                && control.bindingMode != QStringLiteral("write")
                && control.bindingMode != QStringLiteral("readWrite")) {
                return fail(tr("控件绑定方向无效"));
            }
            if (control.eventType.isEmpty()) control.eventType = QStringLiteral("none");
            if (control.eventType != QStringLiteral("none")
                && control.eventType != QStringLiteral("gotoPage")
                && control.eventType != QStringLiteral("command")
                && control.eventType != QStringLiteral("submit")) {
                return fail(tr("控件事件类型无效"));
            }
            if (const ControlSchema *schema =
                    ControlSchemaRegistry::instance().findByKeyword(canonicalKeyword(type))) {
                if (!schema->allowsBindingMode(control.bindingMode))
                    return fail(tr("控件绑定方向与类型不匹配"));
                if (!schema->allowsEventType(control.eventType))
                    return fail(tr("当前控件类型不支持交互事件"));
            }
            if (!validIdentifier(name) || names.contains(name)) {
                return fail(tr("控件名称无效或重复"));
            }
            if (!binding.isEmpty() && !validBinding(binding)) {
                return fail(tr("控件数据绑定无效"));
            }
            if (type == QStringLiteral("number")) {
                NumericDisplayControl numeric;
                if (!numeric.setId(name) || !numeric.setBinding(binding)
                    || !numeric.setPreviewValue(serializedValue.toDouble())) {
                    return fail(tr("数值显示的名称、绑定或模拟值无效"));
                }
                numeric.setDecimals(properties.value(QStringLiteral("decimals")).toInt(1));
                numeric.setUnit(properties.value(QStringLiteral("unit")).toString());
                numeric.setFontPixelSize(properties.value(QStringLiteral("fontPixelSize"))
                                             .toInt(32));
                numeric.setAlignmentIndex(properties.value(QStringLiteral("alignment"))
                                              .toInt(0));
                control.binding = numeric.state().binding;
                control.value = numeric.state().previewValue;
                control.numeric = std::move(numeric);
            } else if (type == QStringLiteral("progress")
                       || type == QStringLiteral("status")
                       || type == QStringLiteral("switch")) {
                control.value = std::clamp(control.value, 0.0, 1.0);
            } else if (control.textValue.isEmpty()) {
                control.textValue = control.label;
            }
            if (control.label.isEmpty()) control.label = controlLabel(control.type);
            controls.append(std::move(control));
            instanceIds.insert(instanceId);
            uniqueInstanceIds.insert(instanceId);
            names.insert(name);
        }
        uniquePageIds.insert(pageId);
        pageIds.append(pageId);
        pageNames.append(pageName);
        pages.append(std::move(controls));
    }

    const QSet<QString> knownPageIds = uniquePageIds;
    for (const QVector<Control> &pageControls : pages) {
        for (const Control &control : pageControls) {
            if (control.eventType == QStringLiteral("gotoPage")) {
                if (control.eventTarget.isEmpty() || !knownPageIds.contains(control.eventTarget)) {
                    return fail(tr("控件事件引用了不存在的目标页面"));
                }
            } else if (control.eventType == QStringLiteral("command")
                       && control.eventTarget.trimmed().isEmpty()) {
                return fail(tr("命令事件不能为空"));
            }
        }
    }

    m_pageIds = std::move(pageIds);
    m_pageNames = std::move(pageNames);
    m_pageControls = std::move(pages);
    const int lastPage = static_cast<int>(m_pageNames.size()) - 1;
    m_pageIndex = std::clamp(state.value(QStringLiteral("currentPage")).toInt(),
                             0, lastPage);
    m_controls = m_pageControls.at(m_pageIndex);
    m_selectedIndex = m_controls.isEmpty() ? -1 : 0;
    m_nextControlNumber = 1;
    for (const QVector<Control> &page : std::as_const(m_pageControls)) {
        m_nextControlNumber += page.size();
    }
    m_dragging = false;
    m_previewMode = false;
    m_previewSnapshot.clear();
    emit pageChanged(m_pageIndex);
    emitSelection();
    update();
    return true;
}

void DesignerCanvas::setGridVisible(const bool visible)
{
    if (m_gridVisible == visible) return;
    m_gridVisible = visible;
    update();
}

void DesignerCanvas::setAssetImages(const QHash<QString, QImage> &images)
{
    m_assetImages = images;
    update();
}

void DesignerCanvas::setPreviewMode(const bool preview)
{
    if (m_previewMode == preview) return;
    if (preview) {
        // Commit the currently edited page before taking the snapshot. Preview
        // interactions are transient and must never turn a click into a dirty
        // project or overwrite authored values.
        if (m_pageIndex >= 0 && m_pageIndex < m_pageControls.size()) {
            m_pageControls[m_pageIndex] = m_controls;
        }
        m_previewSnapshot = m_pageControls;
    }
    m_previewMode = preview;
    m_dragging = false;
    if (m_previewMode) {
        clearFocus();
    } else if (!m_previewSnapshot.isEmpty()) {
        m_pageControls = std::move(m_previewSnapshot);
        const int lastPage = static_cast<int>(m_pageControls.size()) - 1;
        m_pageIndex = std::clamp(m_pageIndex, 0, lastPage);
        m_controls = m_pageControls.at(m_pageIndex);
        m_selectedIndex = m_controls.isEmpty() ? -1 : 0;
        m_previewSnapshot.clear();
        emitSelection();
    }
    update();
    emit statusMessage(m_previewMode ? tr("预览模式：编辑辅助线已隐藏")
                                     : tr("设计模式：可以选中并调整控件"));
}

void DesignerCanvas::setPage(const int pageIndex)
{
    if (m_pageNames.isEmpty()) return;
    const int lastPage = static_cast<int>(m_pageNames.size()) - 1;
    const int bounded = std::clamp(pageIndex, 0, lastPage);
    if (m_pageIndex == bounded) return;
    rebuildPage(bounded);
    emit pageChanged(m_pageIndex);
    emit statusMessage(tr("已切换到页面 %1").arg(m_pageIndex + 1));
}

bool DesignerCanvas::addPage(const QString &name)
{
    if (!ensureEditMode()) return false;
    const QString candidate = normalizedPageName(name);
    if (candidate.isEmpty() || m_pageNames.contains(candidate, Qt::CaseInsensitive)) {
        emit statusMessage(tr("页面名称不能为空或重复"));
        return false;
    }
    if (m_pageIndex >= 0 && m_pageIndex < m_pageControls.size()) {
        m_pageControls[m_pageIndex] = m_controls;
    }
    m_pageIds.append(stableId());
    m_pageNames.append(candidate);
    m_pageControls.emplaceBack();
    rebuildPage(m_pageNames.size() - 1);
    emit pageChanged(m_pageIndex);
    emit documentChanged();
    emit statusMessage(tr("已新建页面 %1").arg(candidate));
    return true;
}

bool DesignerCanvas::duplicatePage(const int pageIndex, const QString &name)
{
    if (!ensureEditMode()) return false;
    if (pageIndex < 0 || pageIndex >= m_pageNames.size()) return false;
    const QString candidate = normalizedPageName(name);
    if (candidate.isEmpty() || m_pageNames.contains(candidate, Qt::CaseInsensitive)) {
        emit statusMessage(tr("页面名称不能为空或重复"));
        return false;
    }
    if (m_pageIndex >= 0 && m_pageIndex < m_pageControls.size()) {
        m_pageControls[m_pageIndex] = m_controls;
    }
    QVector<Control> copy = m_pageControls.at(pageIndex);
    const QString sourcePageId = m_pageIds.at(pageIndex);
    const QString copiedPageId = stableId();
    for (Control &control : copy) {
        control.instanceId = stableId();
        if (control.eventType == QStringLiteral("gotoPage")
            && control.eventTarget == sourcePageId) {
            control.eventTarget = copiedPageId;
        }
    }
    const int insertIndex = pageIndex + 1;
    m_pageIds.insert(insertIndex, copiedPageId);
    m_pageNames.insert(insertIndex, candidate);
    m_pageControls.insert(insertIndex, std::move(copy));
    rebuildPage(insertIndex);
    emit pageChanged(m_pageIndex);
    emit documentChanged();
    emit statusMessage(tr("已复制页面为 %1").arg(candidate));
    return true;
}

bool DesignerCanvas::renamePage(const int pageIndex, const QString &name)
{
    if (!ensureEditMode()) return false;
    if (pageIndex < 0 || pageIndex >= m_pageNames.size()) return false;
    const QString candidate = normalizedPageName(name);
    for (int index = 0; index < m_pageNames.size(); ++index) {
        if (index != pageIndex
            && m_pageNames.at(index).compare(candidate, Qt::CaseInsensitive) == 0) {
            emit statusMessage(tr("页面名称不能为空或重复"));
            return false;
        }
    }
    if (candidate.isEmpty()) {
        emit statusMessage(tr("页面名称不能为空或重复"));
        return false;
    }
    if (m_pageNames.at(pageIndex) == candidate) return true;
    m_pageNames[pageIndex] = candidate;
    emit documentChanged();
    emit statusMessage(tr("页面已重命名为 %1").arg(candidate));
    update();
    return true;
}

bool DesignerCanvas::removePage(const int pageIndex)
{
    if (!ensureEditMode()) return false;
    if (pageIndex < 0 || pageIndex >= m_pageNames.size()) return false;
    if (m_pageNames.size() <= 1) {
        emit statusMessage(tr("工程至少需要保留一个页面"));
        return false;
    }
    if (m_pageIndex >= 0 && m_pageIndex < m_pageControls.size()) {
        m_pageControls[m_pageIndex] = m_controls;
    }
    const QString targetPageId = m_pageIds.at(pageIndex);
    for (int sourcePage = 0; sourcePage < m_pageControls.size(); ++sourcePage) {
        if (sourcePage == pageIndex) continue;
        for (const Control &control : std::as_const(m_pageControls[sourcePage])) {
            if (control.eventType == QStringLiteral("gotoPage")
                && control.eventTarget == targetPageId) {
                emit statusMessage(tr("无法删除：控件 %1 仍跳转到页面 %2")
                                       .arg(control.id, m_pageNames.at(pageIndex)));
                return false;
            }
        }
    }
    const QString removedName = m_pageNames.at(pageIndex);
    m_pageIds.removeAt(pageIndex);
    m_pageNames.removeAt(pageIndex);
    m_pageControls.removeAt(pageIndex);
    const int lastPage = static_cast<int>(m_pageNames.size()) - 1;
    const int nextIndex = std::clamp(pageIndex, 0, lastPage);
    rebuildPage(nextIndex);
    emit pageChanged(m_pageIndex);
    emit documentChanged();
    emit statusMessage(tr("已删除页面 %1").arg(removedName));
    return true;
}

void DesignerCanvas::addControl(const QString &type, const QString &label)
{
    placeControlAt(type, label, QPointF(-1.0, -1.0));
}

DesignerCanvas::Control DesignerCanvas::makeControl(const QString &type, const QString &label)
{
    QString id;
    do {
        id = QStringLiteral("%1_%2").arg(controlPrefix(type))
                 .arg(m_nextControlNumber++, 2, 10, QLatin1Char('0'));
    } while (std::any_of(m_controls.cbegin(), m_controls.cend(),
                         [&id](const Control &control) { return control.id == id; }));
    const QSizeF size = defaultControlSize(type);
    Control control{
        .instanceId = stableId(),
        .id = id,
        .type = type,
        .label = label.isEmpty() ? controlLabel(type) : label,
        .binding = type == QStringLiteral("text") || type == QStringLiteral("button")
            || type == QStringLiteral("image")
            ? QString{} : type == QStringLiteral("progress")
                ? QStringLiteral("telemetry.progress")
                : type == QStringLiteral("status")
                    ? QStringLiteral("telemetry.status")
                    : type == QStringLiteral("switch")
                        ? QStringLiteral("control.enabled")
                        : QStringLiteral("telemetry.temperature"),
        .bindingMode = type == QStringLiteral("text") || type == QStringLiteral("button")
            || type == QStringLiteral("image")
            ? QStringLiteral("none") : type == QStringLiteral("switch")
                ? QStringLiteral("readWrite") : type == QStringLiteral("text-input")
                    ? QStringLiteral("write") : QStringLiteral("read"),
        .eventType = QStringLiteral("none"),
        .eventTarget = QString{},
        .textValue = label.isEmpty() ? controlLabel(type) : label,
        .value = type == QStringLiteral("progress") ? 0.62 : 0.0,
        .rect = QRectF(QPointF(80.0, 140.0), size),
        .fill = QColor(QStringLiteral("#203346")),
        .text = QColor(QStringLiteral("#f2c96b")),
        .interactive = defaultInteractiveForType(type),
        .visible = true,
        .locked = false,
        .assetId = {},
        .scaleMode = QStringLiteral("fit"),
        .inputMaxLength = 32,
        .chartMin = 0.0,
        .chartMax = 100.0,
        .chartWindowLength = 30,
        .chartSamples = {},
        .properties = {},
    };
    // Seed the schema property map from the registry so the generated
    // inspector (and save path) always reflects this type's declared surface.
    if (const ControlSchema *schema =
            ControlSchemaRegistry::instance().findByKeyword(canonicalKeyword(type))) {
        control.properties = schema->defaultProperties;
        const bool knownType = type == QStringLiteral("number")
            || type == QStringLiteral("text") || type == QStringLiteral("button")
            || type == QStringLiteral("progress") || type == QStringLiteral("status")
            || type == QStringLiteral("switch") || type == QStringLiteral("image")
            || type == QStringLiteral("chart") || type == QStringLiteral("text-input");
        if (!knownType) {
            control.binding = control.properties.value(QStringLiteral("binding")).toString();
            control.bindingMode =
                control.properties.value(QStringLiteral("bindingMode")).toString();
            if (!schema->allowedBindingModes.contains(QStringLiteral("none"))
                && control.binding.isEmpty()) {
                control.binding = QStringLiteral("data.value");
            }
        }
    }
    if (type == QStringLiteral("number")) {
        NumericDisplayControl::State state;
        state.id = id;
        state.binding = control.binding;
        state.previewValue = 0.0;
        state.decimals = 1;
        state.unit = QStringLiteral("°C");
        state.fontPixelSize = 32;
        control.numeric.emplace(std::move(state));
    }
    if (type == QStringLiteral("status")) control.value = 1.0;
    if (type == QStringLiteral("switch")) {
        control.value = 0.0;
        control.interactive = true;
    }
    if (type == QStringLiteral("image")) {
        // A picture control is not valid until the user binds a registered
        // image asset through the Inspector.  Leave assetId empty so save-time
        // validation reports the missing reference instead of a placeholder.
        control.assetId = QString{};
        control.scaleMode = QStringLiteral("fit");
    }
    if (type == QStringLiteral("chart")) {
        control.binding = QStringLiteral("telemetry.temperature");
        control.chartMin = 0.0;
        control.chartMax = 100.0;
        control.chartWindowLength = 30;
        for (int i = 0; i < 64; ++i) {
            const double t = static_cast<double>(i) / 63.0;
            control.chartSamples.append(50.0 + 22.0 * std::sin(t * 8.0)
                                        + 6.0 * std::sin(t * 31.0) + 3.0 * std::cos(t * 70.0));
        }
    }
    if (type == QStringLiteral("text-input")) {
        control.binding = QStringLiteral("input.text");
        control.bindingMode = QStringLiteral("write");
        control.inputMaxLength = 32;
        control.textValue = QStringLiteral("请输入");
    }
    return control;
}

QPointF DesignerCanvas::findFreePlacement(const QSizeF &size) const
{
    // Double-click cascade: scan the device surface from the top-left and pick
    // the first slot whose padded footprint does not overlap any existing
    // control, so placements keep a fixed gap instead of stacking (e.g. the
    // wide chart control must not collide with a neighbouring one).
    const double pad = SnapGrid;
    const double step = SnapGrid;
    const double margin = SnapGrid * 3.0;
    for (double y = margin; y + size.height() <= m_deviceSize.height(); y += step) {
        for (double x = margin; x + size.width() <= m_deviceSize.width(); x += step) {
            const QRectF probe(QPointF(x, y), size);
            const QRectF padded = probe.adjusted(-pad, -pad, pad, pad);
            bool overlaps = false;
            for (const Control &existing : m_controls) {
                if (padded.intersects(existing.rect)) {
                    overlaps = true;
                    break;
                }
            }
            if (!overlaps) {
                return QPointF(x, y);
            }
        }
    }
    return QPointF(margin, margin);
}

void DesignerCanvas::appendControl(Control control, const QPointF &point)
{
    if (!ensureEditMode()) return;
    if (point.x() >= 0.0) {
        QPointF position = point - QPointF(control.rect.width() / 2.0, control.rect.height() / 2.0);
        position.setX(std::round(position.x() / SnapGrid) * SnapGrid);
        position.setY(std::round(position.y() / SnapGrid) * SnapGrid);
        position.setX(std::clamp(position.x(), 0.0, m_deviceSize.width() - control.rect.width()));
        position.setY(std::clamp(position.y(), 0.0, m_deviceSize.height() - control.rect.height()));
        control.rect.moveTopLeft(position);
    } else {
        control.rect.moveTopLeft(findFreePlacement(control.rect.size()));
    }
    m_controls.append(control);
    m_selectedIndex = m_controls.size() - 1;
    emitSelection();
    emit statusMessage(tr("已添加 %1 控件 %2").arg(controlLabel(control.type), control.id));
    emit documentChanged();
    update();
}

void DesignerCanvas::alignSelected()
{
    if (!ensureEditMode()) return;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return;
    Control &control = m_controls[m_selectedIndex];
    if (control.locked) {
        emit statusMessage(tr("控件 %1 已锁定").arg(control.label));
        return;
    }
    const double maxX = std::max(0.0, m_deviceSize.width() - control.rect.width());
    const double maxY = std::max(0.0, m_deviceSize.height() - control.rect.height());
    const double alignedX = std::clamp(
        std::round(control.rect.x() / SnapGrid) * SnapGrid, 0.0, maxX);
    const double alignedY = std::clamp(
        std::round(control.rect.y() / SnapGrid) * SnapGrid, 0.0, maxY);
    control.rect.moveTopLeft(QPointF(alignedX, alignedY));
    emitSelection();
    emit statusMessage(tr("已将 %1 对齐到 8px 网格").arg(control.label));
    emit documentChanged();
    update();
}

void DesignerCanvas::refreshSelection()
{
    emitSelection();
}

void DesignerCanvas::removeSelectedControl()
{
    if (!ensureEditMode()) return;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return;
    const QString id = selectedId();
    m_controls.removeAt(m_selectedIndex);
    m_selectedIndex = -1;
    emitSelection();
    emit statusMessage(tr("已删除控件 %1").arg(id));
    emit documentChanged();
    update();
}

bool DesignerCanvas::setSelectedNumericId(const QString &id)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    Control &control = m_controls[m_selectedIndex];
    const QString candidate = id.trimmed();
    for (int index = 0; index < m_controls.size(); ++index) {
        if (index != m_selectedIndex && m_controls.at(index).id == candidate) return false;
    }
    if (!validIdentifier(candidate)) return false;
    if (control.numeric && !control.numeric->setId(candidate)) return false;
    control.id = candidate;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedNumericBinding(const QString &binding)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    Control &control = m_controls[m_selectedIndex];
    const QString candidate = binding.trimmed();
    if (control.numeric) {
        if (!control.numeric->setBinding(candidate)) return false;
        control.binding = control.numeric->state().binding;
    } else {
        if (control.type == QStringLiteral("text")
            || control.type == QStringLiteral("button")) {
            if (!candidate.isEmpty()) return false;
        } else if (candidate.isEmpty() || !validBinding(candidate)) {
            return false;
        }
        control.binding = candidate;
    }
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedNumericPreviewValue(const double value)
{
    if (!ensureEditMode()) return false;
    NumericDisplayControl *numeric = selectedNumericControl();
    if (!numeric || !numeric->setPreviewValue(value)) return false;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedTextValue(const QString &value)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    Control &control = m_controls[m_selectedIndex];
    if (control.type == QStringLiteral("number")) {
        return false;
    }
    const QString candidate = control.type == QStringLiteral("text-input")
        ? value.left(control.inputMaxLength) : value.trimmed().left(64);
    if (candidate.isEmpty() && control.type != QStringLiteral("text-input")) return false;
    control.textValue = candidate;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedGeometry(const double x, const double y,
                                         const double width, const double height)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width)
        || !std::isfinite(height) || width <= 0.0 || height <= 0.0
        || x < 0.0 || y < 0.0 || x + width > m_deviceSize.width()
        || y + height > m_deviceSize.height()) return false;
    Control &control = m_controls[m_selectedIndex];
    if (control.locked) return false;
    const QRectF candidate(std::round(x / SnapGrid) * SnapGrid,
                           std::round(y / SnapGrid) * SnapGrid,
                           std::round(width / SnapGrid) * SnapGrid,
                           std::round(height / SnapGrid) * SnapGrid);
    if (candidate.width() < SnapGrid || candidate.height() < SnapGrid
        || candidate.right() > m_deviceSize.width()
        || candidate.bottom() > m_deviceSize.height()) return false;
    control.rect = candidate;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedBindingMode(const QString &mode)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    const QString candidate = mode.trimmed();
    if (candidate != QStringLiteral("none")
        && candidate != QStringLiteral("read")
        && candidate != QStringLiteral("write")
        && candidate != QStringLiteral("readWrite")) return false;
    Control &control = m_controls[m_selectedIndex];
    if ((control.type == QStringLiteral("text")
         || control.type == QStringLiteral("button")
         || control.type == QStringLiteral("image"))
        && candidate != QStringLiteral("none")) return false;
    if ((control.type == QStringLiteral("number")
         || control.type == QStringLiteral("progress")
         || control.type == QStringLiteral("status"))
        && candidate != QStringLiteral("read")) return false;
    if (control.type == QStringLiteral("switch")
        && candidate == QStringLiteral("none")) return false;
    if (control.type == QStringLiteral("text-input")
        && candidate != QStringLiteral("write")
        && candidate != QStringLiteral("readWrite")) return false;
    control.bindingMode = candidate;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedEvent(const QString &eventType,
                                      const QString &eventTarget)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    const QString type = eventType.trimmed();
    const QString target = eventTarget.trimmed();
    if (type != QStringLiteral("none") && type != QStringLiteral("gotoPage")
        && type != QStringLiteral("command") && type != QStringLiteral("submit")) return false;
    const Control &selected = m_controls.at(m_selectedIndex);
    if (type != QStringLiteral("none") && selected.type != QStringLiteral("button")
        && !(selected.type == QStringLiteral("text-input")
             && (type == QStringLiteral("submit") || type == QStringLiteral("command")))) {
        return false;
    }
    if (type == QStringLiteral("gotoPage") && !m_pageIds.contains(target)) return false;
    if (type == QStringLiteral("command") && target.isEmpty()) return false;
    Control &control = m_controls[m_selectedIndex];
    control.eventType = type;
    control.eventTarget = type == QStringLiteral("none") ? QString{} : target;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

QJsonArray DesignerCanvas::layerList() const
{
    QJsonArray layers;
    for (int index = m_controls.size() - 1; index >= 0; --index) {
        const Control &control = m_controls.at(index);
        layers.append(QJsonObject{
            {QStringLiteral("instanceId"), control.instanceId},
            {QStringLiteral("id"), control.numeric ? control.numeric->state().id
                                                    : control.id},
            {QStringLiteral("label"), control.label},
            {QStringLiteral("type"), control.type},
            {QStringLiteral("visible"), control.visible},
            {QStringLiteral("locked"), control.locked},
            {QStringLiteral("selected"), index == m_selectedIndex},
        });
    }
    return layers;
}

bool DesignerCanvas::selectLayer(const QString &instanceId)
{
    if (!ensureEditMode()) return false;
    for (int index = 0; index < m_controls.size(); ++index) {
        if (m_controls.at(index).instanceId == instanceId) {
            if (m_selectedIndex != index) {
                m_selectedIndex = index;
                emitSelection();
                update();
            }
            return true;
        }
    }
    return false;
}

bool DesignerCanvas::moveLayer(const QString &instanceId, const int offset)
{
    if (!ensureEditMode()) return false;
    int index = -1;
    for (int i = 0; i < m_controls.size(); ++i) {
        if (m_controls.at(i).instanceId == instanceId) {
            index = i;
            break;
        }
    }
    if (index < 0) return false;
    const int target = index + offset;
    if (target < 0 || target >= m_controls.size()) return false;
    m_controls.move(index, target);
    if (m_selectedIndex == index) m_selectedIndex = target;
    else if (m_selectedIndex > index && m_selectedIndex <= target) --m_selectedIndex;
    else if (m_selectedIndex < index && m_selectedIndex >= target) ++m_selectedIndex;
    emitSelection();
    emit statusMessage(tr("已调整图层顺序"));
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedProperty(const QString &key, const QJsonValue &value)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    Control &control = m_controls[m_selectedIndex];
    control.properties.insert(key, value);

    // Keep the typed rendering fields in sync for keys the canvas draws.
    if (key == QStringLiteral("value")) {
        if (control.type == QStringLiteral("text-input")) {
            control.textValue = value.toString();
        } else if (value.isDouble()) {
            control.value = value.toDouble();
            // For numeric-display controls the schema maps "value" to the
            // binding value; the simulated preview is driven by "previewValue".
            if (control.numeric && control.type != QStringLiteral("number")) {
                NumericDisplayControl::State state = control.numeric->state();
                state.previewValue = value.toDouble();
                control.numeric.emplace(std::move(state));
            }
        }
    } else if (key == QStringLiteral("text")) {
        control.textValue = value.toString();
    } else if (key == QStringLiteral("binding")) {
        control.binding = value.toString();
    } else if (key == QStringLiteral("bindingMode")) {
        control.bindingMode = value.toString();
    } else if (key == QStringLiteral("eventType")) {
        control.eventType = value.toString();
    } else if (key == QStringLiteral("eventTarget")) {
        control.eventTarget = value.toString();
    } else if (key == QStringLiteral("visible")) {
        control.visible = value.toBool();
    } else if (key == QStringLiteral("locked")) {
        control.locked = value.toBool();
    } else if (key == QStringLiteral("assetId")) {
        control.assetId = value.toString();
    } else if (key == QStringLiteral("scaleMode")) {
        control.scaleMode = value.toString();
    } else if (key == QStringLiteral("maxLength")) {
        control.inputMaxLength = value.toInt();
    } else if (key == QStringLiteral("previewValue")) {
        control.value = value.toDouble();
        if (control.numeric) {
            NumericDisplayControl::State state = control.numeric->state();
            state.previewValue = value.toDouble();
            control.numeric.emplace(std::move(state));
        }
    } else if (key == QStringLiteral("placeholder")) {
        control.label = value.toString();
    } else if (key == QStringLiteral("decimals") || key == QStringLiteral("unit")
               || key == QStringLiteral("fontPixelSize")
               || key == QStringLiteral("alignment")) {
        if (control.numeric) {
            NumericDisplayControl::State state = control.numeric->state();
            if (key == QStringLiteral("decimals")) {
                state.decimals = value.toInt();
            } else if (key == QStringLiteral("unit")) {
                state.unit = value.toString();
            } else if (key == QStringLiteral("fontPixelSize")) {
                state.fontPixelSize = value.toInt();
            } else {
                state.alignmentIndex = value.toInt();
            }
            control.numeric.emplace(std::move(state));
        }
    } else if (key == QStringLiteral("chartMin") || key == QStringLiteral("chartMax")
               || key == QStringLiteral("chartWindowLength")) {
        if (key == QStringLiteral("chartMin")) {
            control.chartMin = value.toDouble();
        } else if (key == QStringLiteral("chartMax")) {
            control.chartMax = value.toDouble();
        } else {
            control.chartWindowLength = value.toInt();
        }
    } else if (key == QStringLiteral("chartSamples") || key == QStringLiteral("samples")) {
        control.chartSamples.clear();
        const QJsonArray samples = value.toArray();
        for (const QJsonValue &sample : samples) {
            control.chartSamples.append(sample.toDouble());
        }
    } else if (key == QStringLiteral("minValue")) {
        control.chartMin = value.toDouble();
    } else if (key == QStringLiteral("maxValue")) {
        control.chartMax = value.toDouble();
    } else if (key == QStringLiteral("windowLength")) {
        control.chartWindowLength = value.toInt();
    }

    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedVisible(const bool visible)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    m_controls[m_selectedIndex].visible = visible;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedLocked(const bool locked)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    m_controls[m_selectedIndex].locked = locked;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedProgressValue(const double value)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()
        || m_controls.at(m_selectedIndex).type != QStringLiteral("progress")
        || !std::isfinite(value)) {
        return false;
    }
    m_controls[m_selectedIndex].value = std::clamp(value, 0.0, 1.0);
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedControlPreviewValue(const double value)
{
    if (!ensureEditMode()) return false;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()
        || !std::isfinite(value)) return false;
    Control &control = m_controls[m_selectedIndex];
    if (control.type != QStringLiteral("status")
        && control.type != QStringLiteral("switch")) return false;
    control.value = value >= 0.5 ? 1.0 : 0.0;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedAssetId(const QString &assetId)
{
    if (!ensureEditMode() || m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    const QString candidate = assetId.trimmed();
    if (candidate.isEmpty()) return false;
    Control &control = m_controls[m_selectedIndex];
    if (control.type != QStringLiteral("image")) return false;
    control.assetId = candidate;
    emitSelection(); emit documentChanged(); update(); return true;
}

bool DesignerCanvas::setSelectedScaleMode(const QString &scaleMode)
{
    if (!ensureEditMode() || m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    const QString candidate = scaleMode.trimmed();
    if (candidate != QStringLiteral("fit") && candidate != QStringLiteral("fill")
        && candidate != QStringLiteral("stretch") && candidate != QStringLiteral("none")) return false;
    Control &control = m_controls[m_selectedIndex];
    if (control.type != QStringLiteral("image")) return false;
    control.scaleMode = candidate;
    emitSelection(); emit documentChanged(); update(); return true;
}

bool DesignerCanvas::setSelectedInputMaxLength(const int maxLength)
{
    if (!ensureEditMode() || m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return false;
    if (maxLength < 1 || maxLength > 256) return false;
    Control &control = m_controls[m_selectedIndex];
    if (control.type != QStringLiteral("text-input")) return false;
    control.inputMaxLength = maxLength;
    control.textValue = control.textValue.left(maxLength);
    emitSelection(); emit documentChanged(); update(); return true;
}

bool DesignerCanvas::setSelectedChartRange(const double minValue, const double maxValue)
{
    if (!ensureEditMode() || m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()
        || m_controls.at(m_selectedIndex).type != QStringLiteral("chart")
        || !std::isfinite(minValue) || !std::isfinite(maxValue) || maxValue <= minValue) return false;
    Control &control = m_controls[m_selectedIndex];
    control.chartMin = minValue; control.chartMax = maxValue;
    emitSelection(); emit documentChanged(); update(); return true;
}

bool DesignerCanvas::setSelectedChartWindowLength(const int length)
{
    if (!ensureEditMode() || m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()
        || m_controls.at(m_selectedIndex).type != QStringLiteral("chart")
        || length < 2 || length > 4096) return false;
    Control &control = m_controls[m_selectedIndex];
    control.chartWindowLength = length;
    if (control.chartSamples.size() > length)
        control.chartSamples = control.chartSamples.sliced(control.chartSamples.size() - length);
    emitSelection(); emit documentChanged(); update(); return true;
}

bool DesignerCanvas::setSelectedChartSamples(const QJsonArray &samples)
{
    if (!ensureEditMode() || m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()
        || m_controls.at(m_selectedIndex).type != QStringLiteral("chart") || samples.isEmpty()) return false;
    QVector<double> values;
    for (const QJsonValue &sample : samples) {
        if (!sample.isDouble() || !std::isfinite(sample.toDouble())) return false;
        values.append(sample.toDouble());
    }
    Control &control = m_controls[m_selectedIndex];
    if (values.size() > control.chartWindowLength)
        values = values.sliced(values.size() - control.chartWindowLength);
    control.chartSamples = std::move(values);
    emitSelection(); emit documentChanged(); update(); return true;
}

bool DesignerCanvas::setPreviewTextInput(const QString &value)
{
    if (!m_previewMode || m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()
        || m_controls.at(m_selectedIndex).type != QStringLiteral("text-input")) return false;
    Control &control = m_controls[m_selectedIndex];
    control.textValue = value.left(control.inputMaxLength);
    emitSelection();
    emit statusMessage(tr("预览输入已更新（退出预览后恢复作者内容）"));
    update();
    return true;
}

void DesignerCanvas::setSelectedNumericDecimals(const int decimals)
{
    if (!ensureEditMode()) return;
    if (NumericDisplayControl *numeric = selectedNumericControl()) {
        numeric->setDecimals(decimals);
        emitSelection();
        emit documentChanged();
        update();
    }
}

void DesignerCanvas::setSelectedNumericUnit(const QString &unit)
{
    if (!ensureEditMode()) return;
    if (NumericDisplayControl *numeric = selectedNumericControl()) {
        numeric->setUnit(unit);
        emitSelection();
        emit documentChanged();
        update();
    }
}

void DesignerCanvas::setSelectedNumericFontPixelSize(const int pixelSize)
{
    if (!ensureEditMode()) return;
    if (NumericDisplayControl *numeric = selectedNumericControl()) {
        numeric->setFontPixelSize(pixelSize);
        emitSelection();
        emit documentChanged();
        update();
    }
}

void DesignerCanvas::setSelectedNumericAlignment(const int alignmentIndex)
{
    if (!ensureEditMode()) return;
    if (NumericDisplayControl *numeric = selectedNumericControl()) {
        numeric->setAlignmentIndex(alignmentIndex);
        emitSelection();
        emit documentChanged();
        update();
    }
}

QSizeF DesignerCanvas::effectiveSize() const
{
    return (m_rotation % 180 == 0)
        ? m_deviceSize
        : QSizeF(m_deviceSize.height(), m_deviceSize.width());
}

QPointF DesignerCanvas::rotatePoint(const QPointF &point, const double degrees) const
{
    const double radian = degrees * MrPi / 180.0;
    const double cosine = std::cos(radian);
    const double sine = std::sin(radian);
    return QPointF(point.x() * cosine - point.y() * sine,
                   point.x() * sine + point.y() * cosine);
}

QRectF DesignerCanvas::deviceRect() const
{
    const QSizeF size = effectiveSize();
    const QRectF available = QRectF(rect()).adjusted(34.0, 32.0, -34.0, -34.0);
    const double scale = std::min(available.width() / size.width(),
                                  available.height() / size.height());
    const QSizeF device = size * std::max(0.1, scale);
    return QRectF(available.center() - QPointF(device.width() / 2.0, device.height() / 2.0),
                  device);
}

QPointF DesignerCanvas::toDevice(const QPointF &point) const
{
    const QRectF bounds = deviceRect();
    const double scale = bounds.width() / effectiveSize().width();
    const QPointF local = (point - bounds.center()) / scale;
    return rotatePoint(local, -double(m_rotation)) +
           QPointF(m_deviceSize.width() / 2.0, m_deviceSize.height() / 2.0);
}

QPointF DesignerCanvas::toCanvas(const QPointF &point) const
{
    const QRectF bounds = deviceRect();
    const double scale = bounds.width() / effectiveSize().width();
    const QPointF local = point -
                          QPointF(m_deviceSize.width() / 2.0, m_deviceSize.height() / 2.0);
    return bounds.center() + rotatePoint(local, double(m_rotation)) * scale;
}

QVector<QPointF> DesignerCanvas::handleCenters(const QRectF &rect)
{
    return {
        rect.topLeft(),
        QPointF(rect.center().x(), rect.top()),
        rect.topRight(),
        QPointF(rect.right(), rect.center().y()),
        rect.bottomRight(),
        QPointF(rect.center().x(), rect.bottom()),
        rect.bottomLeft(),
        QPointF(rect.left(), rect.center().y()),
    };
}

int DesignerCanvas::hitResizeHandle(const QPointF &widgetPoint) const
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return -1;
    const Control &control = m_controls.at(m_selectedIndex);
    if (control.locked) return -1;
    const QVector<QPointF> centers = handleCenters(control.rect);
    for (int i = 0; i < centers.size(); ++i) {
        // Compare in widget space so handles stay a fixed on-screen size.
        const QPointF center = toCanvas(centers.at(i));
        const double dx = center.x() - widgetPoint.x();
        const double dy = center.y() - widgetPoint.y();
        if (std::hypot(dx, dy) <= HandleHitRadius) return i;
    }
    return -1;
}

QRectF DesignerCanvas::resizeRectForHandle(const QRectF &startRect,
                                           const QPointF &startPoint,
                                           const QPointF &currentPoint,
                                           const int handle) const
{
    const double deltaX = currentPoint.x() - startPoint.x();
    const double deltaY = currentPoint.y() - startPoint.y();
    const double minSize = SnapGrid;

    QRectF result = startRect;
    double left = startRect.left();
    double top = startRect.top();
    double right = startRect.right();
    double bottom = startRect.bottom();

    const bool west = handle == 0 || handle == 6 || handle == 7;
    const bool east = handle == 2 || handle == 3 || handle == 4;
    const bool north = handle == 0 || handle == 1 || handle == 2;
    const bool south = handle == 4 || handle == 5 || handle == 6;

    if (west) left = std::min(startRect.left() + deltaX, right - minSize);
    if (east) right = std::max(right, startRect.right() + deltaX);
    if (north) top = std::min(startRect.top() + deltaY, bottom - minSize);
    if (south) bottom = std::max(bottom, startRect.bottom() + deltaY);

    if (west) left = std::round(left / SnapGrid) * SnapGrid;
    if (east) right = std::round(right / SnapGrid) * SnapGrid;
    if (north) top = std::round(top / SnapGrid) * SnapGrid;
    if (south) bottom = std::round(bottom / SnapGrid) * SnapGrid;

    result = QRectF(QPointF(left, top), QPointF(right, bottom));
    result.setLeft(std::clamp(result.left(), 0.0, m_deviceSize.width() - minSize));
    result.setTop(std::clamp(result.top(), 0.0, m_deviceSize.height() - minSize));
    result.setRight(std::clamp(result.right(), result.left() + minSize, m_deviceSize.width()));
    result.setBottom(std::clamp(result.bottom(), result.top() + minSize, m_deviceSize.height()));
    return result;
}

void DesignerCanvas::beginPlacing(const QString &type, const QString &label)
{
    if (!ensureEditMode()) return;
    if (definitionIdForType(type).isEmpty()) {
        emit statusMessage(tr("当前版本不支持该控件类型"));
        return;
    }
    emit statusMessage(tr("已选择 %1 · 移动到画布点击放置，右键取消").arg(controlLabel(type)));
    m_placing = true;
    m_placingType = type;
    m_placingLabel = label;
    const QPoint local = mapFromGlobal(QCursor::pos());
    if (rect().contains(local)) {
        m_cursorPoint = toDevice(QPointF(local));
        m_hasCursor = true;
    } else {
        m_hasCursor = false;
    }
    update();
    emit placingChanged();
}

void DesignerCanvas::cancelPlacing()
{
    if (!m_placing) return;
    m_placing = false;
    m_placingType.clear();
    m_placingLabel.clear();
    m_hasCursor = false;
    update();
    emit placingChanged();
}

void DesignerCanvas::setDeviceSize(const QSizeF &size)
{
    if (size.width() < 1.0 || size.height() < 1.0) return;
    if (qFuzzyCompare(m_deviceSize.width(), size.width())
        && qFuzzyCompare(m_deviceSize.height(), size.height())) return;
    m_deviceSize = size;
    // Clamp any controls that now fall outside the smaller device surface.
    for (Control &control : m_controls) {
        control.rect.setLeft(std::clamp(control.rect.left(), 0.0,
                                        std::max(0.0, m_deviceSize.width() - control.rect.width())));
        control.rect.setTop(std::clamp(control.rect.top(), 0.0,
                                       std::max(0.0, m_deviceSize.height() - control.rect.height())));
        control.rect.setRight(std::min(control.rect.right(), m_deviceSize.width()));
        control.rect.setBottom(std::min(control.rect.bottom(), m_deviceSize.height()));
    }
    update();
}

void DesignerCanvas::setRotation(const int degrees)
{
    const int normalized = ((degrees % 360) + 360) % 360;
    if (normalized == m_rotation) return;
    m_rotation = normalized;
    update();
}

QJsonObject DesignerCanvas::backgroundState() const
{
    return m_background.toJson();
}

void DesignerCanvas::setBackground(const QJsonObject &background)
{
    m_background = PageBackground::fromJson(background);
    emit documentChanged();
    update();
}

void DesignerCanvas::drawPageBackground(QPainter &painter, const QRectF &bounds) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_background.type == QStringLiteral("image") && !m_background.assetId.isEmpty()) {
        const QImage image = m_assetImages.value(m_background.assetId);
        if (!image.isNull()) {
            painter.save();
            painter.setClipRect(bounds);
            const QString mode = m_background.imageMode;
            if (mode == QStringLiteral("cover")) {
                const qreal scale = std::max(bounds.width() / image.width(),
                                             bounds.height() / image.height());
                const QSizeF scaled(image.width() * scale, image.height() * scale);
                const QPointF topLeft = bounds.center()
                    - QPointF(scaled.width() / 2.0, scaled.height() / 2.0);
                painter.drawImage(QRectF(topLeft, scaled), image);
            } else if (mode == QStringLiteral("contain")) {
                const qreal scale = std::min(bounds.width() / image.width(),
                                             bounds.height() / image.height());
                const QSizeF scaled(image.width() * scale, image.height() * scale);
                const QPointF topLeft = bounds.center()
                    - QPointF(scaled.width() / 2.0, scaled.height() / 2.0);
                painter.drawImage(QRectF(topLeft, scaled), image);
            } else if (mode == QStringLiteral("tile")) {
                painter.fillRect(bounds, QBrush(image));
            } else if (mode == QStringLiteral("center")) {
                painter.drawImage(bounds.center() - QPointF(image.width() / 2.0,
                                                            image.height() / 2.0), image);
            } else { // stretch (default)
                painter.drawImage(bounds, image);
            }
            painter.restore();
            painter.restore();
            return;
        }
    }

    if (m_background.type == QStringLiteral("linear")
        || m_background.type == QStringLiteral("radial")) {
        QPainterPath path;
        path.addRoundedRect(bounds, 5.0, 5.0);
        QBrush brush;
        if (m_background.type == QStringLiteral("radial")) {
            QRadialGradient radial(bounds.center(), std::max(bounds.width(), bounds.height()) * 0.7);
            radial.setColorAt(0.0, m_background.color);
            radial.setColorAt(1.0, m_background.color2);
            brush = QBrush(radial);
        } else {
            QLinearGradient linear(bounds.topLeft(), bounds.topRight());
            const double rad = m_background.angle * 3.14159265358979323846 / 180.0;
            const QPointF delta(std::cos(rad) * bounds.width() * 0.5,
                                std::sin(rad) * bounds.height() * 0.5);
            linear.setStart(bounds.center() - delta);
            linear.setFinalStop(bounds.center() + delta);
            linear.setColorAt(0.0, m_background.color);
            linear.setColorAt(1.0, m_background.color2);
            brush = QBrush(linear);
        }
        painter.fillPath(path, brush);
    } else {
        painter.fillRect(bounds, m_background.color);
    }

    painter.restore();
}

void DesignerCanvas::placeControlAt(const QString &type, const QString &label, const QPointF &point)
{
    if (!ensureEditMode()) return;
    if (definitionIdForType(type).isEmpty()) {
        emit statusMessage(tr("当前版本不支持该控件类型"));
        return;
    }
    if (point.x() >= 0.0) {
        cancelPlacing();
    }
    appendControl(makeControl(type, label), point);
}

int DesignerCanvas::hitTest(const QPointF &devicePoint) const
{
    for (int index = m_controls.size() - 1; index >= 0; --index) {
        if ((!m_previewMode || m_controls.at(index).visible)
            && m_controls.at(index).rect.contains(devicePoint)) return index;
    }
    return -1;
}

NumericDisplayControl *DesignerCanvas::selectedNumericControl()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return nullptr;
    if (!m_controls[m_selectedIndex].numeric) return nullptr;
    return &*m_controls[m_selectedIndex].numeric;
}

const NumericDisplayControl *DesignerCanvas::selectedNumericControl() const
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return nullptr;
    const std::optional<NumericDisplayControl> &numeric =
        m_controls.at(m_selectedIndex).numeric;
    return numeric ? &*numeric : nullptr;
}

bool DesignerCanvas::ensureEditMode()
{
    if (!m_previewMode) return true;
    emit statusMessage(tr("请先退出预览，再修改工程"));
    return false;
}

QVector<DesignerCanvas::Control> DesignerCanvas::controlsForPage(const int pageIndex) const
{
    const auto numeric = [](const QString &id, const QString &label,
                            const QString &binding, const double value,
                            const int decimals, const QString &unit,
                            const QRectF &geometry) {
        Control control{
            .instanceId = stableId(),
            .id = id,
            .type = QStringLiteral("number"),
            .label = label,
            .binding = binding,
            .bindingMode = QStringLiteral("read"),
            .eventType = QStringLiteral("none"),
            .eventTarget = QString{},
            .textValue = label,
            .value = value,
            .rect = geometry,
            .fill = QColor(QStringLiteral("#203346")),
            .text = QColor(QStringLiteral("#f2c96b")),
            .interactive = false,
            .visible = true,
            .locked = false,
            .assetId = {},
            .scaleMode = QStringLiteral("fit"),
            .inputMaxLength = 32,
            .chartMin = 0.0,
            .chartMax = 100.0,
            .chartWindowLength = 30,
            .chartSamples = {},
            .properties = {},
        };
        NumericDisplayControl::State state;
        state.id = id;
        state.binding = binding;
        state.previewValue = value;
        state.decimals = decimals;
        state.unit = unit;
        state.fontPixelSize = 40;
        state.alignmentIndex = 1;
        control.numeric.emplace(std::move(state));
        return control;
    };
    if (pageIndex == 1) {
        return {numeric(QStringLiteral("setpoint"), QStringLiteral("目标温度"),
                        QStringLiteral("control.setpoint"), 30.0, 1,
                        QStringLiteral("°C"), QRectF(250, 165, 300, 120))};
    }
    if (pageIndex == 2) {
        return {numeric(QStringLiteral("alarm_code"), QStringLiteral("告警代码"),
                        QStringLiteral("device.alarm_code"), 0.0, 0,
                        QString{}, QRectF(250, 165, 300, 120))};
    }
    return {numeric(QStringLiteral("temperature"), QStringLiteral("温度"),
                    QStringLiteral("telemetry.temperature"), 24.8, 1,
                    QStringLiteral("°C"), QRectF(250, 165, 300, 120))};
}

void DesignerCanvas::rebuildPage(const int pageIndex)
{
    if (m_pageControls.isEmpty()) {
        for (int index = 0; index < 3; ++index) {
            m_pageControls.append(controlsForPage(index));
        }
    } else if (m_pageIndex >= 0 && m_pageIndex < m_pageControls.size()) {
        m_pageControls[m_pageIndex] = m_controls;
    }
    const int lastPage = static_cast<int>(m_pageControls.size()) - 1;
    m_pageIndex = std::clamp(pageIndex, 0, lastPage);
    m_controls = m_pageControls.at(m_pageIndex);
    m_selectedIndex = m_controls.isEmpty() ? -1 : 0;
    m_dragging = false;
    emitSelection();
    update();
}

void DesignerCanvas::emitSelection()
{
    emit selectionChanged(selectedId(), selectedType(), selectedBinding());
    if (m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()) {
        const Control &control = m_controls.at(m_selectedIndex);
        emit controlPropertiesChanged(control.id, control.type, control.binding,
                                      control.textValue, control.value,
                                      control.bindingMode, control.eventType,
                                      control.eventTarget, control.rect,
                                      control.visible, control.locked);
        QJsonObject details{
            {QStringLiteral("assetId"), control.assetId},
            {QStringLiteral("scaleMode"), control.scaleMode},
            {QStringLiteral("maxLength"), control.inputMaxLength},
            {QStringLiteral("chartMin"), control.chartMin},
            {QStringLiteral("chartMax"), control.chartMax},
            {QStringLiteral("chartWindowLength"), control.chartWindowLength},
        };
        QJsonArray samples;
        for (const double sample : control.chartSamples) samples.append(sample);
        details.insert(QStringLiteral("chartSamples"), samples);
        emit controlDetailsChanged(details);

        // Build the full property surface (schema keys plus typed fields) so
        // the generated inspector always reflects the control's real state.
        QJsonObject state = control.properties;
        if (control.numeric) {
            const NumericDisplayControl::State &numeric = control.numeric->state();
            state.insert(QStringLiteral("previewValue"), numeric.previewValue);
            state.insert(QStringLiteral("decimals"), numeric.decimals);
            state.insert(QStringLiteral("unit"), numeric.unit);
            state.insert(QStringLiteral("fontPixelSize"), numeric.fontPixelSize);
            state.insert(QStringLiteral("alignment"), numeric.alignmentIndex);
        } else {
            state.insert(QStringLiteral("value"), control.value);
        }
        state.insert(QStringLiteral("text"), control.textValue);
        state.insert(QStringLiteral("binding"), control.binding);
        state.insert(QStringLiteral("bindingMode"), control.bindingMode);
        state.insert(QStringLiteral("eventType"), control.eventType);
        state.insert(QStringLiteral("eventTarget"), control.eventTarget);
        state.insert(QStringLiteral("visible"), control.visible);
        state.insert(QStringLiteral("locked"), control.locked);
        if (control.type == QStringLiteral("image")) {
            state.insert(QStringLiteral("assetId"), control.assetId);
            state.insert(QStringLiteral("scaleMode"), control.scaleMode);
        }
        if (control.type == QStringLiteral("chart")) {
            state.insert(QStringLiteral("samples"), samples);
            state.insert(QStringLiteral("minValue"), control.chartMin);
            state.insert(QStringLiteral("maxValue"), control.chartMax);
            state.insert(QStringLiteral("windowLength"), control.chartWindowLength);
        }
        if (control.type == QStringLiteral("text-input")) {
            state.insert(QStringLiteral("value"), control.textValue);
            state.insert(QStringLiteral("maxLength"), control.inputMaxLength);
            state.insert(QStringLiteral("placeholder"), control.label);
        }
        emit controlStateChanged(control.type, state, control.rect);
    }
    if (const NumericDisplayControl *numeric = selectedNumericControl()) {
        const NumericDisplayControl::State &state = numeric->state();
        emit numericPropertiesChanged(state.id, state.binding, state.previewValue,
                                      state.decimals, state.unit, state.fontPixelSize,
                                      state.alignmentIndex);
    }
}

void DesignerCanvas::drawImageControl(QPainter &painter, const Control &control,
                                      const QRectF &rect) const
{
    const QRectF content = rect.adjusted(6, 6, -6, -6);
    const QHash<QString, QImage>::const_iterator imageIt =
        m_assetImages.constFind(control.assetId);
    const QImage *image = imageIt != m_assetImages.constEnd() ? &imageIt.value() : nullptr;

    if (image == nullptr || image->isNull()) {
        auto imgColor = [&control](const char *key, const QColor &fallback) -> QColor {
            const QString hex = control.properties.value(QLatin1String(key)).toString();
            if (!hex.isEmpty()) {
                const QColor parsed(hex);
                if (parsed.isValid()) return parsed;
            }
            return fallback;
        };
        painter.save();
        painter.setPen(QPen(imgColor("borderColor", QColor(QStringLiteral("#cbd5e1"))), 1.0, Qt::DashLine));
        painter.setBrush(imgColor("backgroundColor", QColor(QStringLiteral("#f8fafc"))));
        painter.drawRect(content);
        painter.setPen(QPen(imgColor("accentColor", QColor(QStringLiteral("#94a3b8"))), 2.0));
        painter.drawLine(content.left() + 18, content.bottom() - 18,
                         content.center().x(), content.top() + 24);
        painter.drawLine(content.center().x(), content.top() + 24,
                         content.right() - 18, content.bottom() - 18);
        painter.setPen(imgColor("textColor", QColor(QStringLiteral("#64748b"))));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        painter.drawText(rect.adjusted(12, 8, -12, -8), Qt::AlignCenter,
                         control.assetId.isEmpty() ? tr("未选择图片") : control.assetId);
        painter.restore();
        return;
    }

    painter.save();
    painter.setClipRect(content);
    QRectF target = content;
    if (control.scaleMode == QStringLiteral("fill")) {
        const qreal scale = std::max(content.width() / image->width(),
                                     content.height() / image->height());
        const QSizeF scaled(image->width() * scale, image->height() * scale);
        target = QRectF(content.center()
                            - QPointF(scaled.width() / 2.0, scaled.height() / 2.0),
                        scaled);
    } else if (control.scaleMode == QStringLiteral("none")) {
        target = QRectF(content.center()
                            - QPointF(image->width() / 2.0, image->height() / 2.0),
                        QSizeF(image->width(), image->height()));
    } else if (control.scaleMode != QStringLiteral("stretch")) { // fit
        const qreal scale = std::min(content.width() / image->width(),
                                     content.height() / image->height());
        const QSizeF scaled(image->width() * scale, image->height() * scale);
        target = QRectF(content.center()
                            - QPointF(scaled.width() / 2.0, scaled.height() / 2.0),
                        scaled);
    }
    painter.drawImage(target, *image);
    painter.restore();
}

void DesignerCanvas::drawSelectionOverlay(QPainter &painter,
                                          const Control &control) const
{
    const QRectF rect = control.rect;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#2f6feb")), 1.2, Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect.adjusted(-1.0, -1.0, 1.0, 1.0), 4.0, 4.0);

    const QVector<QPointF> centers = handleCenters(rect);
    painter.setPen(QPen(QColor(QStringLiteral("#2f6feb")), 1.2));
    painter.setBrush(Qt::white);
    for (const QPointF &point : centers) {
        painter.drawRoundedRect(QRectF(point - QPointF(HandleSize / 2.0, HandleSize / 2.0),
                                       QSizeF(HandleSize, HandleSize)), 2.0, 2.0);
    }

    if (control.locked) {
        painter.setPen(QColor(QStringLiteral("#e5484d")));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
        painter.drawText(rect.adjusted(5, 4, -5, -4), Qt::AlignRight | Qt::AlignTop,
                         tr("锁定"));
    }
    painter.restore();
}

void DesignerCanvas::drawPlacingGhost(QPainter &painter) const
{
    if (!m_hasCursor) {
        return;
    }
    const QSizeF size = defaultControlSize(m_placingType);
    QRectF placeholder(m_cursorPoint - QPointF(size.width() / 2.0, size.height() / 2.0), size);
    placeholder.moveLeft(std::round(placeholder.left() / SnapGrid) * SnapGrid);
    placeholder.moveTop(std::round(placeholder.top() / SnapGrid) * SnapGrid);
    placeholder.setRight(std::min(placeholder.right(), m_deviceSize.width()));
    placeholder.setBottom(std::min(placeholder.bottom(), m_deviceSize.height()));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(0.55);
    painter.setPen(QPen(QColor(QStringLiteral("#2f6feb")), 1.0, Qt::DashLine));
    painter.setBrush(QColor(47, 111, 235, 30));
    painter.drawRoundedRect(placeholder, 6.0, 6.0);
    painter.setPen(QColor(QStringLiteral("#2f6feb")));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
    painter.drawText(placeholder, Qt::AlignCenter,
                     controlLabel(m_placingType));
    painter.restore();
}

void DesignerCanvas::drawControl(QPainter &painter, const Control &control,
                                 const bool selected) const
{
    const QRectF rect = control.rect;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (!control.visible) painter.setOpacity(0.30);

    // Schema-driven palette. Every color is overridable through the control's
    // `properties` map; the fallbacks form the cohesive stock look.
    auto propColor = [&control](const char *key, const QColor &fallback) -> QColor {
        const QString hex = control.properties.value(QLatin1String(key)).toString();
        if (!hex.isEmpty()) {
            const QColor parsed(hex);
            if (parsed.isValid()) return parsed;
        }
        return fallback;
    };

    // Height-proportional text so labels stay inside the control at any size.
    auto fitFont = [&rect](double heightRatio, int minPx, int maxPx) {
        QFont font(QStringLiteral("Segoe UI"));
        font.setPixelSize(std::clamp(int(rect.height() * heightRatio), minPx, maxPx));
        font.setWeight(QFont::DemiBold);
        return font;
    };

    if (control.type == QStringLiteral("number")) {
        const NumericDisplayControl *numeric = control.numeric ? &*control.numeric : nullptr;
        const QColor valueInk = propColor("textColor", QColor(QStringLiteral("#0f172a")));
        const QColor bindInk  = propColor("bindColor", QColor(QStringLiteral("#94a3b8")));
        const double h = rect.height();
        const int userPx = numeric ? numeric->state().fontPixelSize : 0;
        const int fitPx  = qMax(12, int(h * 0.36));
        QFont valueFont(QStringLiteral("Bahnschrift"));
        valueFont.setPixelSize(userPx > 0 ? std::clamp(userPx, 12, fitPx) : fitPx);
        valueFont.setWeight(QFont::DemiBold);
        painter.setPen(valueInk);
        painter.setFont(valueFont);
        painter.drawText(rect.adjusted(14, 6, -12, -h * 0.40),
                         numeric ? numeric->alignment()
                                 : Qt::AlignLeft | Qt::AlignVCenter,
                         numeric ? numeric->formattedValue() : control.label);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        painter.setPen(bindInk);
        painter.drawText(rect.adjusted(14, h * 0.58, -12, -4),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         numeric ? numeric->state().binding : control.binding);
    } else if (control.type == QStringLiteral("text")) {
        painter.setPen(propColor("textColor", QColor(QStringLiteral("#0f172a"))));
        painter.setFont(fitFont(0.34, 10, 40));
        painter.drawText(rect.adjusted(14, 8, -14, -8),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         control.textValue.isEmpty() ? control.label : control.textValue);
    } else if (control.type == QStringLiteral("button")) {
        QColor buttonFill = propColor("backgroundColor", QColor(QStringLiteral("#2563eb")));
        if (selected) buttonFill = buttonFill.darker(112);
        painter.setBrush(buttonFill);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rect, 6.0, 6.0);
        painter.setPen(propColor("textColor", QColor(Qt::white)));
        painter.setFont(fitFont(0.32, 10, 26));
        painter.drawText(rect, Qt::AlignCenter,
                         control.textValue.isEmpty() ? control.label : control.textValue);
    } else if (control.type == QStringLiteral("progress")) {
        const QColor pInk = propColor("textColor", QColor(QStringLiteral("#334155")));
        const QColor accent = propColor("accentColor", QColor(QStringLiteral("#3b82f6")));
        const QColor trackFill = propColor("trackColor", QColor(QStringLiteral("#e2e8f0")));
        const double minValue = control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
        const double maxValue = control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
        const double ratio = std::clamp((control.value - minValue)
            / std::max(1e-9, maxValue - minValue), 0.0, 1.0);
        const double h = rect.height();
        const QRectF labelRect = rect.adjusted(12, 5, -12, -h * 0.46);
        const double trackH = std::max(4.0, h * 0.22);
        const QRectF track(rect.left() + 12, rect.bottom() - trackH - 5.0,
                           rect.width() - 24, trackH);
        painter.setPen(pInk);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter,
                         control.textValue.isEmpty() ? control.label : control.textValue);
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter,
                         tr("%1%").arg(qRound(ratio * 100.0)));
        painter.setPen(Qt::NoPen);
        painter.setBrush(trackFill);
        painter.drawRoundedRect(track, trackH / 2.0, trackH / 2.0);
        painter.setBrush(accent);
        painter.drawRoundedRect(QRectF(track.topLeft(), QSizeF(track.width() * ratio, trackH)),
                                trackH / 2.0, trackH / 2.0);
    } else if (control.type == QStringLiteral("status")) {
        const bool active = control.value >= 0.5;
        const QColor onColor = propColor("activeColor", QColor(QStringLiteral("#22c55e")));
        const QColor offColor = propColor("inactiveColor", QColor(QStringLiteral("#cbd5e1")));
        const QColor sInk = propColor("textColor", QColor(QStringLiteral("#0f172a")));
        const QRectF lamp(rect.left() + 14, rect.center().y() - 9, 18, 18);
        painter.setPen(QPen(active ? onColor.lighter(135) : offColor.darker(135), 2.0));
        painter.setBrush(active ? onColor : offColor);
        painter.drawEllipse(lamp);
        painter.setPen(sInk);
        painter.setFont(fitFont(0.30, 10, 22));
        painter.drawText(rect.adjusted(44, 0, -12, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         control.textValue.isEmpty() ? control.label : control.textValue);
    } else if (control.type == QStringLiteral("switch")) {
        const bool active = control.value >= 0.5;
        const QColor onColor = propColor("onColor", QColor(QStringLiteral("#22c55e")));
        const QColor offColor = propColor("offColor", QColor(QStringLiteral("#cbd5e1")));
        const QColor wInk = propColor("textColor", QColor(QStringLiteral("#0f172a")));
        painter.setPen(wInk);
        painter.setFont(fitFont(0.30, 9, 20));
        painter.drawText(rect.adjusted(12, 0, -72, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         control.textValue.isEmpty() ? control.label : control.textValue);
        const QRectF track(rect.right() - 60, rect.center().y() - 13, 48, 26);
        painter.setPen(Qt::NoPen);
        painter.setBrush(active ? onColor : offColor);
        painter.drawRoundedRect(track, 13.0, 13.0);
        painter.setBrush(Qt::white);
        const qreal knobX = active ? track.right() - 22 : track.left() + 4;
        painter.drawEllipse(QRectF(knobX, track.top() + 4, 18, 18));
    } else if (control.type == QStringLiteral("image")) {
        drawImageControl(painter, control, rect);
    } else if (control.type == QStringLiteral("text-input")) {
        const QColor inputBg = propColor("backgroundColor", QColor(Qt::white));
        const QColor inputInk = propColor("textColor", QColor(QStringLiteral("#334155")));
        const QColor inputAccent = propColor("accentColor", QColor(QStringLiteral("#3b82f6")));
        painter.setBrush(inputBg);
        painter.setPen(QPen(propColor("borderColor", QColor(QStringLiteral("#cbd5e1"))), 1.0));
        painter.drawRoundedRect(rect, 5.0, 5.0);
        painter.setPen(inputInk);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 11));
        painter.drawText(rect.adjusted(12, 0, -12, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         control.textValue.isEmpty() ? control.label : control.textValue);
        if (m_previewMode) {
            painter.setPen(QPen(inputAccent, 1.0));
            painter.drawLine(rect.right() - 12, rect.top() + 10, rect.right() - 12, rect.bottom() - 10);
        }
    } else if (control.type == QStringLiteral("chart")) {
        const QColor cInk = propColor("textColor", QColor(QStringLiteral("#334155")));
        const QColor cAccent = propColor("accentColor", QColor(QStringLiteral("#3b82f6")));
        const QColor grid = propColor("gridColor", QColor(QStringLiteral("#e2e8f0")));
        painter.setPen(QPen(grid, 1.0));
        for (int i = 1; i < 4; ++i) {
            const double y = rect.top() + rect.height() * i / 4.0;
            painter.drawLine(QPointF(rect.left() + 12, y), QPointF(rect.right() - 12, y));
        }
        const QRectF plot = rect.adjusted(14, 24, -14, -18);
        const QVector<double> samples = control.chartSamples.isEmpty()
            ? QVector<double>{control.value} : control.chartSamples;
        QVector<QPointF> points;
        points.reserve(samples.size());
        for (int i = 0; i < samples.size(); ++i) {
            const double ratio = samples.size() <= 1 ? 0.0
                : static_cast<double>(i) / (samples.size() - 1);
            const double normalized = std::clamp((samples.at(i) - control.chartMin)
                / std::max(1e-9, control.chartMax - control.chartMin), 0.0, 1.0);
            points.append(QPointF(plot.left() + ratio * plot.width(),
                                  plot.bottom() - normalized * plot.height()));
        }
        // Soft area fill under the trace so the curve reads as a live waveform.
        if (points.size() >= 2) {
            QPainterPath area;
            area.moveTo(points.constFirst().x(), plot.bottom());
            appendSmoothPath(area, points);
            area.lineTo(points.constLast().x(), plot.bottom());
            area.closeSubpath();
            painter.setPen(Qt::NoPen);
            QColor areaFill = cAccent;
            areaFill.setAlpha(36);
            painter.setBrush(areaFill);
            painter.drawPath(area);
        }
        QPainterPath trace;
        appendSmoothPath(trace, points);
        painter.setPen(QPen(cAccent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(trace);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        painter.setPen(cInk);
        painter.drawText(rect.adjusted(12, 8, -12, -8), Qt::AlignLeft | Qt::AlignTop,
                         control.label);
    } else if (control.type == QStringLiteral("gauge")) {
        const double minValue = control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
        const double maxValue = control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
        const double ratio = std::clamp((control.value - minValue) / std::max(1e-9, maxValue - minValue), 0.0, 1.0);
        const QColor gInk = propColor("textColor", QColor(QStringLiteral("#334155")));
        const QColor gAccent = propColor("accentColor", QColor(QStringLiteral("#3b82f6")));
        const QColor gTrack = propColor("trackColor", QColor(QStringLiteral("#e2e8f0")));
        const QRectF zone = rect.adjusted(16, 26, -16, -16);
        const QPointF center(zone.center().x(), zone.bottom());
        const double radius = std::max(4.0, std::min(zone.width() / 2.0, zone.height()));
        const QRectF arcBounds(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
        painter.setPen(QPen(gTrack, 6.0, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(arcBounds, 0, 180 * 16);
        painter.setPen(QPen(gAccent, 6.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(arcBounds, 180 * 16, int(-180 * 16 * ratio));
        constexpr double kPi = 3.14159265358979323846;
        const double needleAngle = (180.0 - 180.0 * ratio) * kPi / 180.0;
        const QPointF needleTip(center.x() + radius * 0.8 * std::cos(needleAngle),
                                center.y() - radius * 0.8 * std::sin(needleAngle));
        painter.setPen(QPen(gInk, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(center + QPointF(0.0, -4.0), needleTip);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        painter.drawText(rect.adjusted(12, 6, -12, -8), Qt::AlignTop | Qt::AlignRight,
                         control.label);
    } else if (control.type == QStringLiteral("sevenSegment")) {
        const int digits = control.properties.value(QStringLiteral("digits")).toInt(6);
        const QColor segBg = propColor("backgroundColor", QColor(QStringLiteral("#0f172a")));
        const QColor segInk = propColor("color", QColor(QStringLiteral("#ef4444")));
        painter.setBrush(segBg);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rect.adjusted(8, 10, -8, -10), 5.0, 5.0);
        QFont segFont(QStringLiteral("Bahnschrift"));
        segFont.setPixelSize(qMax(12, int(rect.height() * 0.55)));
        segFont.setWeight(QFont::DemiBold);
        painter.setPen(segInk);
        painter.setFont(segFont);
        painter.drawText(rect.adjusted(14, 0, -14, 0), Qt::AlignCenter,
                         QString::number(control.value, 'f', 1).rightJustified(digits, QLatin1Char('0')));
    } else if (control.type == QStringLiteral("slider")) {
        const double minValue = control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
        const double maxValue = control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
        const double ratio = std::clamp((control.value - minValue) / std::max(1e-9, maxValue - minValue), 0.0, 1.0);
        const QColor sAccent = propColor("accentColor", QColor(QStringLiteral("#3b82f6")));
        const QColor sTrack = propColor("trackColor", QColor(QStringLiteral("#e2e8f0")));
        const QColor sInk = propColor("textColor", QColor(QStringLiteral("#334155")));
        const bool vertical = control.properties.value(QStringLiteral("orientation")).toString()
                                   == QStringLiteral("vertical");
        painter.setPen(Qt::NoPen);
        if (vertical) {
            const QRectF track(rect.center().x() - 5.0, rect.top() + 18.0, 10.0, rect.height() - 36.0);
            painter.setBrush(sTrack);
            painter.drawRoundedRect(track, 5.0, 5.0);
            painter.setBrush(sAccent);
            const double fillH = track.height() * ratio;
            painter.drawRoundedRect(QRectF(track.left(), track.bottom() - fillH, track.width(), fillH), 5.0, 5.0);
            painter.setBrush(Qt::white);
            painter.drawEllipse(QPointF(track.center().x(), track.bottom() - fillH), 9.0, 9.0);
        } else {
            const double trackHeight = 10.0;
            const QRectF track(rect.left() + 18, rect.center().y() - trackHeight / 2.0, rect.width() - 36, trackHeight);
            painter.setBrush(sTrack);
            painter.drawRoundedRect(track, 5.0, 5.0);
            painter.setBrush(sAccent);
            painter.drawRoundedRect(QRectF(track.topLeft(), QSizeF(track.width() * ratio, track.height())), 5.0, 5.0);
            painter.setBrush(Qt::white);
            painter.drawEllipse(QPointF(track.left() + track.width() * ratio, track.center().y()), 9.0, 9.0);
        }
        painter.setPen(sInk);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        painter.drawText(rect.adjusted(12, 8, -12, -8), Qt::AlignCenter,
                         QString::number(control.value, 'f', 0));
    } else if (control.type == QStringLiteral("knob")) {
        const double minValue = control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
        const double maxValue = control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
        const double ratio = std::clamp((control.value - minValue) / std::max(1e-9, maxValue - minValue), 0.0, 1.0);
        const QColor kInk = propColor("textColor", QColor(QStringLiteral("#334155")));
        const QColor kAccent = propColor("accentColor", QColor(QStringLiteral("#3b82f6")));
        const QColor kTrack = propColor("trackColor", QColor(QStringLiteral("#e2e8f0")));
        const QPointF center(rect.center().x(), rect.top() + rect.height() * 0.46);
        const double radius = std::max(4.0, std::min(rect.width() / 2.0, rect.height() * 0.38));
        constexpr double kPi = 3.14159265358979323846;
        // 270° dial sweeping top (start 225°, clockwise span 270°).
        const double valueAngle = (225.0 - 270.0 * ratio) * kPi / 180.0;
        const QRectF arcBounds(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
        painter.setPen(QPen(kTrack, 4.0, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(arcBounds, 225 * 16, -270 * 16);
        const int activeSpan = int(-270.0 * 16 * ratio);
        if (activeSpan != 0) {
            painter.setPen(QPen(kAccent, 4.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(arcBounds, 225 * 16, activeSpan);
        }
        painter.setPen(QPen(kTrack.darker(130), 1.0));
        for (int i = 0; i <= 6; ++i) {
            const double a = (225.0 - 270.0 * i / 6.0) * kPi / 180.0;
            painter.drawLine(center + QPointF(radius * 0.80 * std::cos(a), -radius * 0.80 * std::sin(a)),
                             center + QPointF(radius * 0.94 * std::cos(a), -radius * 0.94 * std::sin(a)));
        }
        painter.setPen(QPen(Qt::white, 2.0));
        painter.setBrush(kAccent.lighter(150));
        painter.drawEllipse(center, radius * 0.78, radius * 0.78);
        const QPointF tip = center + QPointF(radius * 0.58 * std::cos(valueAngle), -radius * 0.58 * std::sin(valueAngle));
        painter.setPen(QPen(kAccent.darker(115), 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(center, tip);
        painter.setPen(kInk);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        painter.drawText(rect.adjusted(8, rect.height() * 0.66, -8, -4), Qt::AlignHCenter | Qt::AlignVCenter,
                         QString::number(control.value, 'f', 0));
    } else if (control.type == QStringLiteral("alarm")) {
        const QString severity = control.properties.value(QStringLiteral("severity")).toString(QStringLiteral("warn"));
        const QColor sevColor = severity == QStringLiteral("critical") ? QColor(QStringLiteral("#e5484d"))
                                : severity == QStringLiteral("info") ? QColor(QStringLiteral("#36c789"))
                                : QColor(QStringLiteral("#f5a524"));
        const QColor aBg = propColor("backgroundColor",
                                     QColor(sevColor.red(), sevColor.green(), sevColor.blue(), 30));
        const QColor aInk = propColor("textColor", QColor(QStringLiteral("#0f172a")));
        painter.setBrush(aBg);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rect, 6.0, 6.0);
        painter.setBrush(sevColor);
        painter.drawRoundedRect(QRectF(rect.left() + 10, rect.top() + 8, 4, rect.height() - 16), 2.0, 2.0);
        painter.drawEllipse(QPointF(rect.left() + 24, rect.center().y()), 6.0, 6.0);
        painter.setPen(aInk);
        painter.setFont(fitFont(0.28, 8, 18));
        const QString message = control.properties.value(QStringLiteral("message")).toString();
        painter.drawText(rect.adjusted(38, 0, -12, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         message.isEmpty() ? control.label : message);
    } else if (control.type == QStringLiteral("alarmList")) {
        const int maxRows = control.properties.value(QStringLiteral("maxRows")).toInt(10);
        const QRectF panel = rect.adjusted(8, 8, -8, -8);
        const QColor aBg = propColor("backgroundColor", QColor(Qt::white));
        const QColor aInk = propColor("textColor", QColor(QStringLiteral("#334155")));
        const QColor warn = propColor("warnColor", QColor(QStringLiteral("#f5a524")));
        painter.setBrush(aBg);
        painter.setPen(QPen(propColor("borderColor", QColor(QStringLiteral("#cbd5e1"))), 1.0));
        painter.drawRoundedRect(panel, 5.0, 5.0);
        const double pad = 6.0;
        const double avail = panel.height() - pad * 2.0;
        int rows = std::clamp(maxRows, 1, 12);
        if (avail / rows < 10.0) rows = std::max(1, int(avail / 10.0));
        const double rowH = std::max(10.0, avail / rows);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        for (int r = 0; r < rows; ++r) {
            const QRectF rowRect(panel.left() + pad, panel.top() + pad + r * rowH,
                                 panel.width() - pad * 2.0, rowH - 2.0);
            painter.setPen(Qt::NoPen);
            painter.setBrush(r == 0 ? warn : aBg);
            painter.drawRoundedRect(rowRect, 3.0, 3.0);
            painter.setPen(r == 0 ? QColor(Qt::white) : aInk);
            painter.drawText(rowRect.adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter,
                             r == 0 ? QStringLiteral("ALARM  过热") : QStringLiteral("INFO  运行正常"));
        }
    } else if (control.type == QStringLiteral("trend")) {
        const double minValue = control.properties.value(QStringLiteral("minValue")).toDouble(0.0);
        const double maxValue = control.properties.value(QStringLiteral("maxValue")).toDouble(100.0);
        const QColor tInk = propColor("textColor", QColor(QStringLiteral("#334155")));
        const QColor tAccent = propColor("accentColor", QColor(QStringLiteral("#3b82f6")));
        const QColor tGrid = propColor("gridColor", QColor(QStringLiteral("#e2e8f0")));
        const QRectF plot = rect.adjusted(12, 30, -12, -14);
        painter.setPen(QPen(tGrid, 1.0));
        for (int i = 1; i < 4; ++i) {
            const double y = plot.top() + plot.height() * i / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        QVector<QPointF> points;
        constexpr int kTrendSamples = 48;
        points.reserve(kTrendSamples);
        for (int i = 0; i < kTrendSamples; ++i) {
            const double t = static_cast<double>(i) / (kTrendSamples - 1);
            const double x = plot.left() + plot.width() * t;
            const double v = minValue + (maxValue - minValue)
                * (0.5 + 0.36 * std::sin(t * 18.0) * std::sin(t * 3.0)
                   + 0.1 * std::cos(t * 40.0));
            const double y = plot.bottom() - plot.height()
                * std::clamp((v - minValue) / std::max(1e-9, maxValue - minValue), 0.0, 1.0);
            points.append(QPointF(x, y));
        }
        if (points.size() >= 2) {
            QPainterPath area;
            area.moveTo(points.constFirst().x(), plot.bottom());
            appendSmoothPath(area, points);
            area.lineTo(points.constLast().x(), plot.bottom());
            area.closeSubpath();
            painter.setPen(Qt::NoPen);
            QColor areaFill = tAccent;
            areaFill.setAlpha(36);
            painter.setBrush(areaFill);
            painter.drawPath(area);
        }
        QPainterPath path;
        appendSmoothPath(path, points);
        painter.setPen(QPen(tAccent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        painter.setPen(tInk);
        painter.drawText(rect.adjusted(12, 8, -12, -8), Qt::AlignLeft | Qt::AlignTop,
                         QStringLiteral("%1 · %2s").arg(control.label,
                             QString::number(control.properties.value(QStringLiteral("timeSpan")).toInt())));
    } else if (control.type == QStringLiteral("table")) {
        const int rows = control.properties.value(QStringLiteral("rows")).toInt(5);
        const int columns = control.properties.value(QStringLiteral("columns")).toInt(4);
        const bool headerVisible = control.properties.value(QStringLiteral("headerVisible")).toBool(true);
        const QRectF table = rect.adjusted(8, 8, -8, -8);
        const int totalRows = std::max(1, rows + (headerVisible ? 1 : 0));
        const double rowHeight = table.height() / totalRows;
        const double colWidth = table.width() / std::max(1, columns);
        const QColor tInk = propColor("textColor", QColor(QStringLiteral("#334155")));
        const QColor tHeaderBg = propColor("accentColor", QColor(QStringLiteral("#2563eb")));
        const QColor tCellBg = propColor("backgroundColor", QColor(Qt::white));
        const QColor tGrid = propColor("gridColor", QColor(QStringLiteral("#e2e8f0")));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 7));
        for (int r = 0; r < totalRows; ++r) {
            for (int c = 0; c < columns; ++c) {
                const QRectF cell(table.left() + c * colWidth, table.top() + r * rowHeight, colWidth, rowHeight);
                const bool header = r == 0 && headerVisible;
                painter.setPen(QPen(tGrid, 1.0));
                painter.setBrush(header ? tHeaderBg : tCellBg);
                painter.drawRect(cell);
                painter.setPen(header ? QColor(Qt::white) : tInk);
                painter.drawText(cell.adjusted(3, 0, -3, 0), Qt::AlignCenter,
                                 header ? QStringLiteral("列 %1").arg(c + 1)
                                        : QStringLiteral("-%1-").arg((r + c) % 100));
            }
        }
    } else if (control.type == QStringLiteral("timeDisplay")) {
        QFont dFont(QStringLiteral("Bahnschrift"));
        dFont.setPixelSize(qMax(10, int(rect.height() * 0.5)));
        dFont.setWeight(QFont::DemiBold);
        painter.setFont(dFont);
        painter.setPen(propColor("textColor", QColor(QStringLiteral("#0f172a"))));
        const QString format = control.properties.value(QStringLiteral("format")).toString(QStringLiteral("HH:mm:ss"));
        painter.drawText(rect.adjusted(4, 0, -4, 0), Qt::AlignCenter, QTime::currentTime().toString(format));
    } else if (control.type == QStringLiteral("dateDisplay")) {
        const QString format = control.properties.value(QStringLiteral("format")).toString(QStringLiteral("yyyy-MM-dd"));
        QFont dFont(QStringLiteral("Segoe UI"));
        dFont.setPixelSize(qMax(9, int(rect.height() * 0.4)));
        dFont.setWeight(QFont::DemiBold);
        painter.setFont(dFont);
        painter.setPen(propColor("textColor", QColor(QStringLiteral("#0f172a"))));
        painter.drawText(rect.adjusted(4, 0, -4, 0), Qt::AlignCenter, QDate::currentDate().toString(format));
    } else if (control.type == QStringLiteral("multiLamp")) {
        const int states = std::clamp(control.properties.value(QStringLiteral("states")).toInt(3), 1, 8);
        const int active = std::clamp(control.properties.value(QStringLiteral("activeState")).toInt(0), 0, states - 1);
        const double spacing = rect.width() / (states + 1);
        const double lampR = std::max(4.0, std::min(9.0, rect.height() * 0.28));
        const QColor onColor = propColor("activeColor", QColor(QStringLiteral("#22c55e")));
        const QColor offColor = propColor("inactiveColor", QColor(QStringLiteral("#cbd5e1")));
        for (int i = 0; i < states; ++i) {
            const QPointF pos(rect.left() + spacing * (i + 1), rect.center().y());
            const bool on = i == active;
            painter.setPen(QPen(on ? onColor.lighter(140) : offColor.darker(120), 1.5));
            painter.setBrush(on ? onColor : offColor);
            painter.drawEllipse(pos, lampR, lampR);
            if (rect.height() >= 36) {
                painter.setPen(on ? onColor : offColor.darker(140));
                painter.setFont(QFont(QStringLiteral("Segoe UI"), 7));
                painter.drawText(QRectF(pos.x() - 14, pos.y() + lampR + 2, 28, 10), Qt::AlignHCenter,
                                 QString::number(i + 1));
            }
        }
    } else {
        painter.setPen(propColor("textColor", QColor(QStringLiteral("#334155"))));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 10, QFont::DemiBold));
        painter.drawText(rect.adjusted(12, 0, -12, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         control.label);
        if (control.type == QStringLiteral("animation")) {
            painter.setPen(QPen(propColor("accentColor", QColor(QStringLiteral("#3b82f6"))), 1.4));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QRectF(rect.right() - 28, rect.center().y() - 5, 10, 10));
        }
    }

    painter.restore();
}

void DesignerCanvas::drawDeviceChrome(QPainter &painter, const QRectF &bounds) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#b6c2cc")), 1.0));
    painter.setBrush(QColor(QStringLiteral("#dce3e8")));
    painter.drawRoundedRect(bounds.adjusted(-10, -10, 10, 10), 10.0, 10.0);
    painter.setPen(QPen(QColor(QStringLiteral("#93a4b0")), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds.adjusted(-3, -3, 3, 3), 5.0, 5.0);
    painter.restore();
}

void DesignerCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(QStringLiteral("#ffffff")));

    const QRectF bounds = deviceRect();
    drawDeviceChrome(painter, bounds);

    painter.save();
    painter.setClipRect(bounds);
    drawPageBackground(painter, bounds);
    painter.setPen(QPen(QColor(QStringLiteral("#d8e0e8")), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds, 5.0, 5.0);

    const double scale = bounds.width() / effectiveSize().width();
    painter.translate(bounds.center());
    painter.scale(scale, scale);
    painter.rotate(double(m_rotation));
    painter.translate(-m_deviceSize.width() / 2.0, -m_deviceSize.height() / 2.0);

    if (m_gridVisible && !m_previewMode) {
        painter.setPen(QPen(QColor(200, 210, 222, 110), 1.0));
        for (double x = 0.0; x <= m_deviceSize.width(); x += SnapGrid) {
            painter.drawLine(QPointF(x, 0.0), QPointF(x, m_deviceSize.height()));
        }
        for (double y = 0.0; y <= m_deviceSize.height(); y += SnapGrid) {
            painter.drawLine(QPointF(0.0, y), QPointF(m_deviceSize.width(), y));
        }
    }

    painter.setPen(QColor(QStringLiteral("#98a3b0")));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
    painter.drawText(QRectF(18, 14, m_deviceSize.width() - 36, 20),
                     Qt::AlignRight | Qt::AlignTop,
                     QStringLiteral("Y-HMI  /  %1").arg(m_pageIndex == 0 ? QStringLiteral("HOME")
                                                                           : m_pageIndex == 1 ? QStringLiteral("PARAMETERS")
                                                                                              : QStringLiteral("EVENTS")));

    for (int index = 0; index < m_controls.size(); ++index) {
        const Control &control = m_controls.at(index);
        if (m_previewMode && !control.visible) continue;
        drawControl(painter, control, index == m_selectedIndex);
    }

    if (m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
        && !m_previewMode && !m_placing) {
        drawSelectionOverlay(painter, m_controls.at(m_selectedIndex));
    }
    if (m_placing) {
        drawPlacingGhost(painter);
    }
    painter.restore();

    painter.setPen(QColor(QStringLiteral("#98a3b0")));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
    painter.drawText(QRectF(28, height() - 26, width() - 56, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     m_previewMode ? tr("预览 · 使用模拟值，不连接设备")
                                   : m_placing
                                         ? tr("放置 %1 · 左键放置，右键取消").arg(controlLabel(m_placingType))
                                         : tr("设计画布 · %1 × %2 · 旋转 %3°")
                                               .arg(m_deviceSize.width())
                                               .arg(m_deviceSize.height())
                                               .arg(m_rotation));

    if (m_resizing && m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()) {
        const QPointF center = toCanvas(m_controls.at(m_selectedIndex).rect.center());
        const QString label = tr("W: %1 × H: %2")
                                  .arg(qRound(m_controls.at(m_selectedIndex).rect.width()))
                                  .arg(qRound(m_controls.at(m_selectedIndex).rect.height()));
        QFont badgeFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold);
        QFontMetrics metrics(badgeFont);
        const int bw = metrics.horizontalAdvance(label) + 14;
        const QRectF badge(center.x() - bw / 2.0, center.y() - 44, double(bw), 24.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(24, 35, 51, 235));
        painter.drawRoundedRect(badge, 5.0, 5.0);
        painter.setPen(Qt::white);
        painter.setFont(badgeFont);
        painter.drawText(badge, Qt::AlignCenter, label);
    }
}

void DesignerCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_placing) {
        if (event->button() == Qt::RightButton) {
            cancelPlacing();
            return;
        }
        if (event->button() != Qt::LeftButton) return;
        const QPointF devicePoint = toDevice(event->position());
        if (devicePoint.x() < 0.0 || devicePoint.y() < 0.0
            || devicePoint.x() > m_deviceSize.width()
            || devicePoint.y() > m_deviceSize.height()) {
            cancelPlacing();
            return;
        }
        placeControlAt(m_placingType, m_placingLabel, devicePoint);
        return;
    }

    if (event->button() != Qt::LeftButton) return;
    const QPointF devicePoint = toDevice(event->position());

    if (!m_previewMode && m_selectedIndex >= 0
        && m_selectedIndex < m_controls.size()
        && !m_controls.at(m_selectedIndex).locked) {
        const int resizeHandle = hitResizeHandle(event->position());
        if (resizeHandle >= 0) {
            m_resizing = true;
            m_resizeHandle = resizeHandle;
            m_resizeStartRect = m_controls.at(m_selectedIndex).rect;
            m_resizeStartPoint = devicePoint;
            grabMouse();
            update();
            return;
        }
    }

    const int hit = hitTest(devicePoint);
    if (hit < 0) {
        m_selectedIndex = -1;
        emitSelection();
        update();
        return;
    }
    m_selectedIndex = hit;
    emitSelection();
    if (m_previewMode) {
        if (m_controls.at(hit).interactive) {
            Control &control = m_controls[hit];
            if (control.type == QStringLiteral("button")
                && control.eventType == QStringLiteral("gotoPage")) {
                const QString targetPageId = control.eventTarget;
                emit previewNavigationRequested(targetPageId);
                emit statusMessage(tr("预览事件：跳转到 %1").arg(targetPageId));
            } else if (control.type == QStringLiteral("switch")) {
                control.value = control.value >= 0.5 ? 0.0 : 1.0;
                emitSelection();
                emit statusMessage(control.value >= 0.5 ? tr("预览事件：开关已打开")
                                                        : tr("预览事件：开关已关闭"));
            } else if (control.type == QStringLiteral("text-input")) {
                emit textInputRequested(control.instanceId);
                emit statusMessage(tr("预览输入：请在宿主对话框中提交文本"));
            } else if (control.eventType == QStringLiteral("command")) {
                emit statusMessage(tr("预览事件：命令 %1（仅模拟）").arg(control.eventTarget));
            } else {
                emit statusMessage(tr("预览事件：%1 未配置动作").arg(control.label));
            }
            update();
        } else {
            emit statusMessage(tr("预览仅验证布局和模拟值；当前控件未配置可执行动作"));
        }
        return;
    }
    if (m_controls.at(hit).locked) {
        emit statusMessage(tr("控件 %1 已锁定").arg(m_controls.at(hit).label));
        return;
    }
    m_dragging = true;
    m_pressDevicePoint = devicePoint;
    m_pressRect = m_controls.at(hit).rect;
    grabMouse();
    update();
}

void DesignerCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_placing) {
        m_cursorPoint = toDevice(event->position());
        m_hasCursor = true;
        update();
        return;
    }
    if (m_resizing && m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()) {
        const QPointF devicePoint = toDevice(event->position());
        m_controls[m_selectedIndex].rect =
            resizeRectForHandle(m_resizeStartRect, m_resizeStartPoint,
                                devicePoint, m_resizeHandle);
        emit statusMessage(tr("缩放 %1 · 已吸附到 8px 网格")
                               .arg(m_controls.at(m_selectedIndex).label));
        update();
        return;
    }
    if (!m_dragging) {
        if (!m_previewMode && m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()
            && !m_controls.at(m_selectedIndex).locked) {
            const int handle = hitResizeHandle(event->position());
            if (handle == 0 || handle == 4) setCursor(Qt::SizeFDiagCursor);
            else if (handle == 2 || handle == 6) setCursor(Qt::SizeBDiagCursor);
            else if (handle == 1 || handle == 5) setCursor(Qt::SizeVerCursor);
            else if (handle == 3 || handle == 7) setCursor(Qt::SizeHorCursor);
            else unsetCursor();
        } else {
            unsetCursor();
        }
        return;
    }
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return;
    const QPointF delta = toDevice(event->position()) - m_pressDevicePoint;
    Control &control = m_controls[m_selectedIndex];
    QPointF next = m_pressRect.topLeft() + delta;
    next.setX(std::round(next.x() / SnapGrid) * SnapGrid);
    next.setY(std::round(next.y() / SnapGrid) * SnapGrid);
    next.setX(std::clamp(next.x(), 0.0, m_deviceSize.width() - control.rect.width()));
    next.setY(std::clamp(next.y(), 0.0, m_deviceSize.height() - control.rect.height()));
    control.rect.moveTopLeft(next);
    emit statusMessage(tr("移动 %1 · 已吸附到 8px 网格").arg(control.label));
    update();
}

void DesignerCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_resizing) {
        m_resizing = false;
        m_resizeHandle = -1;
        releaseMouse();
        if (m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()) {
            emitSelection();
            emit statusMessage(tr("已提交 %1 的尺寸").arg(m_controls.at(m_selectedIndex).label));
            emit documentChanged();
        }
        update();
        return;
    }
    if (!m_dragging) return;
    m_dragging = false;
    releaseMouse();
    if (m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()) {
        emitSelection();
        emit statusMessage(tr("已提交 %1 的位置").arg(m_controls.at(m_selectedIndex).label));
        emit documentChanged();
    }
}

void DesignerCanvas::dragEnterEvent(QDragEnterEvent *event)
{
    if (!m_previewMode && event->mimeData()->hasFormat(
            QStringLiteral("application/x-serein-component"))) {
        const QString id = QString::fromUtf8(event->mimeData()->data(
            QStringLiteral("application/x-serein-component"))).trimmed();
        if (!definitionIdForType(id).isEmpty()) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void DesignerCanvas::dropEvent(QDropEvent *event)
{
    if (m_previewMode || !event->mimeData()->hasFormat(
            QStringLiteral("application/x-serein-component"))) {
        event->ignore();
        return;
    }
    const QString id = QString::fromUtf8(event->mimeData()->data(
        QStringLiteral("application/x-serein-component"))).trimmed();
    if (definitionIdForType(id).isEmpty()) {
        emit statusMessage(tr("当前版本不支持该控件类型"));
        event->ignore();
        return;
    }
    placeControlAt(id, controlLabel(id), toDevice(event->position()));
    event->acceptProposedAction();
}

void DesignerCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_previewMode || m_placing) return;
    if (event->button() != Qt::LeftButton) return;
    const QPointF devicePoint = toDevice(event->position());
    const int hit = hitTest(devicePoint);
    if (hit < 0) return;
    m_selectedIndex = hit;
    emitSelection();
    Control &control = m_controls[hit];
    const bool textType = control.type == QStringLiteral("text")
        || control.type == QStringLiteral("button")
        || control.type == QStringLiteral("text-input");
    const bool numericType = control.type == QStringLiteral("number")
        || control.type == QStringLiteral("progress")
        || control.type == QStringLiteral("status")
        || control.type == QStringLiteral("gauge")
        || control.type == QStringLiteral("slider")
        || control.type == QStringLiteral("knob")
        || control.type == QStringLiteral("sevenSegment");
    if (textType) {
        bool accepted = false;
        const QString value = QInputDialog::getText(
            this, tr("编辑文本"), tr("文本内容"), QLineEdit::Normal,
            control.textValue, &accepted);
        if (accepted && value != control.textValue) {
            control.textValue = value;
            emitSelection();
            emit documentChanged();
            update();
        }
    } else if (control.type == QStringLiteral("number") && control.numeric) {
        bool accepted = false;
        const double value = QInputDialog::getDouble(
            this, tr("编辑数值"), tr("模拟值"), control.numeric->state().previewValue,
            -1e9, 1e9, 2, &accepted);
        if (accepted) {
            control.numeric->setPreviewValue(value);
            control.value = value;
            emitSelection();
            emit documentChanged();
            update();
        }
    } else if (numericType) {
        bool accepted = false;
        const double value = QInputDialog::getDouble(
            this, tr("编辑数值"), tr("模拟值"), control.value, -1e9, 1e9, 2, &accepted);
        if (accepted) {
            control.value = value;
            emitSelection();
            emit documentChanged();
            update();
        }
    } else if (control.type == QStringLiteral("switch")) {
        control.value = control.value >= 0.5 ? 0.0 : 1.0;
        emitSelection();
        emit documentChanged();
        update();
    } else {
        emit statusMessage(tr("双击 %1：该控件暂不支持就地编辑").arg(control.label));
    }
}

void DesignerCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_placing) {
            cancelPlacing();
            event->accept();
            return;
        }
        if (m_selectedIndex >= 0) {
            m_selectedIndex = -1;
            emitSelection();
            update();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void DesignerCanvas::enterEvent(QEnterEvent *event)
{
    if (m_placing) {
        m_cursorPoint = toDevice(QPointF(mapFromGlobal(QCursor::pos())));
        m_hasCursor = true;
        update();
    }
    QWidget::enterEvent(event);
}

void DesignerCanvas::leaveEvent(QEvent *event)
{
    m_hasCursor = false;
    unsetCursor();
    if (m_placing) update();
    QWidget::leaveEvent(event);
}

} // namespace serein::scope::yserein_hmi
