#include "WorkspaceCanvas.h"

#include <QtCore/QEvent>
#include <QtCore/QSettings>
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
#include <QtGui/QResizeEvent>
#include <QtWidgets/QGraphicsDropShadowEffect>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QSizePolicy>

#include <algorithm>
#include <cmath>

namespace serein::scope::tn160 {
namespace {

constexpr int LayoutVersion = 1;
constexpr int ResizeHitWidth = 7;
constexpr int SnapGrid = 8;
constexpr int AlignmentTolerance = 4;

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

ComponentFrame::ComponentFrame(QString componentId, QString title, QWidget *content,
                               QWidget *parent)
    : QFrame(parent), m_componentId(std::move(componentId))
{
    setObjectName(QStringLiteral("workspaceComponent"));
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(8.0);
    shadow->setColor(QColor(15, 23, 42, 15));
    shadow->setOffset(0.0, 1.0);
    setGraphicsEffect(shadow);

    auto *root = new QVBoxLayout(this);
    // The compact chrome lives in the upper-right corner. Keep a dedicated
    // safety band so a component's first data row can never sit underneath it.
    root->setContentsMargins(1, 40, 1, 1);
    root->setSpacing(0);

    m_titleBar = new QFrame(this);
    m_titleBar->setObjectName(QStringLiteral("componentTitleBar"));
    m_titleBar->setFixedHeight(28);
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

    content->setParent(this);
    root->addWidget(content, 1);
    m_titleBar->raise();

    connect(m_maximizeAction, &QAction::triggered, this,
            [this] { emit toggleMaximizeRequested(this); });
    connect(m_closeAction, &QAction::triggered, this,
            [this] { emit closeRequested(this); });
    updateChrome();
}

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
        layout()->setContentsMargins(margin, 40, margin, margin);
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
    painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -2.0, -2.0), 8.0, 8.0);

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

void ComponentFrame::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    const int available = std::max(120, width() - 20);
    const int desired = m_title->sizeHint().width() + 70;
    const int titleWidth = std::min(available, std::max(132, desired));
    m_titleBar->setGeometry(width() - titleWidth - 10, 10, titleWidth, 28);
    m_titleBar->raise();
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
    : QWidget(parent), m_settingsGroup(std::move(settingsGroup))
{
    setObjectName(QStringLiteral("workspaceCanvas"));
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QSettings settings;
    settings.beginGroup(m_settingsGroup);
    if (settings.value(QStringLiteral("layoutVersion")).toInt() == LayoutVersion) {
        m_editLocked = settings.value(QStringLiteral("editLocked"), true).toBool();
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
    if (m_components.contains(componentId)) return m_components[componentId].frame;
    auto *frame = new ComponentFrame(componentId, title, content, this);
    frame->setMinimumSize(minimumSize);
    frame->setEditLocked(m_editLocked);

    ComponentEntry entry;
    entry.frame = frame;
    entry.defaultGeometry = defaultGeometry;
    entry.normalizedGeometry = defaultGeometry;
    entry.zOrder = m_componentOrder.size();
    loadEntry(entry, componentId);
    m_components.insert(componentId, entry);
    m_componentOrder.append(componentId);

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
    const auto iterator = m_components.constFind(componentId);
    return iterator == m_components.cend() ? nullptr : iterator->frame;
}

QStringList WorkspaceCanvas::componentIds() const
{
    return m_componentOrder;
}

void WorkspaceCanvas::setEditLocked(const bool locked)
{
    if (m_editLocked == locked) return;
    m_editLocked = locked;
    for (const QString &id : std::as_const(m_componentOrder)) {
        if (ComponentFrame *frame = componentFrame(id)) frame->setEditLocked(locked);
    }
    if (locked) {
        clearSelection();
        m_alignmentGuides.clear();
        update();
    }
    saveLayout();
    emit editLockedChanged(locked);
}

bool WorkspaceCanvas::editLocked() const
{
    return m_editLocked;
}

void WorkspaceCanvas::setComponentVisible(const QString &componentId, const bool visible)
{
    auto iterator = m_components.find(componentId);
    if (iterator == m_components.end() || iterator->visible == visible) return;
    if (m_maximizedFrame && m_maximizedFrame->componentId() == componentId && !visible) {
        leaveMaximized();
    }
    iterator->visible = visible;
    if (iterator->frame) iterator->frame->setVisible(visible);
    if (!visible && m_selectedFrame == iterator->frame) clearSelection();
    saveLayout();
    emit componentVisibilityChanged(componentId, visible);
    emit layoutChanged();
}

bool WorkspaceCanvas::addOrShowComponentAt(const QString &componentId,
                                            const QPoint &position)
{
    auto iterator = m_components.find(componentId);
    if (iterator == m_components.end() || !iterator->frame) return false;
    if (m_maximizedFrame) leaveMaximized();

    ComponentEntry &entry = iterator.value();
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

    setSelectedFrame(entry.frame);
    activateFrame(entry.frame);
    saveLayout();
    update();
    emit layoutChanged();
    return true;
}

bool WorkspaceCanvas::isComponentVisible(const QString &componentId) const
{
    const auto iterator = m_components.constFind(componentId);
    return iterator != m_components.cend() && iterator->visible;
}

void WorkspaceCanvas::toggleComponentMaximized(const QString &componentId)
{
    ComponentFrame *frame = componentFrame(componentId);
    if (!frame || !isComponentVisible(componentId)) return;
    if (m_maximizedFrame == frame) leaveMaximized(); else enterMaximized(frame);
}

bool WorkspaceCanvas::isComponentMaximized(const QString &componentId) const
{
    return m_maximizedFrame && m_maximizedFrame->componentId() == componentId;
}

void WorkspaceCanvas::resetLayout()
{
    leaveMaximized();
    int zOrder = 0;
    for (const QString &id : std::as_const(m_componentOrder)) {
        ComponentEntry &entry = m_components[id];
        entry.normalizedGeometry = entry.defaultGeometry;
        entry.zOrder = zOrder++;
        entry.visible = true;
        if (entry.frame) entry.frame->show();
        emit componentVisibilityChanged(id, true);
    }
    m_editLocked = true;
    for (const QString &id : std::as_const(m_componentOrder)) {
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
    if (event->key() == Qt::Key_Escape && m_maximizedFrame) {
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
    if (!addOrShowComponentAt(QString::fromUtf8(id), event->position().toPoint())) {
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
    painter.drawRoundedRect(surface, 8.0, 8.0);

    QPainterPath clip;
    clip.addRoundedRect(surface.adjusted(1.0, 1.0, -1.0, -1.0), 7.0, 7.0);
    painter.save();
    painter.setClipPath(clip);
    if (!m_editLocked) {
        painter.setPen(QColor(dark ? QStringLiteral("#34404a")
                                   : QStringLiteral("#d4dce7")));
        for (int y = SnapGrid; y < height(); y += SnapGrid * 2) {
            for (int x = SnapGrid; x < width(); x += SnapGrid * 2) {
                painter.drawPoint(x, y);
            }
        }
    }

    bool hasVisibleComponent = false;
    for (const QString &id : std::as_const(m_componentOrder)) {
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
    for (const QLine &guide : std::as_const(m_alignmentGuides)) painter.drawLine(guide);
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
    if (m_applyingLayout || width() <= 0 || height() <= 0) return;
    m_applyingLayout = true;
    if (m_maximizedFrame) {
        // A maximized component is the canvas surface itself.  Keeping an
        // extra inset here creates the double-left-edge seen in the QA image.
        m_maximizedFrame->setGeometry(rect());
    } else {
        for (const QString &id : std::as_const(m_componentOrder)) {
            ComponentEntry &entry = m_components[id];
            if (!entry.frame) continue;
            QRect target = geometryFromNormalized(entry.normalizedGeometry);
            const int requiredWidth = entry.frame->minimumWidth();
            const int requiredHeight = entry.frame->minimumHeight();
            target.moveLeft(std::clamp(target.left(), 0, std::max(0, width() - 1)));
            target.moveTop(std::clamp(target.top(), 0, std::max(0, height() - 1)));

            // A dock can temporarily leave less room than a component's normal
            // minimum. Keep the outer frame visible inside the canvas during
            // that constrained state; restore the real minimum immediately so
            // edit-mode resizing and normal workspaces keep their contract.
            const int availableWidth = std::max(1, width() - target.left());
            const int availableHeight = std::max(1, height() - target.top());
            target.setWidth(std::min(std::max(target.width(), requiredWidth),
                                     availableWidth));
            target.setHeight(std::min(std::max(target.height(), requiredHeight),
                                      availableHeight));
            const bool constrained = target.width() < requiredWidth
                || target.height() < requiredHeight;
            if (constrained) entry.frame->setMinimumSize(QSize(0, 0));
            entry.frame->setGeometry(target);
            if (constrained) entry.frame->setMinimumSize(QSize(requiredWidth, requiredHeight));
        }
    }
    m_applyingLayout = false;
}

void WorkspaceCanvas::applyZOrder()
{
    QStringList ordered = m_componentOrder;
    std::sort(ordered.begin(), ordered.end(), [this](const QString &left, const QString &right) {
        return m_components[left].zOrder < m_components[right].zOrder;
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
    if (m_editLocked || m_maximizedFrame) return;
    int maximum = 0;
    for (const ComponentEntry &entry : std::as_const(m_components)) {
        maximum = std::max(maximum, entry.zOrder);
    }
    m_components[frame->componentId()].zOrder = maximum + 1;
    frame->raise();
}

void WorkspaceCanvas::previewFrameGeometry(ComponentFrame *frame)
{
    m_alignmentGuides.clear();
    if (!frame) return;
    const QRect active = frame->geometry();
    const QVector<int> activeX{active.left(), active.center().x(), active.right()};
    const QVector<int> activeY{active.top(), active.center().y(), active.bottom()};
    for (const QString &id : std::as_const(m_componentOrder)) {
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
                    m_alignmentGuides.append(QLine(candidate, 0, candidate, height()));
                }
            }
        }
        for (const int y : activeY) {
            for (const int candidate : comparisonY) {
                if (std::abs(y - candidate) <= AlignmentTolerance) {
                    m_alignmentGuides.append(QLine(0, candidate, width(), candidate));
                }
            }
        }
    }
    update();
}

void WorkspaceCanvas::commitFrameGeometry(ComponentFrame *frame)
{
    m_alignmentGuides.clear();
    if (!frame || m_applyingLayout) return;
    clampAndSnapFrame(frame);
    m_components[frame->componentId()].normalizedGeometry = normalizedFromGeometry(frame->geometry());
    saveLayout();
    update();
    emit layoutChanged();
}

void WorkspaceCanvas::clearSelection()
{
    if (m_selectedFrame) m_selectedFrame->setSelected(false);
    m_selectedFrame = nullptr;
}

void WorkspaceCanvas::setSelectedFrame(ComponentFrame *frame)
{
    if (m_selectedFrame == frame) return;
    clearSelection();
    m_selectedFrame = frame;
    if (m_selectedFrame) m_selectedFrame->setSelected(true);
}

void WorkspaceCanvas::closeFrame(ComponentFrame *frame)
{
    if (frame) setComponentVisible(frame->componentId(), false);
}

void WorkspaceCanvas::enterMaximized(ComponentFrame *frame)
{
    if (!frame) return;
    if (m_maximizedFrame) leaveMaximized();
    m_maximizedFrame = frame;
    for (const QString &id : std::as_const(m_componentOrder)) {
        ComponentFrame *candidate = componentFrame(id);
        if (!candidate) continue;
        candidate->setVisible(candidate == frame);
    }
    frame->setMaximizedState(true);
    // Fullscreen means exact canvas bounds; the component frame owns the
    // visible rounded edge and must not float inside a second inset frame.
    frame->setGeometry(rect());
    frame->raise();
    setSelectedFrame(frame);
    setFocus(Qt::OtherFocusReason);
}

void WorkspaceCanvas::leaveMaximized()
{
    if (!m_maximizedFrame) return;
    m_maximizedFrame->setMaximizedState(false);
    m_maximizedFrame = nullptr;
    for (const QString &id : std::as_const(m_componentOrder)) {
        ComponentEntry &entry = m_components[id];
        if (entry.frame) entry.frame->setVisible(entry.visible);
    }
    applyComponentGeometries();
    applyZOrder();
}

void WorkspaceCanvas::saveLayout() const
{
    QSettings settings;
    settings.beginGroup(m_settingsGroup);
    settings.setValue(QStringLiteral("layoutVersion"), LayoutVersion);
    settings.setValue(QStringLiteral("editLocked"), m_editLocked);
    for (const QString &id : m_componentOrder) {
        const ComponentEntry &entry = m_components[id];
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
    settings.beginGroup(m_settingsGroup);
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

} // namespace serein::scope::tn160
