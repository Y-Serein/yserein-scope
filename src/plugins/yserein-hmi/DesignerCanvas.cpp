#include "DesignerCanvas.h"

#include <QtCore/QSizeF>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QMimeData>
#include <QtCore/QSet>
#include <QtCore/QUuid>
#include <QtGui/QFont>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPaintEvent>
#include <QtGui/QPen>

#include <algorithm>
#include <cmath>
#include <utility>

namespace serein::scope::yserein_hmi {
namespace {

constexpr QSizeF DeviceSize(800.0, 480.0);
constexpr double SnapGrid = 8.0;

QColor blend(const QColor &color, const QColor &background, const double amount)
{
    const double inverse = 1.0 - amount;
    return QColor::fromRgbF(color.redF() * amount + background.redF() * inverse,
                            color.greenF() * amount + background.greenF() * inverse,
                            color.blueF() * amount + background.blueF() * inverse,
                            color.alphaF() * amount + background.alphaF() * inverse);
}

QString controlLabel(const QString &type)
{
    if (type == QStringLiteral("number")) return QStringLiteral("温度");
    return QString{};
}

QString stableId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString normalizedPageName(const QString &name)
{
    return name.trimmed().left(32);
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
    m_pageIds = {stableId(), stableId(), stableId()};
    m_pageNames = {tr("首页"), tr("参数"), tr("告警")};
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
            if (!control.numeric) continue;
            const NumericDisplayControl::State &numeric = control.numeric->state();
            const QJsonObject geometry{
                {QStringLiteral("x"), control.rect.x()},
                {QStringLiteral("y"), control.rect.y()},
                {QStringLiteral("width"), control.rect.width()},
                {QStringLiteral("height"), control.rect.height()},
            };
            const QJsonObject properties{
                {QStringLiteral("binding"), numeric.binding},
                {QStringLiteral("previewValue"), numeric.previewValue},
                {QStringLiteral("decimals"), numeric.decimals},
                {QStringLiteral("unit"), numeric.unit},
                {QStringLiteral("fontPixelSize"), numeric.fontPixelSize},
                {QStringLiteral("alignment"), numeric.alignmentIndex},
            };
            serializedControls.append(QJsonObject{
                {QStringLiteral("instanceId"), control.instanceId},
                {QStringLiteral("definitionId"),
                 QStringLiteral("yserein-hmi.numeric-display")},
                {QStringLiteral("name"), numeric.id},
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
    };
}

bool DesignerCanvas::restorePageState(const QJsonObject &state, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    const QJsonValue pagesValue = state.value(QStringLiteral("pages"));
    if (!pagesValue.isArray() || pagesValue.toArray().isEmpty()) {
        return fail(tr("工程至少需要一个页面"));
    }

    QStringList pageIds;
    QStringList pageNames;
    QVector<QVector<Control>> pages;
    QSet<QString> uniquePageIds;
    const QJsonArray serializedPages = pagesValue.toArray();
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
            if (definitionId != QStringLiteral("yserein-hmi.numeric-display")) {
                return fail(tr("当前版本无法加载控件定义：%1").arg(definitionId));
            }
            const QString instanceId = controlObject
                .value(QStringLiteral("instanceId")).toString().trimmed();
            if (instanceId.isEmpty() || instanceIds.contains(instanceId)) {
                return fail(tr("控件实例 ID 缺失或重复"));
            }
            const QJsonObject geometry = controlObject
                .value(QStringLiteral("geometry")).toObject();
            const QRectF rect(geometry.value(QStringLiteral("x")).toDouble(),
                              geometry.value(QStringLiteral("y")).toDouble(),
                              geometry.value(QStringLiteral("width")).toDouble(),
                              geometry.value(QStringLiteral("height")).toDouble());
            if (!rect.isValid() || rect.left() < 0.0 || rect.top() < 0.0
                || rect.right() > DeviceSize.width()
                || rect.bottom() > DeviceSize.height()) {
                return fail(tr("控件几何超出 800 × 480 设计画布"));
            }

            const QJsonObject properties = controlObject
                .value(QStringLiteral("properties")).toObject();
            NumericDisplayControl numeric;
            const QString name = controlObject.value(QStringLiteral("name"))
                                     .toString().trimmed();
            const QString binding = properties.value(QStringLiteral("binding"))
                                        .toString().trimmed();
            if (!numeric.setId(name) || names.contains(name)
                || !numeric.setBinding(binding)
                || !numeric.setPreviewValue(
                    properties.value(QStringLiteral("previewValue")).toDouble())) {
                return fail(tr("数值显示的名称、绑定或模拟值无效"));
            }
            numeric.setDecimals(properties.value(QStringLiteral("decimals")).toInt(1));
            numeric.setUnit(properties.value(QStringLiteral("unit")).toString());
            numeric.setFontPixelSize(
                properties.value(QStringLiteral("fontPixelSize")).toInt(32));
            numeric.setAlignmentIndex(
                properties.value(QStringLiteral("alignment")).toInt(0));

            Control control{
                .instanceId = instanceId,
                .id = numeric.state().id,
                .type = QStringLiteral("number"),
                .label = controlObject.value(QStringLiteral("label")).toString(),
                .binding = numeric.state().binding,
                .rect = rect,
                .fill = QColor(QStringLiteral("#203346")),
                .text = QColor(QStringLiteral("#f2c96b")),
                .interactive = false,
                .numeric = std::move(numeric),
            };
            if (control.label.isEmpty()) control.label = controlLabel(control.type);
            controls.append(std::move(control));
            instanceIds.insert(instanceId);
            names.insert(name);
        }
        uniquePageIds.insert(pageId);
        pageIds.append(pageId);
        pageNames.append(pageName);
        pages.append(std::move(controls));
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

void DesignerCanvas::setPreviewMode(const bool preview)
{
    if (m_previewMode == preview) return;
    m_previewMode = preview;
    m_dragging = false;
    if (m_previewMode) clearFocus();
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
    for (Control &control : copy) control.instanceId = stableId();
    const int insertIndex = pageIndex + 1;
    m_pageIds.insert(insertIndex, stableId());
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
    if (pageIndex < 0 || pageIndex >= m_pageNames.size()) return false;
    if (m_pageNames.size() <= 1) {
        emit statusMessage(tr("工程至少需要保留一个页面"));
        return false;
    }
    if (m_pageIndex >= 0 && m_pageIndex < m_pageControls.size()) {
        m_pageControls[m_pageIndex] = m_controls;
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
    if (type != QStringLiteral("number")) {
        emit statusMessage(tr("P0 仅开放数值显示控件；其他控件将在闭环后继续实现"));
        return;
    }
    QString id;
    do {
        id = QStringLiteral("number_%1")
                 .arg(m_nextControlNumber++, 2, 10, QLatin1Char('0'));
    } while (std::any_of(m_controls.cbegin(), m_controls.cend(),
                         [&id](const Control &control) { return control.id == id; }));
    const int column = m_controls.size() % 3;
    const int row = m_controls.size() / 3;
    Control control{
        .instanceId = stableId(),
        .id = id,
        .type = type,
        .label = label.isEmpty() ? controlLabel(type) : label,
        .binding = QStringLiteral("telemetry.value"),
        .rect = QRectF(80.0 + column * 180.0, 140.0 + row * 72.0, 150.0, 48.0),
        .fill = QColor(QStringLiteral("#203346")),
        .text = QColor(QStringLiteral("#f2c96b")),
        .interactive = false,
    };
    NumericDisplayControl::State state;
    state.id = id;
    state.binding = control.binding;
    state.previewValue = 0.0;
    state.decimals = 1;
    state.unit = QStringLiteral("°C");
    state.fontPixelSize = 32;
    control.numeric.emplace(std::move(state));
    m_controls.append(control);
    m_selectedIndex = m_controls.size() - 1;
    emitSelection();
    emit statusMessage(tr("已添加数值显示控件 %1").arg(id));
    emit documentChanged();
    update();
}

void DesignerCanvas::alignSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return;
    Control &control = m_controls[m_selectedIndex];
    control.rect.moveTopLeft(QPointF(std::round(control.rect.x() / SnapGrid) * SnapGrid,
                                     std::round(control.rect.y() / SnapGrid) * SnapGrid));
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
    if (m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return;
    if (!selectedNumericControl()) {
        emit statusMessage(tr("当前只允许删除已实现的数值显示控件"));
        return;
    }
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
    NumericDisplayControl *numeric = selectedNumericControl();
    if (!numeric) return false;
    const QString candidate = id.trimmed();
    for (int index = 0; index < m_controls.size(); ++index) {
        if (index != m_selectedIndex && m_controls.at(index).id == candidate) return false;
    }
    if (!numeric->setId(candidate)) return false;
    m_controls[m_selectedIndex].id = numeric->state().id;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedNumericBinding(const QString &binding)
{
    NumericDisplayControl *numeric = selectedNumericControl();
    if (!numeric || !numeric->setBinding(binding)) return false;
    m_controls[m_selectedIndex].binding = numeric->state().binding;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

bool DesignerCanvas::setSelectedNumericPreviewValue(const double value)
{
    NumericDisplayControl *numeric = selectedNumericControl();
    if (!numeric || !numeric->setPreviewValue(value)) return false;
    emitSelection();
    emit documentChanged();
    update();
    return true;
}

void DesignerCanvas::setSelectedNumericDecimals(const int decimals)
{
    if (NumericDisplayControl *numeric = selectedNumericControl()) {
        numeric->setDecimals(decimals);
        emitSelection();
        emit documentChanged();
        update();
    }
}

void DesignerCanvas::setSelectedNumericUnit(const QString &unit)
{
    if (NumericDisplayControl *numeric = selectedNumericControl()) {
        numeric->setUnit(unit);
        emitSelection();
        emit documentChanged();
        update();
    }
}

void DesignerCanvas::setSelectedNumericFontPixelSize(const int pixelSize)
{
    if (NumericDisplayControl *numeric = selectedNumericControl()) {
        numeric->setFontPixelSize(pixelSize);
        emitSelection();
        emit documentChanged();
        update();
    }
}

void DesignerCanvas::setSelectedNumericAlignment(const int alignmentIndex)
{
    if (NumericDisplayControl *numeric = selectedNumericControl()) {
        numeric->setAlignmentIndex(alignmentIndex);
        emitSelection();
        emit documentChanged();
        update();
    }
}

QRectF DesignerCanvas::deviceRect() const
{
    const QRectF available = QRectF(rect()).adjusted(34.0, 32.0, -34.0, -34.0);
    const double scale = std::min(available.width() / DeviceSize.width(),
                                  available.height() / DeviceSize.height());
    const QSizeF size = DeviceSize * std::max(0.1, scale);
    return QRectF(available.center() - QPointF(size.width() / 2.0, size.height() / 2.0), size);
}

QPointF DesignerCanvas::toDevice(const QPointF &point) const
{
    const QRectF bounds = deviceRect();
    const double scale = bounds.width() / DeviceSize.width();
    return (point - bounds.topLeft()) / scale;
}

QPointF DesignerCanvas::toCanvas(const QPointF &point) const
{
    const QRectF bounds = deviceRect();
    const double scale = bounds.width() / DeviceSize.width();
    return bounds.topLeft() + point * scale;
}

int DesignerCanvas::hitTest(const QPointF &devicePoint) const
{
    for (int index = m_controls.size() - 1; index >= 0; --index) {
        if (m_controls.at(index).rect.contains(devicePoint)) return index;
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
            .rect = geometry,
            .fill = QColor(QStringLiteral("#203346")),
            .text = QColor(QStringLiteral("#f2c96b")),
            .interactive = false,
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
    if (const NumericDisplayControl *numeric = selectedNumericControl()) {
        const NumericDisplayControl::State &state = numeric->state();
        emit numericPropertiesChanged(state.id, state.binding, state.previewValue,
                                      state.decimals, state.unit, state.fontPixelSize,
                                      state.alignmentIndex);
    }
}

void DesignerCanvas::drawControl(QPainter &painter, const Control &control,
                                 const bool selected) const
{
    const QRectF rect = QRectF(toCanvas(control.rect.topLeft()),
                               control.rect.size() * (deviceRect().width() / DeviceSize.width()));
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(control.fill);
    painter.drawRoundedRect(rect, 6.0, 6.0);

    if (control.type == QStringLiteral("number")) {
        const NumericDisplayControl *numeric = control.numeric ? &*control.numeric : nullptr;
        const double scale = deviceRect().width() / DeviceSize.width();
        QFont valueFont(QStringLiteral("Bahnschrift"));
        valueFont.setPixelSize(std::clamp(
            int(std::round((numeric ? numeric->state().fontPixelSize : 32) * scale)),
            12, 56));
        valueFont.setWeight(QFont::DemiBold);
        painter.setPen(control.text);
        painter.setFont(valueFont);
        painter.drawText(rect.adjusted(14, 7, -14, -24),
                         numeric ? numeric->alignment()
                                 : Qt::AlignLeft | Qt::AlignVCenter,
                         numeric ? numeric->formattedValue() : control.label);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        painter.setPen(blend(control.text, control.fill, 0.66));
        painter.drawText(rect.adjusted(14, 36, -14, -5),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         numeric ? numeric->state().binding : control.binding);
    } else if (control.type == QStringLiteral("chart")) {
        painter.setPen(QPen(QColor(QStringLiteral("#35546a")), 1.0));
        for (int i = 1; i < 4; ++i) {
            const double y = rect.top() + rect.height() * i / 4.0;
            painter.drawLine(QPointF(rect.left() + 12, y), QPointF(rect.right() - 12, y));
        }
        QPainterPath path;
        path.moveTo(rect.left() + 14, rect.bottom() - 24);
        path.cubicTo(rect.left() + rect.width() * 0.28, rect.top() + rect.height() * 0.58,
                     rect.left() + rect.width() * 0.48, rect.top() + rect.height() * 0.72,
                     rect.left() + rect.width() * 0.64, rect.top() + rect.height() * 0.36);
        path.cubicTo(rect.left() + rect.width() * 0.74, rect.top() + rect.height() * 0.18,
                     rect.left() + rect.width() * 0.86, rect.top() + rect.height() * 0.52,
                     rect.right() - 14, rect.top() + rect.height() * 0.28);
        painter.setPen(QPen(control.text, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        painter.drawText(rect.adjusted(12, 8, -12, -8), Qt::AlignLeft | Qt::AlignTop,
                         control.label);
    } else if (control.type == QStringLiteral("progress")) {
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        painter.setPen(control.text);
        painter.drawText(rect.adjusted(12, 0, -12, -4), Qt::AlignLeft | Qt::AlignVCenter,
                         control.label);
        const QRectF track = rect.adjusted(112, 13, -14, -13);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#102431")));
        painter.drawRoundedRect(track, 4.0, 4.0);
        painter.setBrush(QColor(QStringLiteral("#55c4ad")));
        painter.drawRoundedRect(QRectF(track.topLeft(), QSizeF(track.width() * 0.62, track.height())),
                                4.0, 4.0);
    } else {
        painter.setPen(control.text);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), control.type == QStringLiteral("text") ? 11 : 10,
                              QFont::DemiBold));
        painter.drawText(rect.adjusted(12, 0, -12, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         control.label);
        if (control.type == QStringLiteral("animation")) {
            painter.setPen(QPen(control.text, 1.4));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QRectF(rect.right() - 28, rect.center().y() - 5, 10, 10));
        }
    }

    if (selected && !m_previewMode) {
        painter.setPen(QPen(QColor(QStringLiteral("#6ea3ff")), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect.adjusted(-3, -3, 3, 3), 8.0, 8.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#ffffff")));
        for (const QPointF &point : {rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight()}) {
            painter.drawRoundedRect(QRectF(point - QPointF(3.0, 3.0), QSizeF(6.0, 6.0)), 1.5, 1.5);
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
    painter.fillRect(rect(), QColor(QStringLiteral("#eef1f4")));

    const QRectF bounds = deviceRect();
    drawDeviceChrome(painter, bounds);

    painter.save();
    painter.setClipRect(bounds);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#102330")));
    painter.drawRoundedRect(bounds, 5.0, 5.0);

    if (m_gridVisible && !m_previewMode) {
        const double scale = bounds.width() / DeviceSize.width();
        painter.setPen(QPen(QColor(120, 170, 190, 24), 1.0));
        for (double x = 0; x <= DeviceSize.width(); x += SnapGrid) {
            const double canvasX = bounds.left() + x * scale;
            painter.drawLine(QPointF(canvasX, bounds.top()), QPointF(canvasX, bounds.bottom()));
        }
        for (double y = 0; y <= DeviceSize.height(); y += SnapGrid) {
            const double canvasY = bounds.top() + y * scale;
            painter.drawLine(QPointF(bounds.left(), canvasY), QPointF(bounds.right(), canvasY));
        }
    }

    painter.setPen(QColor(QStringLiteral("#7690a2")));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
    painter.drawText(bounds.adjusted(18, 14, -18, -14), Qt::AlignRight | Qt::AlignTop,
                     QStringLiteral("Y-HMI  /  %1").arg(m_pageIndex == 0 ? QStringLiteral("HOME")
                                                                           : m_pageIndex == 1 ? QStringLiteral("PARAMETERS")
                                                                                              : QStringLiteral("EVENTS")));
    for (int index = 0; index < m_controls.size(); ++index) {
        Control control = m_controls.at(index);
        drawControl(painter, control, index == m_selectedIndex);
    }
    painter.restore();

    painter.setPen(QColor(QStringLiteral("#7f8d98")));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
    painter.drawText(QRectF(28, height() - 26, width() - 56, 18),
                     Qt::AlignLeft | Qt::AlignVCenter,
                      m_previewMode ? tr("预览 · 使用模拟值，不连接设备")
                                    : tr("设计画布 · 800 × 480 · 网格 8px"));
}

void DesignerCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    const QPointF devicePoint = toDevice(event->position());
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
            m_running = !m_running;
            emit statusMessage(m_running ? tr("运行态：%1 已触发").arg(m_controls.at(hit).label)
                                         : tr("运行态：%1 已停止").arg(m_controls.at(hit).label));
            update();
        } else {
            emit statusMessage(tr("预览仅验证布局和数值格式；交互控件尚未接入"));
        }
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
    if (!m_dragging || m_selectedIndex < 0 || m_selectedIndex >= m_controls.size()) return;
    const QPointF delta = toDevice(event->position()) - m_pressDevicePoint;
    Control &control = m_controls[m_selectedIndex];
    QPointF next = m_pressRect.topLeft() + delta;
    next.setX(std::round(next.x() / SnapGrid) * SnapGrid);
    next.setY(std::round(next.y() / SnapGrid) * SnapGrid);
    next.setX(std::clamp(next.x(), 0.0, DeviceSize.width() - control.rect.width()));
    next.setY(std::clamp(next.y(), 0.0, DeviceSize.height() - control.rect.height()));
    control.rect.moveTopLeft(next);
    emit statusMessage(tr("移动 %1 · 已吸附到 8px 网格").arg(control.label));
    update();
}

void DesignerCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging) return;
    m_dragging = false;
    releaseMouse();
    if (m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()) {
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
        if (id == QStringLiteral("number")) {
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
    if (id != QStringLiteral("number")) {
        emit statusMessage(tr("P0 仅开放数值显示控件；其他控件将在闭环后继续实现"));
        event->ignore();
        return;
    }
    addControl(id, tr("数值显示"));
    if (m_selectedIndex >= 0 && m_selectedIndex < m_controls.size()) {
        Control &control = m_controls[m_selectedIndex];
        QPointF position = toDevice(event->position())
            - QPointF(control.rect.width() / 2.0, control.rect.height() / 2.0);
        position.setX(std::round(position.x() / SnapGrid) * SnapGrid);
        position.setY(std::round(position.y() / SnapGrid) * SnapGrid);
        position.setX(std::clamp(position.x(), 0.0,
                                 DeviceSize.width() - control.rect.width()));
        position.setY(std::clamp(position.y(), 0.0,
                                 DeviceSize.height() - control.rect.height()));
        control.rect.moveTopLeft(position);
        emit documentChanged();
        update();
    }
    event->acceptProposedAction();
}

} // namespace serein::scope::yserein_hmi
