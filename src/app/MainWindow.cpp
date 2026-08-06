#include "MainWindow.h"
#include "ComponentManager.h"
#include "PluginCard.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QMimeData>
#include <QtCore/QMetaObject>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStandardPaths>
#include <QtCore/QTime>
#include <QtCore/QTimer>
#include <QtCore/QVariantAnimation>
#include <QtGui/QIcon>
#include <QtGui/QConicalGradient>
#include <QtGui/QDrag>
#include <QtGui/QLinearGradient>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>
#include <QtGui/QPaintEvent>
#include <QtGui/QRegion>
#include <QtGui/QWindow>
#include <serein/scope/SerialTransport.h>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QListView>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QSizePolicy>
#include <QtSvg/QSvgRenderer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>

namespace serein::scope::app {
namespace {

const QString HomeId = QStringLiteral("__home__");
const QString ComponentsId = QStringLiteral("__components__");
const QString DockSettingsKey = QStringLiteral("main/docks-v15-output-clear-connection-right");
const QString CentralDockSettingsKey = QStringLiteral("main/central-dock-v1");
constexpr int DockLayoutVersion = 15;
constexpr int CentralDockLayoutVersion = 1;
constexpr qreal WindowCornerRadius = 12.0;

enum class RailGlyph {
    TemplateGrid,
    TemplateBars,
    TemplateLayers,
    TemplateWallet,
    PanelWorkbench,
    PanelOutput,
    PanelInspector,
    PanelLibrary,
    Sleep,
};

bool isTemplateGlyph(const RailGlyph glyph)
{
    return glyph == RailGlyph::TemplateGrid || glyph == RailGlyph::TemplateBars
        || glyph == RailGlyph::TemplateLayers || glyph == RailGlyph::TemplateWallet;
}

bool isPanelGlyph(const RailGlyph glyph)
{
    return glyph == RailGlyph::PanelWorkbench || glyph == RailGlyph::PanelOutput
        || glyph == RailGlyph::PanelInspector || glyph == RailGlyph::PanelLibrary;
}

class ComponentLibraryList final : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    void startDrag(const Qt::DropActions supportedActions) override
    {
        auto *item = currentItem();
        if (!item) return;
        const QByteArray id = item->data(Qt::UserRole).toString().toUtf8();
        if (id.isEmpty()) return;
        auto *mime = new QMimeData;
        mime->setData(QStringLiteral("application/x-serein-component"), id);
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(supportedActions & Qt::CopyAction ? Qt::CopyAction : Qt::MoveAction);
    }
};

enum class PanelGlyph {
    Float,
    Close,
    Minimize,
    Maximize,
};

class GlobalTitleBar final : public QFrame {
    Q_OBJECT

public:
    explicit GlobalTitleBar(QWidget *parent = nullptr) : QFrame(parent)
    {
        setMouseTracking(true);
    }

signals:
    void dragRequested();
    void doubleClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            emit dragRequested();
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            emit doubleClicked();
            event->accept();
            return;
        }
        QFrame::mouseDoubleClickEvent(event);
    }
};

QIcon makePanelIcon(const PanelGlyph kind)
{
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#667085")), 1.45,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (kind == PanelGlyph::Close) {
        painter.drawLine(QPointF(7, 7), QPointF(13, 13));
        painter.drawLine(QPointF(13, 7), QPointF(7, 13));
    } else if (kind == PanelGlyph::Minimize) {
        painter.drawLine(QPointF(5.5, 10), QPointF(14.5, 10));
    } else if (kind == PanelGlyph::Maximize) {
        painter.drawRect(QRectF(5.5, 5.5, 9, 9));
    } else if (kind == PanelGlyph::Float) {
        painter.drawRoundedRect(QRectF(4.5, 6.5, 7, 7), 1, 1);
        painter.drawLine(QPointF(8.5, 4.5), QPointF(13.5, 4.5));
        painter.drawLine(QPointF(13.5, 4.5), QPointF(13.5, 9.5));
        painter.drawLine(QPointF(10.5, 7.5), QPointF(13.5, 4.5));
    }
    return QIcon(pixmap);
}

class RailButton final : public QToolButton {
public:
    explicit RailButton(const RailGlyph glyph, QWidget *parent = nullptr)
        : QToolButton(parent), m_glyph(glyph)
    {
        if (glyph == RailGlyph::Sleep) {
            setFixedSize(40, 40);
        } else if (isPanelGlyph(glyph)) {
            setFixedSize(36, 36);
        } else {
            setFixedSize(36, 38);
        }
        setCursor(Qt::PointingHandCursor);
        setCheckable(true);
        setAutoRaise(true);
        if (isTemplateGlyph(glyph)) {
            m_templateRenderer = new QSvgRenderer(this);
            const QString resourcePath = glyph == RailGlyph::TemplateGrid
                ? QStringLiteral(":/serein/grid.svg")
                : glyph == RailGlyph::TemplateBars
                    ? QStringLiteral(":/serein/bars.svg")
                    : glyph == RailGlyph::TemplateLayers
                        ? QStringLiteral(":/serein/layers.svg")
                        : QStringLiteral(":/serein/wallet.svg");
            QFile source(resourcePath);
            if (source.open(QIODevice::ReadOnly)) {
                m_templateSvg = source.readAll();
            }
        }
        m_motion = new QVariantAnimation(this);
        m_motion->setDuration(120);
        m_motion->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_motion, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    m_hoverProgress = value.toReal();
                    update();
                });
        if (glyph == RailGlyph::TemplateGrid) {
            m_flowMotion = new QVariantAnimation(this);
            m_flowMotion->setDuration(3200);
            m_flowMotion->setStartValue(0.0);
            m_flowMotion->setEndValue(360.0);
            m_flowMotion->setLoopCount(-1);
            m_flowMotion->setEasingCurve(QEasingCurve::Linear);
            connect(m_flowMotion, &QVariantAnimation::valueChanged, this,
                    [this](const QVariant &value) {
                        m_flowAngle = value.toReal();
                        update();
                    });
            m_flowMotion->start();
        }
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            if (event->type() == QEvent::Leave) m_clicked = false;
            m_motion->stop();
            m_motion->setStartValue(m_hoverProgress);
            m_motion->setEndValue(event->type() == QEvent::Enter ? 1.0 : 0.0);
            m_motion->start();
        }
        return QToolButton::event(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_clicked = true;
            update();
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_clicked = false;
            update();
        }
        QToolButton::mouseReleaseEvent(event);
    }

    void renderTemplateGlyph(QPainter &painter, const QRectF &target,
                             const QColor &color)
    {
        if (!m_templateRenderer || m_templateSvg.isEmpty()) return;
        if (m_templateColor != color) {
            QByteArray coloredSvg = m_templateSvg;
            coloredSvg.replace(QByteArrayLiteral("#000000"), color.name(QColor::HexRgb).toLatin1());
            if (!m_templateRenderer->load(coloredSvg)) return;
            m_templateColor = color;
        }
        m_templateRenderer->render(&painter, target);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool active = isChecked();
        const bool dark = property("darkMode").toBool();
        if (m_glyph == RailGlyph::TemplateGrid) {
            QConicalGradient flow(QPointF(width() / 2.0, height() / 2.0),
                                   -32.0 + m_flowAngle);
            flow.setColorAt(0.00, QColor(QStringLiteral("#f8f9fa")));
            flow.setColorAt(0.18, QColor(QStringLiteral("#8a949d")));
            flow.setColorAt(0.34, QColor(QStringLiteral("#ffffff")));
            flow.setColorAt(0.57, QColor(QStringLiteral("#a3acb3")));
            flow.setColorAt(0.78, QColor(QStringLiteral("#f7f8f9")));
            flow.setColorAt(0.92, QColor(QStringLiteral("#77828c")));
            flow.setColorAt(1.00, QColor(QStringLiteral("#f8f9fa")));
            QLinearGradient fill(0, 4, 0, height() - 4);
            fill.setColorAt(0.0, QColor(dark
                                            ? (active ? QStringLiteral("#263b4a")
                                                      : QStringLiteral("#2d3338"))
                                            : (active ? QStringLiteral("#eef6ff")
                                                      : QStringLiteral("#f7fcff"))));
            fill.setColorAt(1.0, QColor(dark
                                            ? (active ? QStringLiteral("#1c2a34")
                                                      : QStringLiteral("#15191c"))
                                            : (active ? QStringLiteral("#dceeff")
                                                      : QStringLiteral("#e6f3fb"))));
            painter.setBrush(QBrush(fill));
            painter.setPen(QPen(QBrush(flow), 1.6));
            painter.drawRoundedRect(QRectF(3.5, 4.0, 29.0, 30.0), 8, 8);
            painter.setPen(QPen(QColor(255, 255, 255, 185), 1.0));
            painter.drawLine(QPointF(8, 5.5), QPointF(27, 5.5));
        } else if (active && m_glyph != RailGlyph::Sleep) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(dark ? QStringLiteral("#303840")
                                         : QStringLiteral("#eaf1ff")));
            const QRectF activeRect = isTemplateGlyph(m_glyph)
                ? QRectF(4.5, 4.0, 27.0, 30.0)
                : QRectF(5.0, 4.0, 26.0, 28.0);
            painter.drawRoundedRect(activeRect, 8, 8);
        }

        const bool hover = m_hoverProgress > 0.3;
        const QColor ink = active || hover
            ? QColor(dark ? QStringLiteral("#8ac9ed")
                          : m_glyph == RailGlyph::TemplateGrid
                              ? (active ? QStringLiteral("#2f6feb")
                                        : QStringLiteral("#2b333a"))
                              : isTemplateGlyph(m_glyph)
                                  ? QStringLiteral("#5d9fc7")
                                  : QStringLiteral("#2f6feb"))
            : QColor(dark ? QStringLiteral("#c8d0d7")
                          : QStringLiteral("#98a2b3"));
        const QPointF c(width() / 2.0, height() / 2.0);
        painter.save();
        if (!isTemplateGlyph(m_glyph) && !m_clicked) {
            const qreal scale = 1.0 + (m_hoverProgress * 0.33);
            painter.translate(c);
            painter.scale(scale, scale);
            painter.translate(-c);
        }
        QPen pen(ink, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        if (isTemplateGlyph(m_glyph)) {
            // Render the original HTML SVG at the final size.  The previous
            // implementation transformed a hand-copied path after setting
            // the pen, which made the hover state visibly soft at fractional
            // scales.  QSvgRenderer keeps the source geometry and stroke
            // joins intact for every target size.
            const qreal scale = 1.0 + (m_hoverProgress * 0.33);
            const qreal iconSize = 20.0 * scale;
            renderTemplateGlyph(painter,
                                QRectF(c.x() - iconSize / 2.0,
                                       c.y() - iconSize / 2.0,
                                       iconSize, iconSize), ink);
        } else if (m_glyph == RailGlyph::PanelWorkbench) {
            painter.drawRoundedRect(QRectF(c.x() - 9, c.y() - 8, 18, 16), 2.2, 2.2);
            painter.drawLine(QPointF(c.x() - 9, c.y() - 3.5), QPointF(c.x() + 9, c.y() - 3.5));
            painter.drawLine(QPointF(c.x() - 3.5, c.y() - 3.5), QPointF(c.x() - 3.5, c.y() + 8));
        } else if (m_glyph == RailGlyph::PanelInspector) {
            painter.drawRoundedRect(QRectF(c.x() - 8.5, c.y() - 9, 17, 18), 2.2, 2.2);
            painter.drawLine(QPointF(c.x() + 3, c.y() - 9), QPointF(c.x() + 3, c.y() + 9));
            painter.drawLine(QPointF(c.x() - 5, c.y() - 4), QPointF(c.x() - 1, c.y() - 4));
            painter.drawLine(QPointF(c.x() - 5, c.y()), QPointF(c.x(), c.y()));
            painter.drawLine(QPointF(c.x() - 5, c.y() + 4), QPointF(c.x() - 2, c.y() + 4));
        } else if (m_glyph == RailGlyph::PanelOutput) {
            painter.drawRoundedRect(QRectF(c.x() - 9, c.y() - 7.5, 18, 15), 2.2, 2.2);
            painter.drawLine(QPointF(c.x() - 5, c.y() - 2.5), QPointF(c.x() - 2, c.y()));
            painter.drawLine(QPointF(c.x() - 2, c.y()), QPointF(c.x() - 5, c.y() + 2.5));
            painter.drawLine(QPointF(c.x(), c.y() + 3), QPointF(c.x() + 5, c.y() + 3));
        } else if (m_glyph == RailGlyph::PanelLibrary) {
            painter.drawRoundedRect(QRectF(c.x() - 9, c.y() - 8, 18, 16), 2.2, 2.2);
            painter.drawLine(QPointF(c.x() - 4, c.y() - 4), QPointF(c.x() + 4, c.y() - 4));
            painter.drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
            painter.drawLine(QPointF(c.x() - 4, c.y() + 4), QPointF(c.x() + 1, c.y() + 4));
        } else {
            QPainterPath moon;
            moon.addEllipse(QRectF(c.x() - 8, c.y() - 8, 16, 16));
            QPainterPath cutout;
            cutout.addEllipse(QRectF(c.x() - 2, c.y() - 9, 15, 15));
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawPath(moon.subtracted(cutout));
            painter.setPen(QPen(ink, 1.35, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(c.x() + 8, c.y() - 8), QPointF(c.x() + 8, c.y() - 3));
            painter.drawLine(QPointF(c.x() + 5.5, c.y() - 5.5), QPointF(c.x() + 10.5, c.y() - 5.5));
        }
        painter.restore();
    }

private:
    RailGlyph m_glyph;
    QVariantAnimation *m_motion = nullptr;
    QVariantAnimation *m_flowMotion = nullptr;
    qreal m_hoverProgress = 0.0;
    qreal m_flowAngle = 0.0;
    bool m_clicked = false;
    QSvgRenderer *m_templateRenderer = nullptr;
    QByteArray m_templateSvg;
    QColor m_templateColor;
};

QWidget *makeDockHeader(QDockWidget *dock, const QString &title)
{
    auto *header = new QWidget(dock);
    header->setObjectName(QStringLiteral("dockHeader"));
    header->setFixedHeight(30);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(10, 0, 5, 0);
    layout->setSpacing(2);
    auto *label = new QLabel(title, header);
    label->setObjectName(QStringLiteral("dockTitle"));
    layout->addWidget(label, 1);
    auto *floating = new QToolButton(header);
    floating->setObjectName(QStringLiteral("panelChromeButton"));
    floating->setIcon(makePanelIcon(PanelGlyph::Float));
    floating->setIconSize(QSize(16, 16));
    floating->setToolTip(QObject::tr("浮动 / 停靠"));
    floating->setCursor(Qt::PointingHandCursor);
    auto *close = new QToolButton(header);
    close->setObjectName(QStringLiteral("panelChromeButton"));
    close->setIcon(makePanelIcon(PanelGlyph::Close));
    close->setIconSize(QSize(16, 16));
    close->setToolTip(QObject::tr("关闭面板"));
    close->setCursor(Qt::PointingHandCursor);
    layout->addWidget(floating);
    layout->addWidget(close);
    QObject::connect(floating, &QToolButton::clicked, dock,
                     [dock] { dock->setFloating(!dock->isFloating()); });
    QObject::connect(close, &QToolButton::clicked, dock, &QDockWidget::hide);
    return header;
}

void installFloatingDockChrome(QDockWidget *dock, QWidget *innerHeader,
                               QWidget *collapsedTitleBar, const QString &title)
{
    if (!dock || !innerHeader || !collapsedTitleBar) return;
    auto *floatingHeader = new QWidget(dock);
    floatingHeader->setObjectName(QStringLiteral("dockFloatingTitleBar"));
    floatingHeader->setFixedHeight(30);
    auto *layout = new QHBoxLayout(floatingHeader);
    layout->setContentsMargins(10, 0, 5, 0);
    layout->setSpacing(2);
    auto *label = new QLabel(title, floatingHeader);
    label->setObjectName(QStringLiteral("dockTitle"));
    layout->addWidget(label, 1);
    auto *dockButton = new QToolButton(floatingHeader);
    dockButton->setObjectName(QStringLiteral("panelChromeButton"));
    dockButton->setIcon(makePanelIcon(PanelGlyph::Float));
    dockButton->setIconSize(QSize(16, 16));
    dockButton->setToolTip(QObject::tr("停靠面板"));
    dockButton->setCursor(Qt::PointingHandCursor);
    auto *closeButton = new QToolButton(floatingHeader);
    closeButton->setObjectName(QStringLiteral("panelChromeButton"));
    closeButton->setIcon(makePanelIcon(PanelGlyph::Close));
    closeButton->setIconSize(QSize(16, 16));
    closeButton->setToolTip(QObject::tr("关闭面板"));
    closeButton->setCursor(Qt::PointingHandCursor);
    layout->addWidget(dockButton);
    layout->addWidget(closeButton);
    floatingHeader->hide();

    QObject::connect(dockButton, &QToolButton::clicked, dock,
                     [dock] { dock->setFloating(false); });
    QObject::connect(closeButton, &QToolButton::clicked,
                     dock, &QDockWidget::hide);
    QObject::connect(dock, &QDockWidget::topLevelChanged, dock,
                     [dock, innerHeader, collapsedTitleBar, floatingHeader](const bool floating) {
        if (floating) {
            innerHeader->hide();
            dock->setTitleBarWidget(floatingHeader);
            floatingHeader->show();
        } else {
            floatingHeader->hide();
            dock->setTitleBarWidget(collapsedTitleBar);
            innerHeader->show();
        }
    });
}

void clearGrid(QGridLayout *grid)
{
    if (!grid) return;
    while (QLayoutItem *item = grid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowTitle(QStringLiteral("Serein Scope"));
    setMinimumSize(1100, 720);
    resize(1460, 900);
    setMouseTracking(true);
    qApp->installEventFilter(this);
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AnimatedDocks);

    prepareComponents();
    buildCentralShell();
    buildGlobalBar();
    buildDocks();
    buildMenu();
    connect(m_components, &ComponentManager::componentsChanged,
            this, &MainWindow::refreshComponentUi);

    for (const QString &entry : std::as_const(m_pendingLogs)) {
        m_output->appendPlainText(entry);
    }
    m_pendingLogs.clear();
    log(QStringLiteral("info"), tr("Serein Scope 1.0.1 插件宿主已启动"));

    refreshComponentUi();
    QString initialPage = HomeId;
    bool explicitPage = false;
    for (const QString &argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--open="))) {
            initialPage = argument.sliced(QStringLiteral("--open=").size());
            explicitPage = true;
        }
    }
    if (!explicitPage) {
        for (const ComponentSnapshot &component : m_components->components()) {
            if (component.loaded) {
                initialPage = component.metadata.id;
                break;
            }
        }
    }
    activatePage(initialPage);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("main/geometry")).toByteArray());
    const QByteArray dockState = settings.value(DockSettingsKey).toByteArray();
    const QByteArray centralDockState = settings.value(CentralDockSettingsKey).toByteArray();
    if (dockState.isEmpty() || !m_workbenchWindow
        || !m_workbenchWindow->restoreState(dockState, DockLayoutVersion)
        || centralDockState.isEmpty() || !m_centerWorkbenchWindow
        || !m_centerWorkbenchWindow->restoreState(centralDockState,
                                                   CentralDockLayoutVersion)) {
        applyDefaultDockLayout();
    }
    if (QCoreApplication::arguments().contains(QStringLiteral("--dark"))) {
        applyDarkMode(true);
    }
    if (QCoreApplication::arguments().contains(QStringLiteral("--qa-open-all-components"))) {
        for (const ComponentSnapshot &component : m_components->components()) {
            if (component.loaded) activateComponent(component.metadata.id, false);
        }
        // Opening every workspace must not change the page selected by the
        // acceptance command.  Otherwise the last alphabetic plugin hides
        // the very workspace the QA run asked us to inspect.
        if (explicitPage) activatePage(initialPage, false);
    }
    if (QCoreApplication::arguments().contains(QStringLiteral("--qa-show-panels"))) {
        m_navigationDock->show();
        m_inspectorDock->show();
        m_outputDock->show();
        QTimer::singleShot(180, this, [this] {
            if (!m_workbenchWindow) return;
            m_workbenchWindow->resizeDocks({m_navigationDock}, {220}, Qt::Horizontal);
            m_workbenchWindow->resizeDocks({m_inspectorDock}, {220}, Qt::Horizontal);
            m_centerWorkbenchWindow->resizeDocks({m_outputDock}, {156}, Qt::Vertical);
        });
    }
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    QSettings settings;
    settings.setValue(QStringLiteral("main/geometry"), saveGeometry());
    if (m_workbenchWindow) {
        settings.setValue(DockSettingsKey,
                          m_workbenchWindow->saveState(DockLayoutVersion));
    }
    if (m_centerWorkbenchWindow) {
        settings.setValue(CentralDockSettingsKey,
                          m_centerWorkbenchWindow->saveState(CentralDockLayoutVersion));
    }

    disconnect(m_components, nullptr, this, nullptr);
    QStringList openIds;
    for (const auto &workspace : m_workspaces) openIds.append(workspace->componentId);
    for (const QString &id : openIds) closeWorkspace(id);
    for (const ComponentSnapshot &component : m_components->components()) {
        if (component.loaded) m_components->unload(component.metadata.id, nullptr, false);
    }
}

bool MainWindow::runComponentCycleTest(const QString &componentId, QString *error)
{
    if (!m_components->load(componentId, error, false)) return false;
    IDevicePlugin *plugin = m_components->plugin(componentId);
    QWidget *workspace = plugin ? plugin->createWorkspace(nullptr) : nullptr;
    if (!workspace) {
        if (error) *error = tr("插件无法创建测试工作区");
        m_components->unload(componentId, nullptr, false);
        return false;
    }
    if (auto *managed = qobject_cast<IDeviceWorkspace *>(workspace)) managed->shutdown();
    delete workspace;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return m_components->unload(componentId, error, false);
}

void MainWindow::prepareComponents()
{
    QString storePath = ComponentManager::defaultStorePath();
    for (const QString &argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--component-dir="))) {
            storePath = argument.sliced(QStringLiteral("--component-dir=").size());
        }
    }
    m_components = new ComponentManager(this, storePath, true, this);
    m_components->discover();
    QDir packagedComponents(QCoreApplication::applicationDirPath());
    packagedComponents.cdUp();
    if (packagedComponents.cd(QStringLiteral("Components"))) {
        const QFileInfoList packages = packagedComponents.entryInfoList(
            {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name);
        for (const QFileInfo &package : packages) {
            QString error;
            if (m_components->importPackage(package.absoluteFilePath(), &error).isEmpty()) {
                log(QStringLiteral("warning"),
                    tr("内置插件包导入失败：%1").arg(error));
            }
        }
    }
    for (const QString &argument : QCoreApplication::arguments()) {
        if (!argument.startsWith(QStringLiteral("--import-component="))) continue;
        QString error;
        const QString path = argument.sliced(QStringLiteral("--import-component=").size());
        if (m_components->importPackage(path, &error).isEmpty()) {
            log(QStringLiteral("error"), tr("导入插件失败：%1").arg(error));
        }
    }
    m_components->restorePreferredComponents();
    for (const QString &argument : QCoreApplication::arguments()) {
        if (!argument.startsWith(QStringLiteral("--load-component="))) continue;
        QString error;
        const QString id = argument.sliced(QStringLiteral("--load-component=").size());
        if (!m_components->load(id, &error)) {
            log(QStringLiteral("error"), tr("加载插件失败：%1").arg(error));
        }
    }
}

void MainWindow::buildCentralShell()
{
    auto *shell = new QWidget;
    shell->setObjectName(QStringLiteral("rootShell"));
    shell->setProperty("darkMode", false);
    m_rootShell = shell;
    m_rootLayout = new QHBoxLayout(shell);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    m_activityRail = new QFrame(shell);
    m_activityRail->setObjectName(QStringLiteral("activityRail"));
    m_activityRail->setFixedWidth(64);
    m_activityRail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_rootLayout->addWidget(m_activityRail, 0);

    m_workbenchWindow = new QMainWindow(shell);
    m_workbenchWindow->setObjectName(QStringLiteral("workbenchHost"));
    m_workbenchWindow->setWindowFlags(Qt::Widget);
    m_workbenchWindow->setDockOptions(QMainWindow::AllowNestedDocks
                                      | QMainWindow::AnimatedDocks);
    m_workbenchWindow->setDocumentMode(true);
    // Keep every optional surface inset from the Sleep shell by the same
    // breathing room. Without this outer inset, the nested bottom dock
    // reaches the window edge when the side docks are hidden.
    m_workbenchWindow->setContentsMargins(6, 6, 6, 6);
    m_workbenchWindow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rootLayout->addWidget(m_workbenchWindow, 1);
    m_rootLayout->setStretch(1, 1);

    m_centerWorkbenchWindow = new QMainWindow(m_workbenchWindow);
    m_centerWorkbenchWindow->setObjectName(QStringLiteral("centerWorkbench"));
    m_centerWorkbenchWindow->setWindowFlags(Qt::Widget);
    m_centerWorkbenchWindow->setDockOptions(QMainWindow::AnimatedDocks);
    m_centerWorkbenchWindow->setDocumentMode(true);
    // The center host owns both the canvas and the bottom output dock. Put
    // the shared inset here so their left/right/bottom edges stay aligned
    // when optional side docks are toggled.
    m_centerWorkbenchWindow->setContentsMargins(6, 6, 6, 6);
    m_centerWorkbenchWindow->setSizePolicy(QSizePolicy::Expanding,
                                           QSizePolicy::Expanding);

    m_stack = new QStackedWidget;
    m_stack->setObjectName(QStringLiteral("workspaceStack"));
    m_stack->addWidget(buildHomePage());
    m_stack->addWidget(buildComponentCenter());
    auto *workspaceSurface = new QFrame(m_centerWorkbenchWindow);
    workspaceSurface->setObjectName(QStringLiteral("workspaceSurface"));
    auto *surfaceLayout = new QVBoxLayout(workspaceSurface);
    // The shared inset belongs to centerWorkbenchWindow, not this surface;
    // both the canvas and the bottom output dock must use the same baseline.
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    surfaceLayout->setSpacing(0);
    surfaceLayout->addWidget(m_stack, 1);
    m_centerWorkbenchWindow->setCentralWidget(workspaceSurface);
    m_workbenchWindow->setCentralWidget(m_centerWorkbenchWindow);

    setCentralWidget(shell);
    m_workbenchWindow->setVisible(true);
    rebuildWorkspaceTabs();
}

void MainWindow::buildGlobalBar()
{
    auto *host = new QWidget(this);
    host->setObjectName(QStringLiteral("globalMenuHost"));
    host->setFixedHeight(60);
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(8, 7, 8, 5);
    hostLayout->setSpacing(0);

    auto *top = new GlobalTitleBar(host);
    top->setObjectName(QStringLiteral("workbenchBar"));
    top->setFixedHeight(48);
    m_globalBar = top;
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(12, 0, 12, 0);
    topLayout->setSpacing(8);

    auto *identity = new QLabel(tr("Serein Scope  /  仪器工作台"), top);
    identity->setObjectName(QStringLiteral("topbarIdentity"));
    topLayout->addWidget(identity, 0, Qt::AlignVCenter);

    auto *identityDivider = new QFrame(top);
    identityDivider->setObjectName(QStringLiteral("topbarDivider"));
    identityDivider->setFixedSize(1, 24);
    topLayout->addWidget(identityDivider, 0, Qt::AlignVCenter);

    m_appMenuBar = new QMenuBar(top);
    m_appMenuBar->setObjectName(QStringLiteral("applicationMenu"));
    m_appMenuBar->setNativeMenuBar(false);
    m_appMenuBar->setCursor(Qt::PointingHandCursor);
    topLayout->addWidget(m_appMenuBar, 0, Qt::AlignVCenter);

    auto *menuDivider = new QFrame(top);
    menuDivider->setObjectName(QStringLiteral("topbarDivider"));
    menuDivider->setFixedSize(1, 24);
    topLayout->addWidget(menuDivider, 0, Qt::AlignVCenter);

    m_workspaceTabs = new QTabBar(top);
    m_workspaceTabs->setObjectName(QStringLiteral("hostWorkspaceTabs"));
    m_workspaceTabs->setDrawBase(false);
    m_workspaceTabs->setExpanding(false);
    m_workspaceTabs->setMovable(true);
    m_workspaceTabs->setTabsClosable(false);
    m_workspaceTabs->setElideMode(Qt::ElideRight);
    connect(m_workspaceTabs, &QTabBar::currentChanged, this, [this](const int index) {
        if (index < 0) return;
        const QString pageId = m_workspaceTabs->tabData(index).toString();
        if (!pageId.isEmpty() && pageId != m_activePage) activatePage(pageId);
    });

    auto *tabsHolder = new QHBoxLayout;
    tabsHolder->setContentsMargins(0, 0, 0, 0);
    tabsHolder->setSpacing(0);
    tabsHolder->addWidget(m_workspaceTabs, 0, Qt::AlignVCenter);
    tabsHolder->addStretch(1);
    topLayout->addLayout(tabsHolder, 1);

    auto *windowDivider = new QFrame(top);
    windowDivider->setObjectName(QStringLiteral("topbarDivider"));
    windowDivider->setFixedSize(1, 24);
    topLayout->addWidget(windowDivider, 0, Qt::AlignVCenter);

    const auto addWindowControl = [this, topLayout, top](const PanelGlyph glyph,
                                                         const QString &tooltip) {
        auto *button = new QToolButton(top);
        button->setObjectName(QStringLiteral("windowControl"));
        button->setIcon(makePanelIcon(glyph));
        button->setIconSize(QSize(14, 14));
        button->setToolTip(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        button->setAutoRaise(true);
        topLayout->addWidget(button);
        return button;
    };
    auto *minimize = addWindowControl(PanelGlyph::Minimize, tr("最小化"));
    auto *maximize = addWindowControl(PanelGlyph::Maximize, tr("最大化 / 还原"));
    auto *closeWindow = addWindowControl(PanelGlyph::Close, tr("关闭"));
    closeWindow->setProperty("windowAction", QStringLiteral("close"));
    connect(minimize, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(maximize, &QToolButton::clicked, this, [this] {
        if (isMaximized()) showNormal(); else showMaximized();
    });
    connect(closeWindow, &QToolButton::clicked, this, &QWidget::close);
    connect(top, &GlobalTitleBar::dragRequested, this, [this] {
        if (!isMaximized() && windowHandle()) windowHandle()->startSystemMove();
    });
    connect(top, &GlobalTitleBar::doubleClicked, this, [this] {
        if (isMaximized()) showNormal(); else showMaximized();
    });

    m_closeWorkspace = new QPushButton(this);
    m_closeWorkspace->hide();
    m_unloadComponent = new QPushButton(this);
    m_unloadComponent->hide();
    connect(m_closeWorkspace, &QPushButton::clicked, this, [this] {
        const QString id = m_activePage;
        if (closeWorkspace(id)) activatePage(HomeId);
    });
    connect(m_unloadComponent, &QPushButton::clicked, this, [this] {
        requestUnload(m_activePage);
    });

    hostLayout->addWidget(top);
    setMenuWidget(host);
}

QWidget *MainWindow::buildNavigationPanel()
{
    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("contextPanel"));
    panel->setMinimumWidth(165);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(14, 14, 14, 12);
    panelLayout->setSpacing(10);
    panelLayout->addWidget(makeDockHeader(m_navigationDock, tr("设备连接")));

    m_connectionModeStack = new QStackedWidget(panel);
    m_connectionModeStack->setObjectName(QStringLiteral("connectionModeStack"));
    auto *connectionPage = new QWidget(m_connectionModeStack);
    auto *connectionLayout = new QVBoxLayout(connectionPage);
    connectionLayout->setContentsMargins(0, 0, 0, 0);
    connectionLayout->setSpacing(8);
    auto *connectionHint = new QLabel(tr("选择活动工作区后，在这里控制串口租约。"),
                                      connectionPage);
    connectionHint->setObjectName(QStringLiteral("panelHint"));
    connectionHint->setWordWrap(true);
    connectionLayout->addWidget(connectionHint);

    auto *portLabel = new QLabel(tr("端口"), connectionPage);
    portLabel->setObjectName(QStringLiteral("fieldLabel"));
    connectionLayout->addWidget(portLabel);
    m_connectionPort = new QComboBox(connectionPage);
    m_connectionPort->setObjectName(QStringLiteral("connectionPort"));
    m_connectionPort->setPlaceholderText(tr("未发现串口"));
    m_connectionPort->setEditable(true);
    connectionLayout->addWidget(m_connectionPort);

    m_connectionBaudLabel = new QLabel(tr("波特率"), connectionPage);
    m_connectionBaudLabel->setObjectName(QStringLiteral("fieldLabel"));
    connectionLayout->addWidget(m_connectionBaudLabel);
    m_connectionBaud = new QSpinBox(connectionPage);
    m_connectionBaud->setObjectName(QStringLiteral("connectionBaud"));
    m_connectionBaud->setRange(300, 12000000);
    m_connectionBaud->setValue(2000000);
    m_connectionBaud->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connectionLayout->addWidget(m_connectionBaud);

    m_connectionTriggerBaudLabel = new QLabel(tr("触发波特率"), connectionPage);
    m_connectionTriggerBaudLabel->setObjectName(QStringLiteral("fieldLabel"));
    connectionLayout->addWidget(m_connectionTriggerBaudLabel);
    m_connectionTriggerBaud = new QSpinBox(connectionPage);
    m_connectionTriggerBaud->setObjectName(QStringLiteral("connectionTriggerBaud"));
    m_connectionTriggerBaud->setRange(300, 12000000);
    m_connectionTriggerBaud->setValue(2000000);
    m_connectionTriggerBaud->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connectionLayout->addWidget(m_connectionTriggerBaud);

    m_connectionReadBaudLabel = new QLabel(tr("读取波特率"), connectionPage);
    m_connectionReadBaudLabel->setObjectName(QStringLiteral("fieldLabel"));
    connectionLayout->addWidget(m_connectionReadBaudLabel);
    m_connectionReadBaud = new QSpinBox(connectionPage);
    m_connectionReadBaud->setObjectName(QStringLiteral("connectionReadBaud"));
    m_connectionReadBaud->setRange(300, 12000000);
    m_connectionReadBaud->setValue(5000000);
    m_connectionReadBaud->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connectionLayout->addWidget(m_connectionReadBaud);

    m_connectionEngineLabel = new QLabel(tr("数据引擎"), connectionPage);
    m_connectionEngineLabel->setObjectName(QStringLiteral("fieldLabel"));
    connectionLayout->addWidget(m_connectionEngineLabel);
    m_connectionEngine = new QComboBox(connectionPage);
    m_connectionEngine->setObjectName(QStringLiteral("connectionEngine"));
    m_connectionEngine->addItem(tr("逗号分隔数值"), QStringLiteral("delimited"));
    m_connectionEngine->addItem(tr("原始文本"), QStringLiteral("raw"));
    connectionLayout->addWidget(m_connectionEngine);

    auto *rawCaptureRow = new QHBoxLayout;
    rawCaptureRow->setContentsMargins(0, 0, 0, 0);
    rawCaptureRow->setSpacing(6);
    m_tnRawFrameLabel = new QLabel(tr("原始帧"), connectionPage);
    m_tnRawFrameLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_tnRawFrameCount = new QSpinBox(connectionPage);
    m_tnRawFrameCount->setObjectName(QStringLiteral("tn160RawFrameCount"));
    m_tnRawFrameCount->setRange(1, 256);
    m_tnRawFrameCount->setValue(32);
    m_tnRawFrameCount->setSuffix(tr(" 帧"));
    m_tnRawCapture = new QPushButton(tr("采集原始帧"), connectionPage);
    m_tnRawCapture->setObjectName(QStringLiteral("tn160RawCapture"));
    m_tnRawCapture->setProperty("kind", QStringLiteral("quiet"));
    rawCaptureRow->addWidget(m_tnRawFrameLabel);
    rawCaptureRow->addWidget(m_tnRawFrameCount, 0);
    rawCaptureRow->addWidget(m_tnRawCapture, 1);
    connectionLayout->addLayout(rawCaptureRow);

    auto *connectionActions = new QHBoxLayout;
    connectionActions->setContentsMargins(0, 0, 0, 0);
    connectionActions->setSpacing(6);
    m_connectionScan = new QPushButton(tr("扫描串口"), connectionPage);
    m_connectionScan->setProperty("kind", QStringLiteral("quiet"));
    m_connectionToggle = new QPushButton(tr("连接"), connectionPage);
    m_connectionToggle->setProperty("kind", QStringLiteral("primary"));
    connectionActions->addWidget(m_connectionScan);
    connectionActions->addWidget(m_connectionToggle, 1);
    connectionLayout->addLayout(connectionActions);

    m_connectionState = new QLabel(tr("未打开可连接工作区"), connectionPage);
    m_connectionState->setObjectName(QStringLiteral("sessionState"));
    connectionLayout->addWidget(m_connectionState, 0, Qt::AlignLeft);

    auto *divider = new QFrame(connectionPage);
    divider->setObjectName(QStringLiteral("panelDivider"));
    divider->setFixedHeight(1);
    connectionLayout->addSpacing(4);
    connectionLayout->addWidget(divider);
    auto *helpTitle = new QLabel(tr("工作区"), connectionPage);
    helpTitle->setObjectName(QStringLiteral("panelHintTitle"));
    auto *help = new QLabel(tr("顶部标签切换设备；TN160 内部功能在工作区页签中切换。"),
                            connectionPage);
    help->setObjectName(QStringLiteral("panelHint"));
    help->setWordWrap(true);
    connectionLayout->addWidget(helpTitle);
    connectionLayout->addWidget(help);
    connectionLayout->addStretch(1);
    m_connectionModeStack->addWidget(connectionPage);

    auto *firmwarePage = new QWidget(m_connectionModeStack);
    firmwarePage->setObjectName(QStringLiteral("firmwareActionPanel"));
    auto *firmwareLayout = new QVBoxLayout(firmwarePage);
    firmwareLayout->setContentsMargins(0, 0, 0, 0);
    firmwareLayout->setSpacing(8);
    auto *firmwareTitle = new QLabel(tr("固件烧录"), firmwarePage);
    firmwareTitle->setObjectName(QStringLiteral("connectionTitle"));
    firmwareLayout->addWidget(firmwareTitle);
    auto *firmwareHint = new QLabel(
        tr("选择 UF2 后开始监听；只处理新出现且符合容量限制的 BOOTSEL 卷。"),
        firmwarePage);
    firmwareHint->setObjectName(QStringLiteral("panelHint"));
    firmwareHint->setWordWrap(true);
    firmwareLayout->addWidget(firmwareHint);
    auto *firmwarePathLabel = new QLabel(tr("固件文件"), firmwarePage);
    firmwarePathLabel->setObjectName(QStringLiteral("fieldLabel"));
    firmwareLayout->addWidget(firmwarePathLabel);
    m_firmwarePath = new QLabel(tr("尚未选择 UF2"), firmwarePage);
    m_firmwarePath->setObjectName(QStringLiteral("firmwarePath"));
    m_firmwarePath->setWordWrap(true);
    m_firmwarePath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    firmwareLayout->addWidget(m_firmwarePath);
    auto *firmwareActions = new QHBoxLayout;
    firmwareActions->setContentsMargins(0, 0, 0, 0);
    firmwareActions->setSpacing(6);
    m_firmwareChoose = new QPushButton(tr("选择 UF2"), firmwarePage);
    m_firmwareChoose->setProperty("kind", QStringLiteral("quiet"));
    m_firmwareMonitor = new QPushButton(tr("开始监听"), firmwarePage);
    m_firmwareMonitor->setProperty("kind", QStringLiteral("primary"));
    firmwareActions->addWidget(m_firmwareChoose);
    firmwareActions->addWidget(m_firmwareMonitor, 1);
    firmwareLayout->addLayout(firmwareActions);
    m_firmwareState = new QLabel(tr("等待选择 UF2"), firmwarePage);
    m_firmwareState->setObjectName(QStringLiteral("sessionState"));
    firmwareLayout->addWidget(m_firmwareState, 0, Qt::AlignLeft);
    firmwareLayout->addStretch(1);
    m_connectionModeStack->addWidget(firmwarePage);
    panelLayout->addWidget(m_connectionModeStack, 1);

    m_navigation = new QListWidget(panel);
    m_navigation->setObjectName(QStringLiteral("pluginNav"));
    m_navigation->setVisible(false);
    connect(m_navigation, &QListWidget::currentItemChanged,
            this, &MainWindow::navigationChanged);
    panelLayout->addWidget(m_navigation);

    panelLayout->addStretch(1);
    auto *version = new QLabel(QStringLiteral("SEREIN SCOPE  1.0.1   ·   ABI 2"), panel);
    version->setObjectName(QStringLiteral("panelVersion"));
    panelLayout->addWidget(version);

    connect(m_connectionScan, &QPushButton::clicked, this, &MainWindow::scanConnectionPorts);
    connect(m_connectionToggle, &QPushButton::clicked,
            this, &MainWindow::toggleConnection);
    connect(m_connectionPort, &QComboBox::currentTextChanged, this, [this](const QString &port) {
        QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
        if (!workspace) return;
        QObject *target = nullptr;
        if (workspace->objectName() == QStringLiteral("serialScopeWorkspace")) {
            target = workspace;
        } else if (workspace->objectName() == QStringLiteral("instrumentWorkspace")) {
            int page = -1;
            QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                                      Q_RETURN_ARG(int, page));
            const QString pageObject = page == 2 ? QStringLiteral("tn160CalibrationPage")
                : page == 3 ? QStringLiteral("tn160CalibrationDataPage")
                             : QStringLiteral("tn160LivePage");
            target = workspace->findChild<QObject *>(pageObject);
        }
        if (target) QMetaObject::invokeMethod(target, "setPort", Qt::DirectConnection,
                                               Q_ARG(QString, port));
    });
    connect(m_connectionBaud, &QSpinBox::valueChanged, this, [this](const int baud) {
        QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
        if (!workspace) return;
        QObject *target = nullptr;
        if (workspace->objectName() == QStringLiteral("serialScopeWorkspace")) {
            target = workspace;
        } else if (workspace->objectName() == QStringLiteral("instrumentWorkspace")) {
            target = workspace->findChild<QObject *>(QStringLiteral("tn160LivePage"));
        }
        if (target) {
            QMetaObject::invokeMethod(target, "setBaud", Qt::DirectConnection,
                                      Q_ARG(int, baud));
        }
    });
    connect(m_connectionTriggerBaud, &QSpinBox::valueChanged, this, [this](const int baud) {
        QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
        if (!workspace || workspace->objectName() != QStringLiteral("instrumentWorkspace")) return;
        int page = -1;
        QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, page));
        const QString pageObject = page == 2 ? QStringLiteral("tn160CalibrationPage")
            : page == 3 ? QStringLiteral("tn160CalibrationDataPage") : QString();
        if (pageObject.isEmpty()) return;
        if (QObject *target = workspace->findChild<QObject *>(pageObject)) {
            QMetaObject::invokeMethod(target, "setTriggerBaud", Qt::DirectConnection,
                                      Q_ARG(int, baud));
        }
    });
    connect(m_connectionReadBaud, &QSpinBox::valueChanged, this, [this](const int baud) {
        QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
        if (!workspace || workspace->objectName() != QStringLiteral("instrumentWorkspace")) return;
        int page = -1;
        QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, page));
        const QString pageObject = page == 2 ? QStringLiteral("tn160CalibrationPage")
            : page == 3 ? QStringLiteral("tn160CalibrationDataPage") : QString();
        if (pageObject.isEmpty()) return;
        if (QObject *target = workspace->findChild<QObject *>(pageObject)) {
            QMetaObject::invokeMethod(target, "setReadBaud", Qt::DirectConnection,
                                      Q_ARG(int, baud));
        }
    });
    connect(m_connectionEngine, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int index) {
        QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
        if (!workspace) return;
        const QString engine = m_connectionEngine->itemData(index).toString();
        if (engine.isEmpty()) return;
        QObject *target = workspace->objectName() == QStringLiteral("serialScopeWorkspace")
            ? workspace : workspace->findChild<QObject *>(QStringLiteral("tn160LivePage"));
        if (target && workspace->objectName() == QStringLiteral("serialScopeWorkspace")) {
            QMetaObject::invokeMethod(target, "setEngine", Qt::DirectConnection,
                                       Q_ARG(QString, engine));
        }
    });
    connect(m_firmwareChoose, &QPushButton::clicked, this, [this] {
        QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
        if (!workspace) return;
        if (QObject *firmware = workspace->findChild<QObject *>(
                QStringLiteral("tn160FirmwarePage"))) {
            QMetaObject::invokeMethod(firmware, "chooseFirmware", Qt::QueuedConnection);
        }
    });
    connect(m_firmwareMonitor, &QPushButton::clicked, this, [this] {
        QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
        if (!workspace) return;
        if (QObject *firmware = workspace->findChild<QObject *>(
                QStringLiteral("tn160FirmwarePage"))) {
            QMetaObject::invokeMethod(firmware, "toggleMonitoring", Qt::QueuedConnection);
        }
    });
    connect(m_tnRawCapture, &QPushButton::clicked, this, [this] {
        QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
        if (!workspace || workspace->objectName() != QStringLiteral("instrumentWorkspace")) return;
        QObject *live = workspace->findChild<QObject *>(QStringLiteral("tn160LivePage"));
        if (!live) return;
        bool active = false;
        QMetaObject::invokeMethod(live, "rawCaptureActive", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, active));
        if (active) {
            QMetaObject::invokeMethod(live, "cancelRawCapture", Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(live, "startRawCapture", Qt::QueuedConnection,
                                      Q_ARG(int, m_tnRawFrameCount->value()));
        }
    });
    return panel;
}

QWidget *MainWindow::buildHomePage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("workbenchPage"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 18, 24, 24);
    outer->setSpacing(14);
    auto *toolbar = new QFrame;
    toolbar->setObjectName(QStringLiteral("pageToolbar"));
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    auto *hint = new QLabel(tr("已加载到内存的设备插件"));
    hint->setObjectName(QStringLiteral("muted"));
    toolbarLayout->addWidget(hint);
    toolbarLayout->addStretch();
    auto *manage = new QPushButton(tr("插件管理"));
    manage->setProperty("kind", QStringLiteral("primary"));
    connect(manage, &QPushButton::clicked, this,
            [this] { activatePage(ComponentsId); });
    toolbarLayout->addWidget(manage);
    outer->addWidget(toolbar);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("workbenchScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget;
    m_homeGrid = new QGridLayout(canvas);
    m_homeGrid->setContentsMargins(0, 0, 0, 0);
    m_homeGrid->setHorizontalSpacing(14);
    m_homeGrid->setVerticalSpacing(14);
    scroll->setWidget(canvas);
    outer->addWidget(scroll, 1);
    return page;
}

QWidget *MainWindow::buildComponentCenter()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("workbenchPage"));
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 18, 24, 24);
    outer->setSpacing(14);
    auto *toolbar = new QFrame;
    toolbar->setObjectName(QStringLiteral("pageToolbar"));
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    m_storePath = new QLabel;
    m_storePath->setObjectName(QStringLiteral("muted"));
    m_storePath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    toolbarLayout->addWidget(m_storePath, 1);
    auto *import = new QPushButton(tr("+ 导入插件包"));
    import->setProperty("kind", QStringLiteral("primary"));
    connect(import, &QPushButton::clicked, this, &MainWindow::importComponent);
    toolbarLayout->addWidget(import);
    outer->addWidget(toolbar);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("workbenchScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *canvas = new QWidget;
    m_componentGrid = new QGridLayout(canvas);
    m_componentGrid->setContentsMargins(0, 0, 0, 0);
    m_componentGrid->setHorizontalSpacing(14);
    m_componentGrid->setVerticalSpacing(14);
    scroll->setWidget(canvas);
    outer->addWidget(scroll, 1);
    return page;
}

void MainWindow::buildDocks()
{
    constexpr auto features = QDockWidget::DockWidgetClosable
        | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable;

    auto *rail = m_activityRail;
    if (!rail) return;
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(6, 8, 6, 8);
    railLayout->setSpacing(3);
    auto *surfaceRail = new QFrame(rail);
    surfaceRail->setObjectName(QStringLiteral("surfaceRail"));
    surfaceRail->setFixedSize(45, 161);
    auto *surfaceLayout = new QVBoxLayout(surfaceRail);
    surfaceLayout->setContentsMargins(2, 1, 2, 1);
    surfaceLayout->setSpacing(0);
    auto *templateGroup = new QButtonGroup(surfaceRail);
    templateGroup->setExclusive(true);
    const auto addTemplate = [surfaceRail, surfaceLayout, templateGroup](
                                  const RailGlyph glyph, const QString &tooltip) {
        auto *button = new RailButton(glyph, surfaceRail);
        button->setToolTip(tooltip);
        button->setProperty("railLayer", QStringLiteral("template"));
        templateGroup->addButton(button);
        surfaceLayout->addWidget(button, 0, Qt::AlignHCenter);
        return button;
    };
    auto *templateGrid = addTemplate(RailGlyph::TemplateGrid, tr("模板组件 1 · 工作区"));
    const auto addSurfaceDivider = [surfaceRail, surfaceLayout] {
        auto *divider = new QFrame(surfaceRail);
        divider->setObjectName(QStringLiteral("surfaceRailDivider"));
        divider->setFixedHeight(1);
        surfaceLayout->addWidget(divider);
    };
    addSurfaceDivider();
    auto *templateBars = addTemplate(RailGlyph::TemplateBars, tr("模板组件 2 · 信号视图"));
    addSurfaceDivider();
    auto *templateLayers = addTemplate(RailGlyph::TemplateLayers, tr("模板组件 3 · 层级视图"));
    addSurfaceDivider();
    auto *templateWallet = addTemplate(RailGlyph::TemplateWallet, tr("模板组件 4 · 数据容器"));
    templateGrid->setChecked(true);
    railLayout->addWidget(surfaceRail, 0, Qt::AlignHCenter);

    auto *panelRail = new QFrame(rail);
    panelRail->setObjectName(QStringLiteral("panelRail"));
    auto *panelLayout = new QVBoxLayout(panelRail);
    panelLayout->setContentsMargins(0, 5, 0, 0);
    panelLayout->setSpacing(3);
    m_navigationToggle = new RailButton(RailGlyph::PanelWorkbench, panelRail);
    m_navigationToggle->setToolTip(tr("设备连接面板"));
    m_outputToggle = new RailButton(RailGlyph::PanelOutput, panelRail);
    m_outputToggle->setToolTip(tr("输出终端面板"));
    m_inspectorToggle = new RailButton(RailGlyph::PanelInspector, panelRail);
    m_inspectorToggle->setToolTip(tr("数据与属性面板"));
    m_componentsToggle = new RailButton(RailGlyph::PanelLibrary, panelRail);
    m_componentsToggle->setToolTip(tr("组件库"));
    panelLayout->addWidget(m_navigationToggle, 0, Qt::AlignHCenter);
    panelLayout->addWidget(m_outputToggle, 0, Qt::AlignHCenter);
    panelLayout->addWidget(m_inspectorToggle, 0, Qt::AlignHCenter);
    panelLayout->addWidget(m_componentsToggle, 0, Qt::AlignHCenter);
    railLayout->addWidget(panelRail, 0, Qt::AlignHCenter);
    railLayout->addStretch(1);
    m_sleepButton = new RailButton(RailGlyph::Sleep, rail);
    m_sleepButton->setObjectName(QStringLiteral("sleepButton"));
    m_sleepButton->setToolTip(tr("Sleep · 切换深色材质"));
    railLayout->addSpacing(4);
    railLayout->addWidget(m_sleepButton, 0, Qt::AlignHCenter);
    m_navigationDock = new QDockWidget(tr("设备连接"), m_workbenchWindow);
    m_navigationDock->setObjectName(QStringLiteral("navigationDock"));
    m_navigationDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_navigationDock->setFeatures(features);
    auto *connectionTitleBar = new QWidget(m_navigationDock);
    connectionTitleBar->setFixedHeight(0);
    m_navigationDock->setTitleBarWidget(connectionTitleBar);
    m_navigationDock->setWidget(buildNavigationPanel());
    installFloatingDockChrome(m_navigationDock,
                              m_navigationDock->findChild<QWidget *>(QStringLiteral("dockHeader")),
                              connectionTitleBar, tr("设备连接"));
    // Device connection is a context/action surface. Keep it on the right
    // beside the data inspector so the left side remains the quiet canvas
    // edge and the two system panels have a single visual reading order.
    m_workbenchWindow->addDockWidget(Qt::RightDockWidgetArea, m_navigationDock);
    m_navigationDock->setMinimumWidth(165);

    m_componentLibraryDock = new QDockWidget(tr("组件库"), m_workbenchWindow);
    m_componentLibraryDock->setObjectName(QStringLiteral("componentLibraryDock"));
    m_componentLibraryDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_componentLibraryDock->setFeatures(features);
    auto *libraryTitleBar = new QWidget(m_componentLibraryDock);
    libraryTitleBar->setFixedHeight(0);
    m_componentLibraryDock->setTitleBarWidget(libraryTitleBar);
    auto *libraryPanel = new QWidget;
    libraryPanel->setObjectName(QStringLiteral("dockPanelContent"));
    auto *libraryLayout = new QVBoxLayout(libraryPanel);
    libraryLayout->setContentsMargins(12, 12, 12, 12);
    libraryLayout->setSpacing(8);
    libraryLayout->addWidget(makeDockHeader(m_componentLibraryDock, tr("组件库")));
    auto *libraryHint = new QLabel(tr("拖动到中央工作区以添加组件"));
    libraryHint->setObjectName(QStringLiteral("pageSubtitle"));
    libraryHint->setWordWrap(true);
    libraryLayout->addWidget(libraryHint);
    auto *libraryList = new ComponentLibraryList;
    m_componentLibraryList = libraryList;
    libraryList->setObjectName(QStringLiteral("componentLibraryList"));
    libraryList->setDragEnabled(true);
    libraryList->setDragDropMode(QAbstractItemView::DragOnly);
    libraryList->setSelectionMode(QAbstractItemView::SingleSelection);
    libraryList->setViewMode(QListView::IconMode);
    libraryList->setFlow(QListView::LeftToRight);
    libraryList->setMovement(QListView::Static);
    libraryList->setResizeMode(QListView::Adjust);
    libraryList->setIconSize(QSize(34, 34));
    libraryList->setGridSize(QSize(62, 64));
    libraryList->setSpacing(4);
    libraryList->setWordWrap(false);
    libraryLayout->addWidget(libraryList, 1);
    connect(libraryList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
        if (item) showLibraryComponent(item->data(Qt::UserRole).toString());
    });
    m_componentLibraryDock->setWidget(libraryPanel);
    m_workbenchWindow->addDockWidget(Qt::LeftDockWidgetArea, m_componentLibraryDock);
    m_componentLibraryDock->hide();
    installFloatingDockChrome(
        m_componentLibraryDock,
        libraryPanel->findChild<QWidget *>(QStringLiteral("dockHeader")),
        libraryTitleBar, tr("组件库"));

    m_outputDock = new QDockWidget(tr("输出终端"), m_centerWorkbenchWindow);
    m_outputDock->setObjectName(QStringLiteral("outputDock"));
    // The dock remains floatable/re-dockable for the existing panel
    // capability contract.  Its owner is the center workbench, so the
    // normal docked position cannot consume the side panels.
    m_outputDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_outputDock->setFeatures(features);
    auto *outputTitleBar = new QWidget(m_outputDock);
    outputTitleBar->setFixedHeight(0);
    m_outputDock->setTitleBarWidget(outputTitleBar);
    auto *outputPanel = new QWidget;
    outputPanel->setObjectName(QStringLiteral("dockPanelContent"));
    auto *outputLayout = new QVBoxLayout(outputPanel);
    outputLayout->setContentsMargins(8, 6, 8, 8);
    outputLayout->setSpacing(6);
    auto *outputDockHeader = makeDockHeader(m_outputDock, tr("输出终端"));
    auto *clear = new QPushButton(tr("清空"));
    clear->setProperty("kind", QStringLiteral("quiet"));
    clear->setToolTip(tr("清空终端输出"));
    clear->setCursor(Qt::PointingHandCursor);
    if (auto *headerLayout = qobject_cast<QHBoxLayout *>(outputDockHeader->layout())) {
        // Keep the action in the same chrome row as the terminal title and
        // dock controls. The former standalone hint row carried no action.
        headerLayout->insertWidget(1, clear);
    }
    outputLayout->addWidget(outputDockHeader);
    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->document()->setMaximumBlockCount(2000);
    connect(clear, &QPushButton::clicked, m_output, &QPlainTextEdit::clear);
    m_outputStack = new QStackedWidget(outputPanel);
    m_outputStack->setObjectName(QStringLiteral("outputContentStack"));
    m_outputStack->addWidget(m_output);
    m_outputTrendHost = new QWidget(m_outputStack);
    m_outputTrendHost->setObjectName(QStringLiteral("tn160TrendOutput"));
    m_outputTrendLayout = new QVBoxLayout(m_outputTrendHost);
    m_outputTrendLayout->setContentsMargins(0, 0, 0, 0);
    m_outputTrendLayout->setSpacing(0);
    m_outputTrendEmpty = new QLabel(tr("进入 TN160 实时诊断后，实时趋势会显示在这里。"),
                                    m_outputTrendHost);
    m_outputTrendEmpty->setObjectName(QStringLiteral("emptyState"));
    m_outputTrendEmpty->setAlignment(Qt::AlignCenter);
    m_outputTrendLayout->addWidget(m_outputTrendEmpty);
    m_outputStack->addWidget(m_outputTrendHost);
    m_outputStack->setCurrentWidget(m_output);
    outputLayout->addWidget(m_outputStack, 1);
    m_outputDock->setWidget(outputPanel);
    installFloatingDockChrome(m_outputDock,
                              outputPanel->findChild<QWidget *>(QStringLiteral("dockHeader")),
                              outputTitleBar, tr("输出终端"));
    m_centerWorkbenchWindow->addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);

    m_inspectorDock = new QDockWidget(tr("数据与属性"), m_workbenchWindow);
    m_inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    m_inspectorDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_inspectorDock->setFeatures(features);
    auto *inspectorTitleBar = new QWidget(m_inspectorDock);
    inspectorTitleBar->setFixedHeight(0);
    m_inspectorDock->setTitleBarWidget(inspectorTitleBar);
    auto *inspector = new QWidget;
    inspector->setObjectName(QStringLiteral("dockPanelContent"));
    auto *inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(12, 12, 12, 12);
    inspectorLayout->setSpacing(7);
    inspectorLayout->insertWidget(0, makeDockHeader(m_inspectorDock, tr("数据与属性")));
    auto *active = new QLabel(tr("当前组件"));
    active->setObjectName(QStringLiteral("sectionLabel"));
    m_inspectorName = new QLabel(tr("宿主概览"));
    m_inspectorName->setObjectName(QStringLiteral("cardTitle"));
    m_inspectorDescription = new QLabel(tr("从插件管理加载一个设备插件。"));
    m_inspectorDescription->setObjectName(QStringLiteral("pageSubtitle"));
    m_inspectorDescription->setWordWrap(true);
    m_inspectorMeta = new QLabel(tr("宿主 · C++20 · ABI 2"));
    m_inspectorMeta->setObjectName(QStringLiteral("chip"));
    m_inspectorCapabilities = new QLabel(tr("没有活动设备工作区"));
    m_inspectorCapabilities->setObjectName(QStringLiteral("pageSubtitle"));
    m_inspectorCapabilities->setWordWrap(true);
    inspectorLayout->addWidget(active);
    inspectorLayout->addWidget(m_inspectorName);
    inspectorLayout->addWidget(m_inspectorDescription);
    inspectorLayout->addSpacing(8);
    inspectorLayout->addWidget(m_inspectorMeta);
    inspectorLayout->addWidget(m_inspectorCapabilities);

    m_liveMetrics = new QWidget(inspector);
    m_liveMetrics->setObjectName(QStringLiteral("tn160InspectorMetrics"));
    m_liveMetrics->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *liveMetricsLayout = new QVBoxLayout(m_liveMetrics);
    liveMetricsLayout->setContentsMargins(0, 4, 0, 0);
    liveMetricsLayout->setSpacing(0);
    const auto addLiveMetric = [liveMetricsLayout](const QString &name,
                                                    QLabel **value,
                                                    const bool primary = false) {
        auto *row = new QFrame;
        row->setObjectName(primary ? QStringLiteral("metricPrimary")
                                   : QStringLiteral("metricRow"));
        row->setMaximumHeight(primary ? 64 : 28);
        QBoxLayout *rowLayout = primary
            ? static_cast<QBoxLayout *>(new QVBoxLayout(row))
            : static_cast<QBoxLayout *>(new QHBoxLayout(row));
        rowLayout->setContentsMargins(0, primary ? 0 : 2, 0, primary ? 8 : 2);
        rowLayout->setSpacing(primary ? 2 : 8);
        auto *label = new QLabel(name, row);
        label->setObjectName(primary ? QStringLiteral("metricPrimaryLabel")
                                     : QStringLiteral("metricLabel"));
        *value = new QLabel(primary ? QStringLiteral("--.-- °C")
                                    : QStringLiteral("—"), row);
        (*value)->setObjectName(primary ? QStringLiteral("metricPrimaryValue")
                                        : QStringLiteral("metricValue"));
        (*value)->setAlignment(primary ? Qt::AlignLeft | Qt::AlignVCenter
                                       : Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(label);
        rowLayout->addWidget(*value, 1);
        liveMetricsLayout->addWidget(row);
    };
    addLiveMetric(QStringLiteral("CENTER 5×5"), &m_liveCenter, true);
    addLiveMetric(QStringLiteral("NTC / 板载"), &m_liveNtc);
    addLiveMetric(QStringLiteral("CENTER ADU"), &m_liveCenterAdu);
    addLiveMetric(QStringLiteral("VTEMP ADU"), &m_liveVtemp);
    addLiveMetric(tr("帧率"), &m_liveFps);
    m_liveMetrics->hide();
    inspectorLayout->addWidget(m_liveMetrics);
    inspectorLayout->addStretch();
    m_inspectorDock->setWidget(inspector);
    installFloatingDockChrome(m_inspectorDock,
                              inspector->findChild<QWidget *>(QStringLiteral("dockHeader")),
                              inspectorTitleBar, tr("数据与属性"));
    m_inspectorDock->setMinimumWidth(165);
    m_workbenchWindow->addDockWidget(Qt::LeftDockWidgetArea, m_inspectorDock);

    connect(m_navigationToggle, &QToolButton::clicked, m_navigationDock,
            [this] {
                const bool visible = m_navigationToggle->isChecked();
                if (visible) m_componentLibraryDock->hide();
                m_navigationDock->setVisible(visible);
            });
    connect(m_inspectorToggle, &QToolButton::clicked, m_inspectorDock,
            [this] { m_inspectorDock->setVisible(m_inspectorToggle->isChecked()); });
    connect(m_outputToggle, &QToolButton::clicked, m_outputDock,
            [this] { m_outputDock->setVisible(m_outputToggle->isChecked()); });
    connect(m_componentsToggle, &QToolButton::clicked, this,
            [this] {
                if (!m_componentLibraryDock) return;
                const bool visible = !m_componentLibraryDock->isVisible();
                if (visible) m_navigationDock->hide();
                m_componentLibraryDock->setVisible(visible);
                if (visible) m_componentLibraryDock->raise();
            });
    connect(m_navigationDock, &QDockWidget::visibilityChanged,
            m_navigationToggle, &QToolButton::setChecked);
    connect(m_inspectorDock, &QDockWidget::visibilityChanged,
            m_inspectorToggle, &QToolButton::setChecked);
    connect(m_outputDock, &QDockWidget::visibilityChanged,
            m_outputToggle, &QToolButton::setChecked);
    connect(m_componentLibraryDock, &QDockWidget::visibilityChanged,
            m_componentsToggle, &QToolButton::setChecked);
    connect(m_sleepButton, &QToolButton::clicked, this, &MainWindow::toggleDarkMode);

    connect(templateGrid, &QToolButton::clicked, this, [this] {
        setStatus(tr("模板组件 1 · 工作区"));
        m_componentLibraryDock->hide();
        m_navigationDock->show();
    });
    connect(templateBars, &QToolButton::clicked, this, [this] {
        setStatus(tr("模板组件 2 · 信号视图"));
        m_outputDock->show();
    });
    connect(templateLayers, &QToolButton::clicked, this, [this] {
        setStatus(tr("模板组件 3 · 层级视图"));
        m_inspectorDock->show();
    });
    connect(templateWallet, &QToolButton::clicked, this, [this] {
        setStatus(tr("模板组件 4 · 数据容器"));
        m_componentLibraryDock->hide();
        m_navigationDock->show();
    });
    refreshConnectionPanel();
}

void MainWindow::buildMenu()
{
    auto *workspace = new QMenu(this);
    auto *home = workspace->addAction(tr("返回开始"));
    home->setShortcut(QKeySequence(QStringLiteral("Ctrl+Home")));
    connect(home, &QAction::triggered, this, [this] { activatePage(HomeId); });
    auto *components = workspace->addAction(tr("打开插件管理"));
    components->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(components, &QAction::triggered, this,
            [this] { activatePage(ComponentsId); });
    workspace->addSeparator();
    auto *import = workspace->addAction(tr("导入组件包…"));
    connect(import, &QAction::triggered, this, &MainWindow::importComponent);
    auto *close = workspace->addAction(tr("关闭当前工作区"));
    connect(close, &QAction::triggered, m_closeWorkspace, &QPushButton::click);
    auto *unload = workspace->addAction(tr("卸载当前组件"));
    connect(unload, &QAction::triggered, m_unloadComponent, &QPushButton::click);
    workspace->addSeparator();
    auto *view = workspace->addMenu(tr("面板"));
    view->addAction(m_inspectorDock->toggleViewAction());
    view->addAction(m_outputDock->toggleViewAction());
    auto *reset = workspace->addAction(tr("恢复默认布局"));
    connect(reset, &QAction::triggered, this, &MainWindow::resetDockLayout);
    workspace->addSeparator();
    auto *quit = workspace->addAction(tr("退出"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    if (m_appMenuBar) {
        auto *file = m_appMenuBar->addMenu(tr("文件"));
        file->addAction(home);
        file->addAction(components);
        file->addAction(import);
        file->addSeparator();
        file->addAction(quit);

        auto *edit = m_appMenuBar->addMenu(tr("编辑"));
        edit->addAction(reset);

        auto *viewBar = m_appMenuBar->addMenu(tr("视图"));
        viewBar->addAction(m_inspectorDock->toggleViewAction());
        viewBar->addAction(m_outputDock->toggleViewAction());

        auto *tools = m_appMenuBar->addMenu(tr("工具"));
        tools->addAction(import);
        tools->addAction(reset);
        tools->addSeparator();
        tools->addAction(close);
        tools->addAction(unload);

        auto *help = m_appMenuBar->addMenu(tr("帮助"));
        auto *about = help->addAction(tr("关于 Serein Scope"));
        connect(about, &QAction::triggered, this, [this] {
            QMessageBox::about(this, tr("关于 Serein Scope"),
                               tr("Serein Scope\nC++20 · Qt 6 · Plugin ABI 2"));
        });
    }
}

void MainWindow::navigationChanged()
{
    if (QListWidgetItem *item = m_navigation->currentItem()) {
        activatePage(item->data(Qt::UserRole).toString(), false);
    }
}

void MainWindow::importComponent()
{
    const QString source = QFileDialog::getOpenFileName(
        this, tr("导入 Serein Scope 插件"), componentPackageStartPath(),
        tr("Serein 插件 (*.sscomponent);;动态插件 (*.dll *.so *.dylib);;所有文件 (*)"));
    if (source.isEmpty()) return;
    QString error;
    const QString id = m_components->importPackage(source, &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("导入失败"), error);
        return;
    }
    activatePage(ComponentsId);
    setStatus(tr("插件包已安装 · 点击“加载插件”后才会进入内存"));
}

void MainWindow::refreshComponentUi()
{
    if (!m_navigation || !m_homeGrid || !m_componentGrid) return;
    rebuildNavigation();
    rebuildWorkspaceTabs();
    rebuildCards(m_homeGrid, true, false);
    rebuildCards(m_componentGrid, false, true);
    if (m_storePath) m_storePath->setText(tr("插件仓库：%1").arg(m_components->storePath()));

    int loaded = 0;
    const QList<ComponentSnapshot> installed = m_components->components();
    for (const ComponentSnapshot &component : installed) {
        if (component.loaded) ++loaded;
    }
    if (m_buildInfo) {
        m_buildInfo->setText(tr("C++20 · Qt %1 · 插件 %2/%3")
                                 .arg(QString::fromLatin1(qVersion()))
                                 .arg(loaded)
                                 .arg(installed.size()));
    }
    if (m_activePage != HomeId && m_activePage != ComponentsId
        && !m_components->isLoaded(m_activePage)) {
        activatePage(ComponentsId);
    }
}

void MainWindow::rebuildNavigation()
{
    const QSignalBlocker blocker(m_navigation);
    m_navigation->clear();
    auto *home = new QListWidgetItem(tr("开始"));
    home->setData(Qt::UserRole, HomeId);
    m_navigation->addItem(home);
    auto *components = new QListWidgetItem(tr("组件库"));
    components->setData(Qt::UserRole, ComponentsId);
    m_navigation->addItem(components);
    selectNavigation(m_activePage.isEmpty() ? HomeId : m_activePage);
}

void MainWindow::rebuildWorkspaceTabs()
{
    if (!m_workspaceTabs) return;
    const QSignalBlocker blocker(m_workspaceTabs);
    while (m_workspaceTabs->count() > 0) m_workspaceTabs->removeTab(0);
    const auto attachCloseButton = [this](const int tab, const QString &pageId) {
        auto *close = new QToolButton(m_workspaceTabs);
        close->setObjectName(QStringLiteral("hostTabClose"));
        close->setText(QStringLiteral("×"));
        close->setToolTip(tr("关闭工作区"));
        close->setCursor(Qt::PointingHandCursor);
        connect(close, &QToolButton::clicked, this, [this, pageId] {
            if (pageId == ComponentsId) {
                activatePage(HomeId);
            } else if (closeWorkspace(pageId)) {
                activatePage(HomeId);
            }
        });
        m_workspaceTabs->setTabButton(tab, QTabBar::RightSide, close);
    };
    int index = m_workspaceTabs->addTab(QStringLiteral("Scope"));
    m_workspaceTabs->setTabData(index, HomeId);
    m_workspaceTabs->setTabButton(index, QTabBar::RightSide, nullptr);
    if (m_activePage == ComponentsId) {
        index = m_workspaceTabs->addTab(tr("组件库"));
        m_workspaceTabs->setTabData(index, ComponentsId);
        attachCloseButton(index, ComponentsId);
    }
    for (const auto &workspace : m_workspaces) {
        if (!workspace->widget) continue;
        const ComponentSnapshot component = m_components->component(workspace->componentId);
        index = m_workspaceTabs->addTab(component.metadata.name);
        m_workspaceTabs->setTabData(index, workspace->componentId);
        m_workspaceTabs->setTabToolTip(index, component.metadata.description);
        attachCloseButton(index, workspace->componentId);
    }
    selectWorkspaceTab(m_activePage.isEmpty() ? HomeId : m_activePage);
}

void MainWindow::selectWorkspaceTab(const QString &id)
{
    if (!m_workspaceTabs) return;
    const QSignalBlocker blocker(m_workspaceTabs);
    for (int index = 0; index < m_workspaceTabs->count(); ++index) {
        if (m_workspaceTabs->tabData(index).toString() == id) {
            m_workspaceTabs->setCurrentIndex(index);
            return;
        }
    }
}

void MainWindow::rebuildCards(QGridLayout *grid, const bool loadedOnly,
                              const bool managementActions)
{
    clearGrid(grid);
    const int availableWidth = grid && grid->parentWidget()
        ? grid->parentWidget()->width() : 0;
    // Two cards are comfortable only while the scroll canvas can give each
    // card a real working width. When side docks reduce the canvas, switch to
    // one column instead of letting the action row clip underneath a dock.
    const int columns = availableWidth >= 680 ? 2 : 1;
    if (grid == m_homeGrid) m_homeCardColumns = columns;
    if (grid == m_componentGrid) m_componentCardColumns = columns;
    int index = 0;
    for (const ComponentSnapshot &component : m_components->components()) {
        if (loadedOnly && !component.loaded) continue;
        auto *card = new PluginCard(component.metadata, component.loaded,
                                    isWorkspaceOpen(component.metadata.id),
                                    managementActions);
        connect(card, &PluginCard::primaryRequested,
                this, &MainWindow::requestPrimaryAction);
        connect(card, &PluginCard::unloadRequested,
                this, &MainWindow::requestUnload);
        connect(card, &PluginCard::removeRequested,
                this, &MainWindow::requestRemove);
        grid->addWidget(card, index / columns, index % columns);
        ++index;
    }
    if (index == 0) {
        auto *empty = new QLabel(loadedOnly
            ? tr("当前没有已加载插件\n打开“插件管理”，导入并加载 TN160 或其他设备插件。")
            : tr("插件仓库为空\n点击右上角“+ 导入插件包”，选择独立 .sscomponent 文件。"));
        empty->setObjectName(QStringLiteral("emptyState"));
        empty->setAlignment(Qt::AlignCenter);
        grid->addWidget(empty, 0, 0, 1, 2);
    }
    for (int column = 0; column < columns; ++column) {
        grid->setColumnStretch(column, 1);
    }
    grid->setRowStretch(index > 0 ? (index - 1) / columns + 1 : 0, 1);
}

void MainWindow::activatePage(const QString &pageId, const bool synchronizeNavigation)
{
    if (pageId == HomeId || pageId.isEmpty()) {
        m_activePage = HomeId;
        m_stack->setCurrentIndex(0);
        updateInspector(nullptr);
        m_closeWorkspace->hide();
        m_unloadComponent->hide();
        setStatus(tr("就绪 · 从组件库加载仪器"));
    } else if (pageId == ComponentsId) {
        m_activePage = ComponentsId;
        m_stack->setCurrentIndex(1);
        updateInspector(nullptr);
        m_closeWorkspace->hide();
        m_unloadComponent->hide();
        setStatus(tr("组件库 · 安装与运行时管理"));
    } else {
        activateComponent(pageId, synchronizeNavigation);
        return;
    }
    rebuildWorkspaceTabs();
    if (synchronizeNavigation) selectNavigation(m_activePage);
    selectWorkspaceTab(m_activePage);
    refreshConnectionPanel();
    synchronizeContextPanels();
}

void MainWindow::activateComponent(const QString &componentId,
                                   const bool synchronizeNavigation)
{
    QString error;
    if (!m_components->isLoaded(componentId)
        && !m_components->load(componentId, &error)) {
        log(QStringLiteral("error"), tr("加载插件失败：%1").arg(error));
        QMessageBox::warning(this, tr("插件加载失败"), error);
        activatePage(ComponentsId);
        return;
    }
    const ComponentSnapshot component = m_components->component(componentId);
    WorkspaceRecord *record = findWorkspace(componentId);
    if (!record) {
        IDevicePlugin *plugin = m_components->plugin(componentId);
        QWidget *workspace = plugin ? plugin->createWorkspace(m_stack) : nullptr;
        if (!workspace) {
            log(QStringLiteral("error"), tr("插件未能创建设备工作区：%1").arg(component.metadata.name));
            return;
        }
        auto created = std::make_unique<WorkspaceRecord>();
        created->componentId = componentId;
        created->widget = workspace;
        created->stackIndex = m_stack->addWidget(workspace);
        record = created.get();
        m_workspaces.emplace_back(std::move(created));
        if (auto *tabs = workspace->findChild<QTabBar *>(QStringLiteral("workspaceTabs"))) {
            connect(tabs, &QTabBar::currentChanged, this,
                    [this](const int) {
                        refreshConnectionPanel();
                        synchronizeContextPanels();
                    });
        }
        if (workspace->objectName() == QStringLiteral("serialScopeWorkspace")) {
            connect(workspace, SIGNAL(connectionStateChanged(QString,QString)),
                    this, SLOT(updateConnectionState(QString,QString)));
        } else if (QObject *live = workspace->findChild<QObject *>(QStringLiteral("tn160LivePage"))) {
            if (QObject *firmware = workspace->findChild<QObject *>(
                    QStringLiteral("tn160FirmwarePage"))) {
                connect(firmware, SIGNAL(firmwareStateChanged(QString,QString,bool)),
                        this, SLOT(updateFirmwareState(QString,QString,bool)));
            }
            connect(live, SIGNAL(connectionStateChanged(QString,QString)),
                    this, SLOT(updateConnectionState(QString,QString)));
            connect(live, SIGNAL(rawCaptureStateChanged(QString,QString)),
                    this, SLOT(updateRawCaptureState(QString,QString)));
            connect(live, SIGNAL(telemetryChanged(QString,QString,QString,QString,QString)),
                    this, SLOT(updateTn160Telemetry(QString,QString,QString,QString,QString)));
            if (QObject *calibrate = workspace->findChild<QObject *>(
                    QStringLiteral("tn160CalibrationPage"))) {
                connect(calibrate, SIGNAL(transferStateChanged(QString,QString)),
                        this, SLOT(updateCalibrationState(QString,QString)));
            }
            if (QObject *dump = workspace->findChild<QObject *>(
                    QStringLiteral("tn160CalibrationDataPage"))) {
                connect(dump, SIGNAL(transferStateChanged(QString,QString)),
                        this, SLOT(updateCalibrationState(QString,QString)));
            }
        }
        if (m_sleepButton && m_sleepButton->isChecked()) applyDarkMode(true);
        log(QStringLiteral("info"), tr("已创建设备工作区：%1").arg(component.metadata.name));
        refreshComponentUi();
    }
    m_activePage = componentId;
    m_stack->setCurrentWidget(record->widget);
    updateInspector(&component.metadata);
    setStatus(tr("%1 · 工作区运行中").arg(component.metadata.name));
    rebuildWorkspaceTabs();
    if (synchronizeNavigation) selectNavigation(componentId);
    selectWorkspaceTab(componentId);
    refreshConnectionPanel();
    synchronizeContextPanels();
    QTimer::singleShot(120, this, &MainWindow::refreshConnectionPanel);
}

void MainWindow::requestPrimaryAction(const QString &componentId)
{
    if (!m_components->isLoaded(componentId)) {
        QString error;
        if (!m_components->load(componentId, &error)) {
            QMessageBox::warning(this, tr("加载失败"), error);
            return;
        }
        activateComponent(componentId);
        return;
    }
    activateComponent(componentId);
}

void MainWindow::requestUnload(const QString &componentId)
{
    if (componentId == HomeId || componentId == ComponentsId || componentId.isEmpty()) return;
    if (isWorkspaceOpen(componentId)) {
        activatePage(ComponentsId);
        closeWorkspace(componentId);
    }
    QString error;
    if (!m_components->unload(componentId, &error)) {
        QMessageBox::warning(this, tr("卸载失败"), error);
        return;
    }
    activatePage(ComponentsId);
}

void MainWindow::requestRemove(const QString &componentId)
{
    const ComponentSnapshot component = m_components->component(componentId);
    if (QMessageBox::question(this, tr("移除插件"),
            tr("确定从本机插件仓库移除“%1”吗？\n原始插件包不会被删除。")
                .arg(component.metadata.name)) != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!m_components->removeInstalled(componentId, &error)) {
        QMessageBox::warning(this, tr("移除失败"), error);
    }
}

bool MainWindow::closeWorkspace(const QString &componentId)
{
    WorkspaceRecord *record = findWorkspace(componentId);
    if (!record || !record->widget) return true;
    QWidget *widget = record->widget;
    if (m_attachedTn160TrendOwner == widget
        || (m_attachedTn160TrendOwner && widget->isAncestorOf(m_attachedTn160TrendOwner))) {
        detachTn160Trend();
    }
    if (auto *managed = qobject_cast<IDeviceWorkspace *>(widget)) managed->shutdown();
    m_stack->removeWidget(widget);
    delete widget;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    std::erase_if(m_workspaces, [&componentId](const auto &candidate) {
        return candidate->componentId == componentId;
    });
    log(QStringLiteral("info"), tr("设备工作区已关闭：%1").arg(componentId));
    refreshComponentUi();
    return true;
}

bool MainWindow::isWorkspaceOpen(const QString &componentId) const
{
    for (const auto &workspace : m_workspaces) {
        if (workspace->componentId == componentId && workspace->widget) return true;
    }
    return false;
}

MainWindow::WorkspaceRecord *MainWindow::findWorkspace(const QString &componentId)
{
    for (const auto &workspace : m_workspaces) {
        if (workspace->componentId == componentId) return workspace.get();
    }
    return nullptr;
}

const MainWindow::WorkspaceRecord *MainWindow::findWorkspace(const QString &componentId) const
{
    for (const auto &workspace : m_workspaces) {
        if (workspace->componentId == componentId) return workspace.get();
    }
    return nullptr;
}

void MainWindow::updateInspector(const PluginMetadata *metadata)
{
    if (!metadata) {
        m_inspectorName->setText(m_activePage == ComponentsId ? tr("插件管理") : tr("宿主概览"));
        m_inspectorDescription->setText(m_activePage == ComponentsId
            ? tr("插件包安装在用户目录；只有点击加载后 DLL 才进入进程。")
            : tr("从插件管理加载一个设备插件。"));
        m_inspectorMeta->setText(tr("宿主 · C++20 · ABI 2"));
        m_inspectorCapabilities->setText(tr("没有活动设备工作区"));
        return;
    }
    m_inspectorName->setText(metadata->name);
    m_inspectorDescription->setText(metadata->description);
    m_inspectorMeta->setText(tr("%1 · v%2 · ABI %3")
                                 .arg(metadata->category.toUpper(), metadata->version)
                                 .arg(metadata->interfaceVersion));
    m_inspectorCapabilities->setText(tr("能力\n%1")
        .arg(metadata->capabilities.join(QStringLiteral("  ·  "))));
}

void MainWindow::selectNavigation(const QString &id)
{
    if (!m_navigation) return;
    const QSignalBlocker blocker(m_navigation);
    for (int row = 0; row < m_navigation->count(); ++row) {
        if (m_navigation->item(row)->data(Qt::UserRole).toString() == id) {
            m_navigation->setCurrentRow(row);
            return;
        }
    }
    m_navigation->setCurrentRow(id == ComponentsId ? 1 : (id == HomeId ? 0 : -1));
}

QString MainWindow::componentPackageStartPath() const
{
    QDir sibling(QCoreApplication::applicationDirPath());
    sibling.cdUp();
    if (sibling.cd(QStringLiteral("Components"))) return sibling.absolutePath();
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return downloads.isEmpty() ? QDir::homePath() : downloads;
}

void MainWindow::log(const QString &level, const QString &message)
{
    const QString line = QStringLiteral("%1  %2  %3")
                             .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                  level.toUpper().leftJustified(7), message);
    if (m_output) m_output->appendPlainText(line);
    else m_pendingLogs.append(line);
}

void MainWindow::setStatus(const QString &message)
{
    if (m_statusText) m_statusText->setText(message);
}

void MainWindow::resetDockLayout()
{
    applyDefaultDockLayout();
    log(QStringLiteral("info"), tr("已恢复默认面板布局"));
}

QObject *MainWindow::connectionTargetForCurrentWorkspace() const
{
    QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
    if (!workspace) return nullptr;
    if (workspace->objectName() == QStringLiteral("serialScopeWorkspace")) {
        return workspace;
    }
    if (workspace->objectName() != QStringLiteral("instrumentWorkspace")) {
        return nullptr;
    }
    int page = -1;
    QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                              Q_RETURN_ARG(int, page));
    const QString pageObject = page == 1 ? QStringLiteral("tn160LivePage")
        : page == 2 ? QStringLiteral("tn160CalibrationPage")
        : page == 3 ? QStringLiteral("tn160CalibrationDataPage") : QString();
    return pageObject.isEmpty() ? nullptr : workspace->findChild<QObject *>(pageObject);
}

bool MainWindow::connectionTargetActive(QObject *target) const
{
    if (!target) return false;
    bool active = false;
    const QString name = target->objectName();
    const char *method = name == QStringLiteral("tn160CalibrationPage")
            || name == QStringLiteral("tn160CalibrationDataPage")
        ? "transferActive" : "connectionActive";
    return QMetaObject::invokeMethod(target, method, Qt::DirectConnection,
                                     Q_RETURN_ARG(bool, active)) && active;
}

void MainWindow::setConnectionConfigurationEnabled(const bool canConnect,
                                                   const bool serial,
                                                   const bool calibration,
                                                   const bool locked)
{
    const bool enabled = canConnect && !locked;
    if (m_connectionPort) m_connectionPort->setEnabled(enabled);
    if (m_connectionBaud) m_connectionBaud->setEnabled(enabled && !calibration);
    if (m_connectionTriggerBaud) m_connectionTriggerBaud->setEnabled(enabled && calibration);
    if (m_connectionReadBaud) m_connectionReadBaud->setEnabled(enabled && calibration);
    if (m_connectionEngine) m_connectionEngine->setEnabled(enabled && serial);
    if (m_connectionScan) m_connectionScan->setEnabled(enabled);
}

void MainWindow::scanConnectionPorts()
{
    const int detectedCount = SerialTransport::availablePorts().size();
    refreshConnectionPanel();
    if (!m_connectionState || !m_connectionScan || !m_connectionScan->isEnabled()) {
        return;
    }
    const QString text = detectedCount > 0
        ? tr("扫描到 %1 个串口").arg(detectedCount)
        : tr("未发现串口；可手动输入 COMx");
    m_connectionState->setText(text);
    m_connectionState->setProperty("state", detectedCount > 0
                                                ? QStringLiteral("installed")
                                                : QStringLiteral("error"));
    m_connectionState->style()->unpolish(m_connectionState);
    m_connectionState->style()->polish(m_connectionState);
    log(detectedCount > 0 ? QStringLiteral("info") : QStringLiteral("warning"), text);
}

void MainWindow::refreshConnectionPanel()
{
    if (!m_connectionPort) return;
    for (QWidget *widget : {static_cast<QWidget *>(m_tnRawFrameLabel),
                           static_cast<QWidget *>(m_tnRawFrameCount),
                           static_cast<QWidget *>(m_tnRawCapture)}) {
        if (widget) widget->setVisible(false);
    }
    QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
    int tn160Page = -1;
    if (workspace && workspace->objectName() == QStringLiteral("instrumentWorkspace")) {
        QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, tn160Page));
    }

    const bool firmware = workspace
        && workspace->objectName() == QStringLiteral("instrumentWorkspace")
        && tn160Page == 0;
    if (m_connectionModeStack) m_connectionModeStack->setCurrentIndex(firmware ? 1 : 0);
    if (!firmware) updateFirmwareState(QString(), QString(), false);
    if (m_navigationDock) {
        const QString dockTitle = firmware ? tr("固件烧录") : tr("设备连接");
        m_navigationDock->setWindowTitle(dockTitle);
        for (QLabel *label : m_navigationDock->findChildren<QLabel *>(
                 QStringLiteral("dockTitle"))) {
            label->setText(dockTitle);
        }
    }
    if (firmware) {
        QString path;
        QString status;
        bool monitoring = false;
        if (QObject *page = workspace->findChild<QObject *>(
                QStringLiteral("tn160FirmwarePage"))) {
            QMetaObject::invokeMethod(page, "firmwarePath", Qt::DirectConnection,
                                      Q_RETURN_ARG(QString, path));
            QMetaObject::invokeMethod(page, "firmwareStatus", Qt::DirectConnection,
                                      Q_RETURN_ARG(QString, status));
            QMetaObject::invokeMethod(page, "monitoring", Qt::DirectConnection,
                                      Q_RETURN_ARG(bool, monitoring));
        }
        updateFirmwareState(path, status, monitoring);
        synchronizeContextPanels();
        return;
    }

    const QString current = m_connectionPort->currentText();
    const QSignalBlocker blocker(m_connectionPort);
    const QSignalBlocker baudBlocker(m_connectionBaud);
    const QSignalBlocker triggerBaudBlocker(m_connectionTriggerBaud);
    const QSignalBlocker readBaudBlocker(m_connectionReadBaud);
    m_connectionPort->clear();
    for (const SerialPortInfo &port : SerialTransport::availablePorts()) {
        m_connectionPort->addItem(port.portName, port.description);
    }
    const int previous = m_connectionPort->findText(current);
    if (previous >= 0) m_connectionPort->setCurrentIndex(previous);

    QObject *target = nullptr;
    bool canConnect = false;
    const bool serial = workspace
        && workspace->objectName() == QStringLiteral("serialScopeWorkspace");
    bool calibration = false;
    bool calibrationDump = false;
    QString title = tr("未打开可连接工作区");
    if (serial) {
        target = workspace;
        canConnect = true;
        title = tr("Serial Scope · 就绪");
    } else if (workspace && workspace->objectName() == QStringLiteral("instrumentWorkspace")) {
        int page = -1;
        QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, page));
        const QString pageObject = page == 2 ? QStringLiteral("tn160CalibrationPage")
            : page == 3 ? QStringLiteral("tn160CalibrationDataPage")
                        : QStringLiteral("tn160LivePage");
        target = workspace->findChild<QObject *>(pageObject);
        calibration = page == 2 || page == 3;
        calibrationDump = page == 3;
        canConnect = target != nullptr && (page == 1 || calibration);
        title = page == 1 ? tr("TN160 · 实时诊断")
            : page == 2 ? tr("TN160 · 设备标定")
            : page == 3 ? tr("TN160 · 标定数据")
                        : tr("TN160 · 当前页面无串口动作");
    }
    m_connectionBaudLabel->setVisible(!calibration);
    m_connectionBaud->setVisible(!calibration);
    m_connectionTriggerBaudLabel->setVisible(calibration);
    m_connectionTriggerBaud->setVisible(calibration);
    m_connectionReadBaudLabel->setVisible(calibration);
    m_connectionReadBaud->setVisible(calibration);
    m_connectionEngineLabel->setVisible(serial);
    m_connectionEngine->setVisible(serial);
    const bool tn160Live = !serial && !calibration
        && workspace && workspace->objectName() == QStringLiteral("instrumentWorkspace")
        && tn160Page == 1;
    for (QWidget *widget : {static_cast<QWidget *>(m_tnRawFrameLabel),
                           static_cast<QWidget *>(m_tnRawFrameCount),
                           static_cast<QWidget *>(m_tnRawCapture)}) {
        if (widget) widget->setVisible(tn160Live);
    }
    if (m_tnRawFrameCount) m_tnRawFrameCount->setEnabled(tn160Live && canConnect);
    if (m_tnRawCapture) m_tnRawCapture->setEnabled(tn160Live && canConnect);
    const bool sessionActive = canConnect && connectionTargetActive(target);
    setConnectionConfigurationEnabled(canConnect, serial, calibration, sessionActive);
    m_connectionToggle->setEnabled(canConnect && !(calibration && sessionActive));
    m_connectionToggle->setText(!canConnect ? tr("连接")
        : calibrationDump ? tr("读取标定数据")
        : calibration ? tr("开始标定")
        : sessionActive ? (serial ? tr("断开") : tr("停止采集"))
                        : (serial ? tr("连接") : tr("开始采集")));
    m_connectionState->setText(title);
    m_connectionState->setProperty("state", canConnect ? QStringLiteral("installed")
                                                          : QStringLiteral("disabled"));
    m_connectionState->style()->unpolish(m_connectionState);
    m_connectionState->style()->polish(m_connectionState);

    if (canConnect && target) {
        QMetaObject::invokeMethod(target, "refreshPorts", Qt::DirectConnection);
        const QString portObject = serial ? QStringLiteral("serialPort")
            : calibration ? (calibrationDump ? QStringLiteral("tn160CalibrationDataPort")
                                             : QStringLiteral("tn160CalibrationPort"))
                          : QStringLiteral("tn160Port");
        QComboBox *sourcePort = workspace->findChild<QComboBox *>(portObject);
        if (sourcePort) {
            const QSignalBlocker sourceBlocker(m_connectionPort);
            const QString selected = sourcePort->currentText();
            m_connectionPort->clear();
            for (int index = 0; index < sourcePort->count(); ++index) {
                m_connectionPort->addItem(sourcePort->itemText(index), sourcePort->itemData(index));
            }
            int selectedIndex = m_connectionPort->findText(selected);
            if (selectedIndex < 0 && !selected.trimmed().isEmpty()) {
                m_connectionPort->addItem(selected.trimmed(), tr("手动端口"));
                selectedIndex = m_connectionPort->count() - 1;
            }
            if (selectedIndex >= 0) m_connectionPort->setCurrentIndex(selectedIndex);
        }
        if (serial) {
            QComboBox *sourceEngine = workspace->findChild<QComboBox *>(
                QStringLiteral("serialEngine"));
            if (sourceEngine) {
                const QSignalBlocker sourceBlocker(m_connectionEngine);
                const int current = m_connectionEngine->findData(sourceEngine->currentData());
                if (current >= 0) m_connectionEngine->setCurrentIndex(current);
            }
            int baud = 0;
            if (QMetaObject::invokeMethod(target, "baudRate", Qt::DirectConnection,
                                          Q_RETURN_ARG(int, baud)) && baud > 0) {
                m_connectionBaud->setValue(baud);
            }
        } else if (!calibration) {
            int baud = 0;
            if (QMetaObject::invokeMethod(target, "baudRate", Qt::DirectConnection,
                                          Q_RETURN_ARG(int, baud)) && baud > 0) {
                m_connectionBaud->setValue(baud);
            }
        } else if (calibration) {
            const QString pageObject = calibrationDump
                ? QStringLiteral("tn160CalibrationDataPage")
                : QStringLiteral("tn160CalibrationPage");
            QObject *page = workspace->findChild<QObject *>(pageObject);
            if (page) {
                if (auto *trigger = page->findChild<QSpinBox *>(
                        QStringLiteral("tn160CalibrationTriggerBaud"))) {
                    m_connectionTriggerBaud->setValue(trigger->value());
                }
                if (auto *read = page->findChild<QSpinBox *>(
                        QStringLiteral("tn160CalibrationReadBaud"))) {
                    m_connectionReadBaud->setValue(read->value());
                }
            }
        }
    }
    synchronizeContextPanels();
}

void MainWindow::refreshComponentLibrary()
{
    if (!m_componentLibraryList) return;
    m_componentLibraryList->clear();

    QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
    QList<QPair<QString, QString>> items;
    if (workspace && workspace->objectName() == QStringLiteral("serialScopeWorkspace")) {
        items = {
            {QStringLiteral("waveform"), tr("实时波形")},
            {QStringLiteral("channels"), tr("数据通道")},
            {QStringLiteral("terminal"), tr("串口终端")},
        };
    } else if (workspace && workspace->objectName() == QStringLiteral("instrumentWorkspace")) {
        int page = -1;
        QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, page));
        if (page == 1) items = {{QStringLiteral("thermal"), tr("TN160 热图")}};
    }

    const QHash<QString, QString> icons{
        {QStringLiteral("waveform"), QStringLiteral(":/serein/waveform.svg")},
        {QStringLiteral("channels"), QStringLiteral(":/serein/channels.svg")},
        {QStringLiteral("terminal"), QStringLiteral(":/serein/terminal.svg")},
        {QStringLiteral("thermal"), QStringLiteral(":/serein/thermal.svg")},
    };
    for (const auto &[id, title] : items) {
        auto *item = new QListWidgetItem(QIcon(icons.value(id)), QString(),
                                         m_componentLibraryList);
        item->setData(Qt::UserRole, id);
        item->setToolTip(tr("%1\n拖动到中央工作区添加").arg(title));
        item->setStatusTip(title);
    }
}

void MainWindow::showLibraryComponent(const QString &componentId)
{
    QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
    if (!workspace || componentId.trimmed().isEmpty()) return;

    const QList<QObject *> canvases = workspace->findChildren<QObject *>(
        QStringLiteral("workspaceCanvas"));
    for (QObject *canvas : canvases) {
        bool added = false;
        if (!QMetaObject::invokeMethod(
                canvas, "addOrShowComponentAt", Qt::DirectConnection,
                Q_RETURN_ARG(bool, added), Q_ARG(QString, componentId),
                Q_ARG(QPoint, QPoint(-1, -1)))) {
            continue;
        }
        if (added) {
            if (m_componentLibraryDock) m_componentLibraryDock->hide();
            if (m_componentsToggle) m_componentsToggle->setChecked(false);
            return;
        }
    }
    log(QStringLiteral("warning"), tr("当前工作区不支持组件：%1").arg(componentId));
}

void MainWindow::detachTn160Trend()
{
    if (!m_attachedTn160Trend) return;
    if (m_outputTrendLayout) m_outputTrendLayout->removeWidget(m_attachedTn160Trend);
    m_attachedTn160Trend->hide();
    if (m_attachedTn160TrendOwner) {
        m_attachedTn160Trend->setParent(m_attachedTn160TrendOwner);
    }
    m_attachedTn160Trend.clear();
    m_attachedTn160TrendOwner.clear();
    if (m_outputTrendEmpty) m_outputTrendEmpty->show();
}

void MainWindow::synchronizeContextPanels()
{
    QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
    bool tn160Live = false;
    QWidget *livePage = nullptr;
    if (workspace && workspace->objectName() == QStringLiteral("instrumentWorkspace")) {
        int page = -1;
        QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, page));
        tn160Live = page == 1;
        livePage = workspace->findChild<QWidget *>(QStringLiteral("tn160LivePage"));
    }

    const bool showMetrics = tn160Live && livePage;
    if (m_liveMetrics) m_liveMetrics->setVisible(showMetrics);
    for (QWidget *generic : {static_cast<QWidget *>(m_inspectorName),
                             static_cast<QWidget *>(m_inspectorDescription),
                             static_cast<QWidget *>(m_inspectorMeta),
                             static_cast<QWidget *>(m_inspectorCapabilities)}) {
        if (generic) generic->setVisible(!showMetrics);
    }

    if (tn160Live && livePage && m_outputTrendHost && m_outputTrendLayout) {
        QWidget *trend = livePage->findChild<QWidget *>(QStringLiteral("tn160TrendWidget"));
        if (trend && m_attachedTn160Trend != trend) {
            detachTn160Trend();
            m_attachedTn160TrendOwner = livePage;
            trend->setParent(m_outputTrendHost);
            m_outputTrendLayout->addWidget(trend, 1);
            m_attachedTn160Trend = trend;
            trend->setProperty("darkMode", property("darkMode").toBool());
            trend->show();
            if (m_outputTrendEmpty) m_outputTrendEmpty->hide();
        }
        if (m_outputStack) m_outputStack->setCurrentWidget(m_outputTrendHost);
    } else {
        detachTn160Trend();
        if (m_outputStack) m_outputStack->setCurrentWidget(m_output);
    }
    refreshComponentLibrary();
}

void MainWindow::updateTn160Telemetry(const QString &center, const QString &ntc,
                                      const QString &centerAdu, const QString &vtemp,
                                      const QString &fps)
{
    if (m_liveCenter) m_liveCenter->setText(center);
    if (m_liveNtc) m_liveNtc->setText(ntc);
    if (m_liveCenterAdu) m_liveCenterAdu->setText(centerAdu);
    if (m_liveVtemp) m_liveVtemp->setText(vtemp);
    if (m_liveFps) m_liveFps->setText(fps);
}

void MainWindow::toggleConnection()
{
    QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
    if (!workspace) return;
    QObject *target = nullptr;
    const char *method = nullptr;
    int tn160Page = -1;
    if (workspace->objectName() == QStringLiteral("serialScopeWorkspace")) {
        target = workspace;
        method = "toggleConnection";
    } else if (workspace->objectName() == QStringLiteral("instrumentWorkspace")) {
        QMetaObject::invokeMethod(workspace, "currentPageIndex", Qt::DirectConnection,
                                  Q_RETURN_ARG(int, tn160Page));
        if (tn160Page == 1) {
            target = workspace->findChild<QObject *>(QStringLiteral("tn160LivePage"));
            method = "toggleCapture";
        } else if (tn160Page == 2) {
            target = workspace->findChild<QObject *>(QStringLiteral("tn160CalibrationPage"));
            method = "startTransfer";
        } else if (tn160Page == 3) {
            target = workspace->findChild<QObject *>(QStringLiteral("tn160CalibrationDataPage"));
            method = "startTransfer";
        } else {
            return;
        }
    }
    if (!target || !method) return;

    // The visible host controls are proxies for hidden plugin-owned controls.
    // Synchronize immediately before opening so an editable, manually entered
    // port (for example COM28) cannot be lost to signal timing or a refresh.
    const QString port = m_connectionPort ? m_connectionPort->currentText().trimmed()
                                          : QString();
    if (!port.isEmpty()) {
        QMetaObject::invokeMethod(target, "setPort", Qt::DirectConnection,
                                  Q_ARG(QString, port));
    }
    if (workspace->objectName() == QStringLiteral("serialScopeWorkspace")) {
        if (m_connectionBaud) {
            QMetaObject::invokeMethod(target, "setBaud", Qt::DirectConnection,
                                      Q_ARG(int, m_connectionBaud->value()));
        }
        if (m_connectionEngine) {
            const QString engine = m_connectionEngine->currentData().toString();
            if (!engine.isEmpty()) {
                QMetaObject::invokeMethod(target, "setEngine", Qt::DirectConnection,
                                          Q_ARG(QString, engine));
            }
        }
    } else if (tn160Page == 1 && m_connectionBaud) {
        QMetaObject::invokeMethod(target, "setBaud", Qt::DirectConnection,
                                  Q_ARG(int, m_connectionBaud->value()));
    } else if ((tn160Page == 2 || tn160Page == 3)) {
        if (m_connectionTriggerBaud) {
            QMetaObject::invokeMethod(target, "setTriggerBaud", Qt::DirectConnection,
                                      Q_ARG(int, m_connectionTriggerBaud->value()));
        }
        if (m_connectionReadBaud) {
            QMetaObject::invokeMethod(target, "setReadBaud", Qt::DirectConnection,
                                      Q_ARG(int, m_connectionReadBaud->value()));
        }
    }
    QMetaObject::invokeMethod(target, method, Qt::QueuedConnection);
}

void MainWindow::updateCalibrationState(const QString &state, const QString &text)
{
    if (!m_connectionState || !m_connectionToggle) return;
    if (QObject *origin = sender(); origin && origin != connectionTargetForCurrentWorkspace()) {
        return;
    }
    m_connectionState->setText(text);
    m_connectionState->setProperty("state", state);
    m_connectionState->style()->unpolish(m_connectionState);
    m_connectionState->style()->polish(m_connectionState);
    const bool running = state == QStringLiteral("running");
    int page = -1;
    if (m_stack && m_stack->currentWidget()
        && m_stack->currentWidget()->objectName() == QStringLiteral("instrumentWorkspace")) {
        QMetaObject::invokeMethod(m_stack->currentWidget(), "currentPageIndex",
                                  Qt::DirectConnection, Q_RETURN_ARG(int, page));
    }
    const bool dump = page == 3;
    setConnectionConfigurationEnabled(true, false, true, running);
    m_connectionToggle->setText(dump ? tr("读取标定数据") : tr("开始标定"));
    m_connectionToggle->setEnabled(!running);
}

void MainWindow::updateFirmwareState(const QString &path, const QString &status,
                                     const bool monitoring)
{
    if (!m_firmwarePath || !m_firmwareState || !m_firmwareMonitor) return;
    m_firmwarePath->setText(path.isEmpty() ? tr("尚未选择 UF2")
                                           : QDir::toNativeSeparators(path));
    m_firmwarePath->setToolTip(path);
    m_firmwareState->setText(status.isEmpty() ? tr("等待选择 UF2") : status);
    m_firmwareState->setProperty("state", monitoring ? QStringLiteral("connecting")
                                                      : path.isEmpty()
                                                          ? QStringLiteral("disabled")
                                                          : QStringLiteral("installed"));
    m_firmwareState->style()->unpolish(m_firmwareState);
    m_firmwareState->style()->polish(m_firmwareState);
    m_firmwareChoose->setEnabled(!monitoring);
    m_firmwareMonitor->setText(monitoring ? tr("停止监听") : tr("开始监听"));
    m_firmwareMonitor->setEnabled(!path.isEmpty() || monitoring);
    m_firmwareMonitor->setProperty("kind", monitoring ? QStringLiteral("warning")
                                                       : QStringLiteral("primary"));
    m_firmwareMonitor->style()->unpolish(m_firmwareMonitor);
    m_firmwareMonitor->style()->polish(m_firmwareMonitor);
}

void MainWindow::updateRawCaptureState(const QString &state, const QString &text)
{
    if (!m_tnRawCapture) return;
    if (QObject *origin = sender(); origin && origin != connectionTargetForCurrentWorkspace()) {
        return;
    }
    const bool capturing = state == QStringLiteral("raw-capturing");
    const bool restoring = state == QStringLiteral("raw-restoring");
    if (capturing) {
        m_tnRawCapture->setText(tr("停止原始采集"));
        m_tnRawCapture->setProperty("kind", QStringLiteral("warning"));
    } else if (restoring) {
        m_tnRawCapture->setText(tr("恢复监控中"));
        m_tnRawCapture->setProperty("kind", QStringLiteral("quiet"));
    } else {
        m_tnRawCapture->setText(tr("采集原始帧"));
        m_tnRawCapture->setProperty("kind", QStringLiteral("quiet"));
    }
    m_tnRawCapture->setEnabled(!restoring);
    m_tnRawCapture->style()->unpolish(m_tnRawCapture);
    m_tnRawCapture->style()->polish(m_tnRawCapture);
    if (m_connectionState && (capturing || restoring)) {
        m_connectionState->setText(text);
    }
}

void MainWindow::updateConnectionState(const QString &state, const QString &text)
{
    if (!m_connectionState || !m_connectionToggle) return;
    if (QObject *origin = sender(); origin && origin != connectionTargetForCurrentWorkspace()) {
        return;
    }
    m_connectionState->setText(text);
    m_connectionState->setProperty("state", state);
    m_connectionState->style()->unpolish(m_connectionState);
    m_connectionState->style()->polish(m_connectionState);
    const bool serial = m_stack && m_stack->currentWidget()
        && m_stack->currentWidget()->objectName() == QStringLiteral("serialScopeWorkspace");
    const bool active = state == QStringLiteral("connected")
        || state == QStringLiteral("raw-capturing")
        || state == QStringLiteral("raw-restoring");
    const bool changing = state == QStringLiteral("connecting")
        || state == QStringLiteral("disconnecting")
        || state == QStringLiteral("raw-restoring");
    const bool locked = state != QStringLiteral("disconnected")
        && (state != QStringLiteral("error")
            || connectionTargetActive(connectionTargetForCurrentWorkspace()));
    setConnectionConfigurationEnabled(true, serial, false, locked);
    m_connectionToggle->setText(active ? (serial ? tr("断开") : tr("停止采集"))
                                        : serial ? tr("连接") : tr("开始采集"));
    m_connectionToggle->setEnabled(!changing);
}

void MainWindow::toggleDarkMode()
{
    const bool enabled = m_sleepButton && m_sleepButton->isChecked();
    applyDarkMode(enabled);
    setStatus(enabled ? tr("Sleep · 深色材质") : tr("Sleep · 浅色材质"));
}

void MainWindow::applyDarkMode(const bool enabled)
{
    QWidget *shell = findChild<QWidget *>(QStringLiteral("rootShell"));
    if (!shell) return;
    setProperty("darkMode", enabled);
    shell->setProperty("darkMode", enabled);
    for (QWidget *widget : findChildren<QWidget *>()) {
        widget->setProperty("darkMode", enabled);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
    shell->style()->unpolish(shell);
    shell->style()->polish(shell);
    style()->unpolish(this);
    style()->polish(this);
    update();
    if (m_sleepButton && m_sleepButton->isChecked() != enabled) {
        const QSignalBlocker blocker(m_sleepButton);
        m_sleepButton->setChecked(enabled);
    }
}

void MainWindow::applyDefaultDockLayout()
{
    if (!m_workbenchWindow || !m_centerWorkbenchWindow) return;
    m_navigationDock->setFloating(false);
    m_componentLibraryDock->setFloating(false);
    m_inspectorDock->setFloating(false);
    m_outputDock->setFloating(false);
    m_workbenchWindow->addDockWidget(Qt::RightDockWidgetArea, m_navigationDock);
    m_workbenchWindow->addDockWidget(Qt::LeftDockWidgetArea, m_componentLibraryDock);
    m_workbenchWindow->addDockWidget(Qt::LeftDockWidgetArea, m_inspectorDock);
    m_centerWorkbenchWindow->addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);
    m_navigationDock->show();
    m_componentLibraryDock->hide();
    m_inspectorDock->hide();
    m_outputDock->hide();
    m_workbenchWindow->resizeDocks({m_navigationDock}, {220}, Qt::Horizontal);
    m_workbenchWindow->resizeDocks({m_inspectorDock}, {220}, Qt::Horizontal);
    m_centerWorkbenchWindow->resizeDocks({m_outputDock}, {156}, Qt::Vertical);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = property("darkMode").toBool();
    // The window surround is the Sleep surface.  Do not introduce a second
    // vertical gradient behind the top bar and docks; component material is
    // reserved for the rounded surfaces inside this continuous shell.
    const QColor surface = dark ? QColor(QStringLiteral("#20252a"))
                                : QColor(QStringLiteral("#e3e6e8"));

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(frame, WindowCornerRadius, WindowCornerRadius);
    painter.fillPath(path, surface);
    painter.setPen(QPen(dark ? QColor(255, 255, 255, 28)
                            : QColor(111, 123, 133, 52),
                    1.0));
    painter.drawPath(path);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    const auto reflowCards = [this](QGridLayout *grid, const bool loadedOnly,
                                     const bool managementActions, int *lastColumns) {
        if (!grid || !grid->parentWidget() || !lastColumns) return;
        const int columns = grid->parentWidget()->width() >= 680 ? 2 : 1;
        if (*lastColumns != columns) {
            rebuildCards(grid, loadedOnly, managementActions);
        }
    };
    reflowCards(m_homeGrid, true, false, &m_homeCardColumns);
    reflowCards(m_componentGrid, false, true, &m_componentCardColumns);

#ifdef Q_OS_WIN
    if (isMaximized()) {
        clearMask();
        return;
    }
    // Windows DWM supplies an anti-aliased system corner.  QRegion's
    // integer ellipse is only a fallback: it produces the stair-stepped
    // silhouette visible on the outer shell at normal DPI.
    constexpr DWORD DwmCornerPreferenceAttribute = 33;
    constexpr DWORD DwmCornerRound = 2;
    const HWND windowHandle = reinterpret_cast<HWND>(winId());
    const DWORD preference = DwmCornerRound;
    if (windowHandle
        && SUCCEEDED(DwmSetWindowAttribute(windowHandle,
                                           DwmCornerPreferenceAttribute,
                                           &preference, sizeof(preference)))) {
        clearMask();
        return;
    }
    const int radius = qRound(WindowCornerRadius);
    const int diameter = radius * 2;
    QRegion region(rect());
    const auto carveCorner = [&region, radius, diameter](const QRect &square,
                                                          const QRect &ellipse) {
        region -= QRegion(square);
        region += QRegion(ellipse, QRegion::Ellipse);
    };
    carveCorner(QRect(0, 0, radius, radius), QRect(0, 0, diameter, diameter));
    carveCorner(QRect(width() - radius, 0, radius, radius),
                QRect(width() - diameter, 0, diameter, diameter));
    carveCorner(QRect(0, height() - radius, radius, radius),
                QRect(0, height() - diameter, diameter, diameter));
    carveCorner(QRect(width() - radius, height() - radius, radius, radius),
                QRect(width() - diameter, height() - diameter, diameter, diameter));
    setMask(region);
#endif
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || (widget != this && !isAncestorOf(widget))) {
        return QMainWindow::eventFilter(watched, event);
    }
    if (isMaximized() || !windowHandle()) {
        return QMainWindow::eventFilter(watched, event);
    }

    const auto edgeAt = [this](const QPoint &global) {
        const QRect bounds = frameGeometry();
        constexpr int grip = 7;
        Qt::Edges edges;
        if (global.x() <= bounds.left() + grip) edges |= Qt::LeftEdge;
        if (global.x() >= bounds.right() - grip) edges |= Qt::RightEdge;
        if (global.y() <= bounds.top() + grip) edges |= Qt::TopEdge;
        if (global.y() >= bounds.bottom() - grip) edges |= Qt::BottomEdge;
        return edges;
    };

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            const Qt::Edges edges = edgeAt(mouse->globalPosition().toPoint());
            if (edges != Qt::Edges()) {
                if (windowHandle()->startSystemResize(edges)) return true;
            }
        }
    } else if (event->type() == QEvent::MouseMove) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const Qt::Edges edges = edgeAt(mouse->globalPosition().toPoint());
        if (edges == (Qt::LeftEdge | Qt::TopEdge)
            || edges == (Qt::RightEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (edges == (Qt::RightEdge | Qt::TopEdge)
                   || edges == (Qt::LeftEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        } else if (cursor().shape() >= Qt::SizeVerCursor
                   && cursor().shape() <= Qt::SizeBDiagCursor) {
            unsetCursor();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

} // namespace serein::scope::app

#include "MainWindow.moc"
