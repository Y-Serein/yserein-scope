#include <serein/scope/WorkspaceCanvas.h>

#include <QtCore/QEvent>
#include <QtCore/QHash>
#include <QtCore/QLine>
#include <QtCore/QPointer>
#include <QtCore/QSettings>
#include <QtCore/QVector>
#include <QtGui/QAction>
#include <QtGui/QEnterEvent>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QIcon>
#include <QtGui/QKeyEvent>
#include <QtCore/QMimeData>
#include <QtGui/QMouseEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPaintEvent>
#include <QtGui/QPixmap>
#include <QtGui/QRegion>
#include <QtGui/QResizeEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QSizePolicy>

#include <algorithm>
#include <cmath>
#include <utility>

namespace serein::scope {
namespace {

constexpr int LayoutVersion = 1;
constexpr int ResizeHitWidth = 7;
constexpr int SnapGrid = 8;
constexpr int AlignmentTolerance = 4;
constexpr qreal ComponentRadius = 8.0;
constexpr qreal ComponentInnerRadius = ComponentRadius - 1.0;

class RoundedBottomClip final : public QWidget {
public:
    explicit RoundedBottomClip(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("componentContentClip"));
    }

    void setClipTopCorners(const bool enabled)
    {
        if (m_clipTopCorners == enabled) return;
        m_clipTopCorners = enabled;
        updateMask();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateMask();
    }

private:
    void updateMask()
    {
        if (width() <= 0 || height() <= 0) return;

        const qreal right = width();
        const qreal bottom = height();
        const qreal radius = std::min<qreal>(
            ComponentInnerRadius, std::min(right, bottom) / 2.0);
        QPainterPath path;
        path.moveTo(m_clipTopCorners ? radius : 0.0, 0.0);
        path.lineTo(m_clipTopCorners ? right - radius : right, 0.0);
        if (m_clipTopCorners) {
            path.quadTo(right, 0.0, right, radius);
        }
        path.lineTo(right, bottom - radius);
        path.quadTo(right, bottom, right - radius, bottom);
        path.lineTo(radius, bottom);
        path.quadTo(0.0, bottom, 0.0, bottom - radius);
        if (m_clipTopCorners) {
            path.lineTo(0.0, radius);
            path.quadTo(0.0, 0.0, radius, 0.0);
        }
        path.closeSubpath();
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    bool m_clipTopCorners = false;
};

enum class ChromeIcon {
    More,
};

QIcon makeChromeIcon(const ChromeIcon kind)
{
    Q_UNUSED(kind)
    QPixmap pixmap(40, 40);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#667085")));
    // Keep the three-dot affordance unmistakable at the 18–20px chrome size.
    // The previous 2.2px span was rendered as a faint square in PrintWindow
    // captures on Windows fractional-DPI configurations.
    painter.drawEllipse(QPointF(6.5, 10.0), 1.15, 1.15);
    painter.drawEllipse(QPointF(10.0, 10.0), 1.15, 1.15);
    painter.drawEllipse(QPointF(13.5, 10.0), 1.15, 1.15);
    return QIcon(pixmap);
}

bool isNormalizedGeometryValid(const QRectF &geometry)
{
    return geometry.x() >= 0.0 && geometry.y() >= 0.0
        && geometry.width() > 0.02 && geometry.height() > 0.02
        && geometry.right() <= 1.01 && geometry.bottom() <= 1.01;
}

} // namespace

class ComponentSurfacePrivate {
public:
    QFrame *titleBar = nullptr;
    QLabel *titleLabel = nullptr;
    RoundedBottomClip *content = nullptr;
};

ComponentSurface::ComponentSurface(QString title, QWidget *parent)
    : QFrame(parent), d(std::make_unique<ComponentSurfacePrivate>())
{
    setObjectName(QStringLiteral("workspaceComponent"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    d->titleBar = new QFrame(this);
    d->titleBar->setObjectName(QStringLiteral("componentTitleBar"));
    d->titleBar->setFixedHeight(32);
    auto *titleLayout = new QHBoxLayout(d->titleBar);
    titleLayout->setContentsMargins(9, 0, 9, 0);
    titleLayout->setSpacing(0);
    d->titleLabel = new QLabel(d->titleBar);
    d->titleLabel->setObjectName(QStringLiteral("componentTitle"));
    titleLayout->addWidget(d->titleLabel);
    root->addWidget(d->titleBar);

    d->content = new RoundedBottomClip(this);
    root->addWidget(d->content, 1);
    setTitle(title);
}

ComponentSurface::~ComponentSurface() = default;

QWidget *ComponentSurface::contentHost() const noexcept
{
    return d->content;
}

QString ComponentSurface::title() const
{
    return d->titleLabel ? d->titleLabel->text() : QString{};
}

void ComponentSurface::setTitle(const QString &title)
{
    const bool hasTitle = !title.trimmed().isEmpty();
    if (d->titleLabel) d->titleLabel->setText(title);
    if (d->titleBar) d->titleBar->setVisible(hasTitle);
    if (d->content) d->content->setClipTopCorners(!hasTitle);
}

struct WorkspaceCanvas::ComponentEntry {
    QPointer<ComponentFrame> frame;
    QRectF defaultGeometry;
    QRectF normalizedGeometry;
    QSize minimumSize;
    int zOrder = 0;
    bool visible = true;
};

class WorkspaceCanvasPrivate {
public:
    explicit WorkspaceCanvasPrivate(QString group)
        : settingsGroup(std::move(group))
    {
    }

    QString settingsGroup;
    QHash<QString, WorkspaceCanvas::ComponentEntry> components;
    QStringList componentOrder;
    QPointer<ComponentFrame> selectedFrame;
    QPointer<ComponentFrame> maximizedFrame;
    QVector<QLine> alignmentGuides;
    bool editLocked = true;
    bool applyingLayout = false;
};

ComponentFrame::ComponentFrame(QString componentId, QString title, QWidget *content,
                               QWidget *parent)
    : QFrame(parent), m_componentId(std::move(componentId))
{
    setObjectName(QStringLiteral("workspaceComponent"));
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);

    m_titleBar = new QFrame(this);
    m_titleBar->setObjectName(QStringLiteral("componentTitleBar"));
    m_titleBar->setFixedHeight(32);
    m_titleBar->setMouseTracking(true);
    m_titleBar->installEventFilter(this);
    auto *titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(9, 0, 4, 0);
    titleLayout->setSpacing(4);
    m_title = new QLabel(std::move(title));
    m_title->setObjectName(QStringLiteral("componentTitle"));
    m_title->installEventFilter(this);
    titleLayout->addWidget(m_title, 1);
    m_menu = new QToolButton;
    m_menu->setObjectName(QStringLiteral("componentChromeButton"));
    m_menu->setAutoRaise(true);
    m_menu->setCursor(Qt::PointingHandCursor);
    m_menu->setIconSize(QSize(18, 18));
    m_menu->setIcon(makeChromeIcon(ChromeIcon::More));
    m_menu->setToolTip(tr("组件操作"));
    m_menu->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_menu->setArrowType(Qt::NoArrow);
    m_menu->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(m_menu);
    m_maximizeAction = menu->addAction(tr("组件全屏"));
    m_closeAction = menu->addAction(tr("关闭组件"));
    m_menu->setMenu(menu);
    titleLayout->addWidget(m_menu);

    root->addWidget(m_titleBar);
    auto *contentClip = new RoundedBottomClip(this);
    auto *contentLayout = new QVBoxLayout(contentClip);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    content->setParent(contentClip);
    contentLayout->addWidget(content);
    root->addWidget(contentClip, 1);

    connect(m_maximizeAction, &QAction::triggered, this,
            [this] { emit toggleMaximizeRequested(this); });
    connect(m_closeAction, &QAction::triggered, this,
            [this] { emit closeRequested(this); });
    updateChrome();
}

ComponentFrame::~ComponentFrame() = default;

QString ComponentFrame::componentId() const
{
    return m_componentId;
}

void ComponentFrame::setEditLocked(const bool locked)
{
    if (m_editLocked == locked) return;
    m_editLocked = locked;
    if (locked && m_interaction != Interaction::None) finishInteraction();
    if (layout()) {
        const int margin = locked ? 1 : ResizeHitWidth;
        layout()->setContentsMargins(margin, margin, margin, margin);
    }
    unsetCursor();
    updateChrome();
    update();
}

bool ComponentFrame::editLocked() const
{
    return m_editLocked;
}

void ComponentFrame::setSelected(const bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    updateChrome();
    update();
}

bool ComponentFrame::isSelected() const
{
    return m_selected;
}

void ComponentFrame::setMaximizedState(const bool maximized)
{
    m_maximizedState = maximized;
    if (layout()) {
        const int margin = maximized || m_editLocked ? 1 : ResizeHitWidth;
        layout()->setContentsMargins(margin, margin, margin, margin);
    }
    if (m_maximizeAction) {
        m_maximizeAction->setText(maximized ? tr("恢复组件") : tr("组件全屏"));
    }
    updateChrome();
}

bool ComponentFrame::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_titleBar && watched != m_title) {
        return QFrame::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) break;
        emit activated(this);
        if (!m_editLocked && !m_maximizedState) {
            beginInteraction(Interaction::Move, mouse->globalPosition().toPoint());
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_interaction == Interaction::Move) {
            continueInteraction(mouse->globalPosition().toPoint());
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease:
        if (m_interaction == Interaction::Move) {
            finishInteraction();
            return true;
        }
        break;
    case QEvent::MouseButtonDblClick: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            emit toggleMaximizeRequested(this);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QFrame::eventFilter(watched, event);
}

void ComponentFrame::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    updateChrome();
    QFrame::enterEvent(event);
}

void ComponentFrame::leaveEvent(QEvent *event)
{
    m_hovered = false;
    if (m_interaction == Interaction::None) unsetCursor();
    updateChrome();
    QFrame::leaveEvent(event);
}

void ComponentFrame::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit toggleMaximizeRequested(this);
        event->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}

void ComponentFrame::mouseMoveEvent(QMouseEvent *event)
{
    if (m_interaction == Interaction::Resize) {
        continueInteraction(event->globalPosition().toPoint());
        event->accept();
        return;
    }
    updateCursor(event->position().toPoint());
    QFrame::mouseMoveEvent(event);
}

void ComponentFrame::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit activated(this);
        const ResizeEdges edges = hitTestEdges(event->position().toPoint());
        if (!m_editLocked && !m_maximizedState && edges != NoEdge) {
            beginInteraction(Interaction::Resize, event->globalPosition().toPoint(), edges);
            event->accept();
            return;
        }
    }
    QFrame::mousePressEvent(event);
}

void ComponentFrame::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_interaction == Interaction::Resize) {
        finishInteraction();
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void ComponentFrame::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    if (!m_selected || m_editLocked || m_maximizedState) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#2563eb")), 1.4));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -2.0, -2.0),
                            ComponentRadius, ComponentRadius);

    painter.setBrush(QColor(QStringLiteral("#ffffff")));
    constexpr int handle = 7;
    const QVector<QPoint> points{
        rect().topLeft() + QPoint(1, 1),
        QPoint(rect().center().x(), 1),
        rect().topRight() + QPoint(-1, 1),
        QPoint(1, rect().center().y()),
        QPoint(rect().right() - 1, rect().center().y()),
        rect().bottomLeft() + QPoint(1, -1),
        QPoint(rect().center().x(), rect().bottom() - 1),
        rect().bottomRight() + QPoint(-1, -1),
    };
    for (const QPoint &point : points) {
        painter.drawRoundedRect(QRectF(point.x() - handle / 2.0,
                                       point.y() - handle / 2.0,
                                       handle, handle), 1.5, 1.5);
    }
}

ComponentFrame::ResizeEdges ComponentFrame::hitTestEdges(const QPoint &position) const
{
    ResizeEdges edges;
    if (position.x() <= ResizeHitWidth) edges |= LeftEdge;
    if (position.y() <= ResizeHitWidth) edges |= TopEdge;
    if (position.x() >= width() - ResizeHitWidth) edges |= RightEdge;
    if (position.y() >= height() - ResizeHitWidth) edges |= BottomEdge;
    return edges;
}

void ComponentFrame::beginInteraction(const Interaction interaction,
                                      const QPoint &globalPosition,
                                      const ResizeEdges edges)
{
    m_interaction = interaction;
    m_resizeEdges = edges;
    m_pressGlobalPosition = globalPosition;
    m_pressGeometry = geometry();
    grabMouse();
}

void ComponentFrame::continueInteraction(const QPoint &globalPosition)
{
    QWidget *area = parentWidget();
    if (!area) return;
    const QPoint delta = globalPosition - m_pressGlobalPosition;
    QRect next = m_pressGeometry;
    if (m_interaction == Interaction::Move) {
        const int x = std::clamp(m_pressGeometry.x() + delta.x(), 0,
                                 std::max(0, area->width() - width()));
        const int y = std::clamp(m_pressGeometry.y() + delta.y(), 0,
                                 std::max(0, area->height() - height()));
        next.moveTopLeft(QPoint(x, y));
    } else if (m_interaction == Interaction::Resize) {
        int left = m_pressGeometry.x();
        int top = m_pressGeometry.y();
        int right = left + m_pressGeometry.width();
        int bottom = top + m_pressGeometry.height();
        if (m_resizeEdges.testFlag(LeftEdge)) left += delta.x();
        if (m_resizeEdges.testFlag(RightEdge)) right += delta.x();
        if (m_resizeEdges.testFlag(TopEdge)) top += delta.y();
        if (m_resizeEdges.testFlag(BottomEdge)) bottom += delta.y();
        left = std::clamp(left, 0, right - minimumWidth());
        right = std::clamp(right, left + minimumWidth(), area->width());
        top = std::clamp(top, 0, bottom - minimumHeight());
        bottom = std::clamp(bottom, top + minimumHeight(), area->height());
        next = QRect(left, top, right - left, bottom - top);
    }
    setGeometry(next);
    emit geometryPreview(this);
}

void ComponentFrame::finishInteraction()
{
    if (m_interaction == Interaction::None) return;
    releaseMouse();
    m_interaction = Interaction::None;
    m_resizeEdges = {};
    emit geometryCommitted(this);
}

void ComponentFrame::updateCursor(const QPoint &position)
{
    if (m_editLocked || m_maximizedState || m_interaction != Interaction::None) {
        unsetCursor();
        return;
    }
    const ResizeEdges edges = hitTestEdges(position);
    if ((edges.testFlag(LeftEdge) && edges.testFlag(TopEdge))
        || (edges.testFlag(RightEdge) && edges.testFlag(BottomEdge))) {
        setCursor(Qt::SizeFDiagCursor);
    } else if ((edges.testFlag(RightEdge) && edges.testFlag(TopEdge))
               || (edges.testFlag(LeftEdge) && edges.testFlag(BottomEdge))) {
        setCursor(Qt::SizeBDiagCursor);
    } else if (edges.testFlag(LeftEdge) || edges.testFlag(RightEdge)) {
        setCursor(Qt::SizeHorCursor);
    } else if (edges.testFlag(TopEdge) || edges.testFlag(BottomEdge)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        unsetCursor();
    }
}

void ComponentFrame::updateChrome()
{
    const bool active = m_hovered || (!m_editLocked && m_selected) || m_maximizedState;
    m_titleBar->setVisible(true);
    m_titleBar->setProperty("active", active);
    m_titleBar->setProperty("selected", m_selected);
    m_titleBar->setProperty("locked", m_editLocked);
    m_titleBar->setProperty("editing", !m_editLocked);
    setProperty("editing", !m_editLocked);
    setProperty("selected", m_selected);
    style()->unpolish(m_titleBar);
    style()->polish(m_titleBar);
    style()->unpolish(this);
    style()->polish(this);
    m_titleBar->raise();
}

WorkspaceCanvas::WorkspaceCanvas(QString settingsGroup, QWidget *parent)
    : QWidget(parent),
      d(std::make_unique<WorkspaceCanvasPrivate>(std::move(settingsGroup)))
{
    setObjectName(QStringLiteral("workspaceCanvas"));
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QSettings settings;
    settings.beginGroup(d->settingsGroup);
    if (settings.value(QStringLiteral("layoutVersion")).toInt() == LayoutVersion) {
        d->editLocked = settings.value(QStringLiteral("editLocked"), true).toBool();
    }
    settings.endGroup();
}

WorkspaceCanvas::~WorkspaceCanvas()
{
    saveLayout();
}

ComponentFrame *WorkspaceCanvas::addComponent(const QString &componentId,
                                               const QString &title,
                                               QWidget *content,
                                               const QRectF &defaultGeometry,
                                               const QSize &minimumSize)
{
    if (componentId.trimmed().isEmpty() || !content) return nullptr;
    if (d->components.contains(componentId)) return d->components[componentId].frame;
    auto *frame = new ComponentFrame(componentId, title, content, this);
    const QSize safeMinimumSize(std::max(0, minimumSize.width()),
                                std::max(0, minimumSize.height()));
    frame->setMinimumSize(safeMinimumSize);
    frame->setEditLocked(d->editLocked);

    ComponentEntry entry;
    entry.frame = frame;
    entry.defaultGeometry = isNormalizedGeometryValid(defaultGeometry)
        ? defaultGeometry : QRectF(0.02, 0.02, 0.96, 0.96);
    entry.normalizedGeometry = entry.defaultGeometry;
    entry.minimumSize = safeMinimumSize;
    entry.zOrder = d->componentOrder.size();
    loadEntry(entry, componentId);
    d->components.insert(componentId, entry);
    d->componentOrder.append(componentId);

    connect(frame, &ComponentFrame::activated,
            this, &WorkspaceCanvas::activateFrame);
    connect(frame, &ComponentFrame::geometryPreview,
            this, &WorkspaceCanvas::previewFrameGeometry);
    connect(frame, &ComponentFrame::geometryCommitted,
            this, &WorkspaceCanvas::commitFrameGeometry);
    connect(frame, &ComponentFrame::toggleMaximizeRequested, this,
            [this](ComponentFrame *candidate) {
                toggleComponentMaximized(candidate->componentId());
            });
    connect(frame, &ComponentFrame::closeRequested,
            this, &WorkspaceCanvas::closeFrame);

    frame->setVisible(entry.visible);
    applyComponentGeometries();
    applyZOrder();
    return frame;
}

ComponentFrame *WorkspaceCanvas::componentFrame(const QString &componentId) const
{
    const auto iterator = d->components.constFind(componentId);
    return iterator == d->components.cend() ? nullptr : iterator->frame;
}

QStringList WorkspaceCanvas::componentIds() const
{
    return d->componentOrder;
}

bool WorkspaceCanvas::hasStoredLayout() const
{
    QSettings settings;
    settings.beginGroup(d->settingsGroup);
    const bool stored = settings.value(QStringLiteral("layoutVersion")).toInt()
        == LayoutVersion;
    settings.endGroup();
    return stored;
}

void WorkspaceCanvas::setEditLocked(const bool locked)
{
    if (d->editLocked == locked) return;
    d->editLocked = locked;
    for (const QString &id : std::as_const(d->componentOrder)) {
        if (ComponentFrame *frame = componentFrame(id)) frame->setEditLocked(locked);
    }
    if (locked) {
        clearSelection();
        d->alignmentGuides.clear();
        update();
    }
    saveLayout();
    emit editLockedChanged(locked);
}

bool WorkspaceCanvas::editLocked() const
{
    return d->editLocked;
}

void WorkspaceCanvas::setComponentVisible(const QString &componentId, const bool visible)
{
    auto iterator = d->components.find(componentId);
    if (iterator == d->components.end() || iterator->visible == visible) return;
    if (d->maximizedFrame && d->maximizedFrame->componentId() == componentId && !visible) {
        leaveMaximized();
    }
    iterator->visible = visible;
    if (iterator->frame) iterator->frame->setVisible(visible);
    if (!visible && d->selectedFrame == iterator->frame) clearSelection();
    saveLayout();
    emit componentVisibilityChanged(componentId, visible);
    emit layoutChanged();
}

bool WorkspaceCanvas::addOrShowComponentAt(const QString &componentId,
                                            const QPoint &position)
{
    auto iterator = d->components.find(componentId);
    if (iterator == d->components.end() || !iterator->frame) return false;
    if (d->maximizedFrame) leaveMaximized();

    ComponentEntry &entry = iterator.value();
    const bool newlyRevealed = !entry.visible;
    if (!entry.visible) {
        entry.visible = true;
        entry.frame->show();
        emit componentVisibilityChanged(componentId, true);
    }

    applyComponentGeometries();
    QRect target = entry.frame->geometry();
    if (position.x() >= 0 && position.y() >= 0 && width() > 0 && height() > 0) {
        target.moveCenter(position);
        target.moveLeft(std::clamp(target.left(), 0, std::max(0, width() - target.width())));
        target.moveTop(std::clamp(target.top(), 0, std::max(0, height() - target.height())));
        entry.frame->setGeometry(target);
        entry.normalizedGeometry = normalizedFromGeometry(target);
    }

    const bool hasExplicitPosition = position.x() >= 0 && position.y() >= 0;
    if (newlyRevealed && !hasExplicitPosition) {
        enterMaximized(entry.frame);
    } else {
        setSelectedFrame(entry.frame);
        activateFrame(entry.frame);
    }
    saveLayout();
    update();
    emit layoutChanged();
    return true;
}

bool WorkspaceCanvas::isComponentVisible(const QString &componentId) const
{
    const auto iterator = d->components.constFind(componentId);
    return iterator != d->components.cend() && iterator->visible;
}

void WorkspaceCanvas::toggleComponentMaximized(const QString &componentId)
{
    ComponentFrame *frame = componentFrame(componentId);
    if (!frame || !isComponentVisible(componentId)) return;
    if (d->maximizedFrame == frame) leaveMaximized(); else enterMaximized(frame);
}

bool WorkspaceCanvas::isComponentMaximized(const QString &componentId) const
{
    return d->maximizedFrame && d->maximizedFrame->componentId() == componentId;
}

void WorkspaceCanvas::resetLayout()
{
    leaveMaximized();
    int zOrder = 0;
    for (const QString &id : std::as_const(d->componentOrder)) {
        ComponentEntry &entry = d->components[id];
        entry.normalizedGeometry = entry.defaultGeometry;
        entry.zOrder = zOrder++;
        entry.visible = true;
        if (entry.frame) entry.frame->show();
        emit componentVisibilityChanged(id, true);
    }
    d->editLocked = true;
    for (const QString &id : std::as_const(d->componentOrder)) {
        if (ComponentFrame *frame = componentFrame(id)) frame->setEditLocked(true);
    }
    clearSelection();
    applyComponentGeometries();
    applyZOrder();
    saveLayout();
    emit editLockedChanged(true);
    emit layoutChanged();
}

void WorkspaceCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && d->maximizedFrame) {
        leaveMaximized();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void WorkspaceCanvas::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-serein-component"))) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void WorkspaceCanvas::dropEvent(QDropEvent *event)
{
    const QByteArray id = event->mimeData()->data(
        QStringLiteral("application/x-serein-component"));
    if (id.isEmpty()) {
        event->ignore();
        return;
    }
    const QString componentId = QString::fromUtf8(id);
    if (!addOrShowComponentAt(componentId, event->position().toPoint())
        && !componentId.isEmpty()) {
        emit componentDropRequested(componentId, event->position().toPoint());
        event->acceptProposedAction();
        return;
    }
    if (!componentFrame(componentId)) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void WorkspaceCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        clearSelection();
    }
    QWidget::mousePressEvent(event);
}

void WorkspaceCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = property("darkMode").toBool();
    const QRectF surface = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor background(dark ? QStringLiteral("#20252a")
                                : QStringLiteral("#e5e8ea"));
    painter.setPen(QPen(QColor(dark ? QStringLiteral("#3d474f")
                                   : QStringLiteral("#d1d8de")), 1.0));
    painter.setBrush(background);
    painter.drawRoundedRect(surface, ComponentRadius, ComponentRadius);

    QPainterPath clip;
    clip.addRoundedRect(surface.adjusted(1.0, 1.0, -1.0, -1.0),
                        ComponentInnerRadius, ComponentInnerRadius);
    painter.save();
    painter.setClipPath(clip);
    if (!d->editLocked) {
        painter.setPen(QColor(dark ? QStringLiteral("#34404a")
                                   : QStringLiteral("#d4dce7")));
        for (int y = SnapGrid; y < height(); y += SnapGrid * 2) {
            for (int x = SnapGrid; x < width(); x += SnapGrid * 2) {
                painter.drawPoint(x, y);
            }
        }
    }

    bool hasVisibleComponent = false;
    for (const QString &id : std::as_const(d->componentOrder)) {
        if (isComponentVisible(id)) {
            hasVisibleComponent = true;
            break;
        }
    }
    const QString emptyTitle = property("emptyTitle").toString();
    if (!hasVisibleComponent && !emptyTitle.isEmpty()) {
        const QString emptyHint = property("emptyHint").toString();
        painter.setPen(QColor(dark ? QStringLiteral("#d8e0e8")
                                   : QStringLiteral("#526171")));
        QFont titleFont = painter.font();
        titleFont.setPointSize(14);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        const QRect titleRect = rect().adjusted(24, height() / 2 - 38, -24, 0);
        painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignBottom, emptyTitle);
        painter.setPen(QColor(dark ? QStringLiteral("#95a2ae")
                                   : QStringLiteral("#7a8699")));
        QFont hintFont = painter.font();
        hintFont.setPointSize(10);
        hintFont.setWeight(QFont::Normal);
        painter.setFont(hintFont);
        const QRect hintRect = rect().adjusted(24, height() / 2 + 4, -24, 0);
        painter.drawText(hintRect, Qt::AlignHCenter | Qt::AlignTop, emptyHint);
    }

    painter.setPen(QPen(QColor(dark ? QStringLiteral("#5a9de0")
                                   : QStringLiteral("#2563eb")), 1, Qt::DashLine));
    for (const QLine &guide : std::as_const(d->alignmentGuides)) painter.drawLine(guide);
    painter.restore();
}

void WorkspaceCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyComponentGeometries();
}

QRect WorkspaceCanvas::geometryFromNormalized(const QRectF &normalized) const
{
    if (width() <= 0 || height() <= 0) return {};
    return QRect(qRound(normalized.x() * width()), qRound(normalized.y() * height()),
                 qRound(normalized.width() * width()),
                 qRound(normalized.height() * height()));
}

QRectF WorkspaceCanvas::normalizedFromGeometry(const QRect &geometry) const
{
    if (width() <= 0 || height() <= 0) return {};
    return QRectF(double(geometry.x()) / width(), double(geometry.y()) / height(),
                  double(geometry.width()) / width(),
                  double(geometry.height()) / height());
}

void WorkspaceCanvas::applyComponentGeometries()
{
    if (d->applyingLayout || width() <= 0 || height() <= 0) return;
    d->applyingLayout = true;
    if (d->maximizedFrame) {
        // A maximized component is the canvas surface itself.  Keeping an
        // extra inset here creates the double-left-edge seen in the QA image.
        d->maximizedFrame->setMinimumSize(QSize(0, 0));
        d->maximizedFrame->setGeometry(rect());
    } else {
        for (const QString &id : std::as_const(d->componentOrder)) {
            ComponentEntry &entry = d->components[id];
            if (!entry.frame) continue;
            QRect target = geometryFromNormalized(entry.normalizedGeometry);
            const int requiredWidth = entry.minimumSize.width();
            const int requiredHeight = entry.minimumSize.height();
            target.moveLeft(std::clamp(target.left(), 0, std::max(0, width() - 1)));
            target.moveTop(std::clamp(target.top(), 0, std::max(0, height() - 1)));

            // A dock can temporarily leave less room than a component's normal
            // minimum. Keep the outer frame visible inside the canvas during
            // that constrained state. Restore the declared minimum only after
            // the canvas has enough room again; restoring it immediately would
            // make QWidget expand beyond the canvas a second time.
            const int availableWidth = std::max(1, width() - target.left());
            const int availableHeight = std::max(1, height() - target.top());
            target.setWidth(std::min(std::max(target.width(), requiredWidth),
                                     availableWidth));
            target.setHeight(std::min(std::max(target.height(), requiredHeight),
                                      availableHeight));
            const bool constrained = target.width() < requiredWidth
                || target.height() < requiredHeight;
            entry.frame->setMinimumSize(constrained ? QSize(0, 0)
                                                    : entry.minimumSize);
            entry.frame->setGeometry(target);
        }
    }
    d->applyingLayout = false;
}

void WorkspaceCanvas::applyZOrder()
{
    QStringList ordered = d->componentOrder;
    std::sort(ordered.begin(), ordered.end(), [this](const QString &left, const QString &right) {
        return d->components[left].zOrder < d->components[right].zOrder;
    });
    for (const QString &id : std::as_const(ordered)) {
        if (ComponentFrame *frame = componentFrame(id)) frame->raise();
    }
}

void WorkspaceCanvas::activateFrame(ComponentFrame *frame)
{
    if (!frame) return;
    setFocus(Qt::MouseFocusReason);
    setSelectedFrame(frame);
    if (d->editLocked || d->maximizedFrame) return;
    int maximum = 0;
    for (const ComponentEntry &entry : std::as_const(d->components)) {
        maximum = std::max(maximum, entry.zOrder);
    }
    d->components[frame->componentId()].zOrder = maximum + 1;
    frame->raise();
}

void WorkspaceCanvas::previewFrameGeometry(ComponentFrame *frame)
{
    d->alignmentGuides.clear();
    if (!frame) return;
    const QRect active = frame->geometry();
    const QVector<int> activeX{active.left(), active.center().x(), active.right()};
    const QVector<int> activeY{active.top(), active.center().y(), active.bottom()};
    for (const QString &id : std::as_const(d->componentOrder)) {
        ComponentFrame *other = componentFrame(id);
        if (!other || other == frame || !other->isVisible()) continue;
        const QRect comparison = other->geometry();
        const QVector<int> comparisonX{
            comparison.left(), comparison.center().x(), comparison.right()};
        const QVector<int> comparisonY{
            comparison.top(), comparison.center().y(), comparison.bottom()};
        for (const int x : activeX) {
            for (const int candidate : comparisonX) {
                if (std::abs(x - candidate) <= AlignmentTolerance) {
                    d->alignmentGuides.append(QLine(candidate, 0, candidate, height()));
                }
            }
        }
        for (const int y : activeY) {
            for (const int candidate : comparisonY) {
                if (std::abs(y - candidate) <= AlignmentTolerance) {
                    d->alignmentGuides.append(QLine(0, candidate, width(), candidate));
                }
            }
        }
    }
    update();
}

void WorkspaceCanvas::commitFrameGeometry(ComponentFrame *frame)
{
    d->alignmentGuides.clear();
    if (!frame || d->applyingLayout) return;
    clampAndSnapFrame(frame);
    d->components[frame->componentId()].normalizedGeometry = normalizedFromGeometry(frame->geometry());
    saveLayout();
    update();
    emit layoutChanged();
}

void WorkspaceCanvas::clearSelection()
{
    if (d->selectedFrame) d->selectedFrame->setSelected(false);
    d->selectedFrame = nullptr;
}

void WorkspaceCanvas::setSelectedFrame(ComponentFrame *frame)
{
    if (d->selectedFrame == frame) return;
    clearSelection();
    d->selectedFrame = frame;
    if (d->selectedFrame) d->selectedFrame->setSelected(true);
}

void WorkspaceCanvas::closeFrame(ComponentFrame *frame)
{
    if (frame) setComponentVisible(frame->componentId(), false);
}

void WorkspaceCanvas::enterMaximized(ComponentFrame *frame)
{
    if (!frame) return;
    if (d->maximizedFrame) leaveMaximized();
    d->maximizedFrame = frame;
    for (const QString &id : std::as_const(d->componentOrder)) {
        ComponentFrame *candidate = componentFrame(id);
        if (!candidate) continue;
        candidate->setVisible(candidate == frame);
    }
    frame->setMaximizedState(true);
    // Fullscreen means exact canvas bounds; the component frame owns the
    // visible rounded edge and must not float inside a second inset frame.
    frame->setMinimumSize(QSize(0, 0));
    frame->setGeometry(rect());
    frame->raise();
    setSelectedFrame(frame);
    setFocus(Qt::OtherFocusReason);
    emit focusModeChanged(true);
}

void WorkspaceCanvas::leaveMaximized()
{
    if (!d->maximizedFrame) return;
    d->maximizedFrame->setMaximizedState(false);
    d->maximizedFrame = nullptr;
    for (const QString &id : std::as_const(d->componentOrder)) {
        ComponentEntry &entry = d->components[id];
        if (entry.frame) entry.frame->setVisible(entry.visible);
    }
    applyComponentGeometries();
    applyZOrder();
    emit focusModeChanged(false);
}

void WorkspaceCanvas::saveLayout() const
{
    QSettings settings;
    settings.beginGroup(d->settingsGroup);
    settings.setValue(QStringLiteral("layoutVersion"), LayoutVersion);
    settings.setValue(QStringLiteral("editLocked"), d->editLocked);
    for (const QString &id : d->componentOrder) {
        const auto iterator = d->components.constFind(id);
        if (iterator == d->components.cend()) continue;
        const ComponentEntry &entry = iterator.value();
        settings.setValue(QStringLiteral("components/%1/geometry").arg(id),
                          entry.normalizedGeometry);
        settings.setValue(QStringLiteral("components/%1/zOrder").arg(id), entry.zOrder);
        settings.setValue(QStringLiteral("components/%1/visible").arg(id), entry.visible);
    }
    settings.endGroup();
}

void WorkspaceCanvas::loadEntry(ComponentEntry &entry, const QString &componentId)
{
    QSettings settings;
    settings.beginGroup(d->settingsGroup);
    if (settings.value(QStringLiteral("layoutVersion")).toInt() == LayoutVersion) {
        const QRectF saved = settings.value(
            QStringLiteral("components/%1/geometry").arg(componentId)).toRectF();
        if (isNormalizedGeometryValid(saved)) entry.normalizedGeometry = saved;
        entry.zOrder = settings.value(QStringLiteral("components/%1/zOrder").arg(componentId),
                                      entry.zOrder).toInt();
        entry.visible = settings.value(QStringLiteral("components/%1/visible").arg(componentId),
                                       true).toBool();
    }
    settings.endGroup();
}

void WorkspaceCanvas::clampAndSnapFrame(ComponentFrame *frame)
{
    QRect geometry = frame->geometry();
    const auto snap = [](const int value) {
        return qRound(double(value) / SnapGrid) * SnapGrid;
    };
    geometry.setX(snap(geometry.x()));
    geometry.setY(snap(geometry.y()));
    geometry.setWidth(std::max(frame->minimumWidth(), snap(geometry.width())));
    geometry.setHeight(std::max(frame->minimumHeight(), snap(geometry.height())));
    if (geometry.right() >= width()) geometry.moveRight(width() - 1);
    if (geometry.bottom() >= height()) geometry.moveBottom(height() - 1);
    geometry.moveLeft(std::max(0, geometry.left()));
    geometry.moveTop(std::max(0, geometry.top()));
    frame->setGeometry(geometry);
}

} // namespace serein::scope
