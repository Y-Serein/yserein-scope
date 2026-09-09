#include "MainWindow.h"
#include "ComponentManager.h"
#include "PluginCard.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QMimeData>
#include <QtCore/QMetaObject>
#include <QtCore/QSettings>
#include <QtCore/QSet>
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
#include <QtGui/QResizeEvent>
#include <QtGui/QWindow>
#include <serein/scope/BackendRuntime.h>
#include <serein/scope/WorkspaceCanvas.h>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QDockWidget>
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
#include <cmath>

namespace serein::scope::app {
namespace {

const QString HomeId = QStringLiteral("__home__");
const QString ComponentsId = QStringLiteral("__components__");
const QString LastActivePageSettingsKey = QStringLiteral("main/last-active-page-v1");
const QString LastComponentImportDirectoryKey =
    QStringLiteral("components/last-import-directory-v1");
const QString DockSettingsKey = QStringLiteral("main/docks-v15-output-clear-connection-right");
const QString CentralDockSettingsKey = QStringLiteral("main/central-dock-v1");
const QString PageLayoutSettingsGroup = QStringLiteral("main/page-layout-v1");
const QString EqualSidePanelWidthMigrationKey =
    QStringLiteral("main/page-layout-equal-side-panel-width-v1");
const QString MinimumSidePanelWidthMigrationKey =
    QStringLiteral("main/page-layout-minimum-side-panel-width-v2");
const QString Tn160LiveOutputFractionMigrationKey =
    QStringLiteral("main/page-layout-tn160-live-output-fraction-v3");
const QString Tn160NcmEqualPanelsMigrationKey =
    QStringLiteral("main/page-layout-tn160-ncm-equal-panels-v1");
const QString Tn160NcmOutputFractionMigrationKey =
    QStringLiteral("main/page-layout-tn160-ncm-output-fraction-v2");
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
    header->setFixedHeight(32);
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

struct DockPanelShell {
    QWidget *surface = nullptr;
    QWidget *header = nullptr;
    QWidget *body = nullptr;
    QVBoxLayout *bodyLayout = nullptr;
};

class RoundedBottomPanelBody final : public QWidget {
public:
    RoundedBottomPanelBody(QWidget *header, QWidget *parent = nullptr)
        : QWidget(parent), m_header(header)
    {
        if (m_header) m_header->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_header
            && (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
            m_clipTopCorners = event->type() == QEvent::Hide;
            updateMask();
        }
        return QWidget::eventFilter(watched, event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateMask();
    }

private:
    void updateMask()
    {
        if (width() <= 0 || height() <= 0) return;

        constexpr qreal radius = 7.0;
        const qreal effectiveRadius = std::min<qreal>(
            radius, std::min<qreal>(width(), height()) / 2.0);
        QPainterPath path;
        path.moveTo(m_clipTopCorners ? effectiveRadius : 0.0, 0.0);
        path.lineTo(m_clipTopCorners ? width() - effectiveRadius : width(), 0.0);
        if (m_clipTopCorners) {
            path.quadTo(width(), 0.0, width(), effectiveRadius);
        }
        path.lineTo(width(), height() - effectiveRadius);
        path.quadTo(width(), height(), width() - effectiveRadius, height());
        path.lineTo(effectiveRadius, height());
        path.quadTo(0.0, height(), 0.0, height() - effectiveRadius);
        if (m_clipTopCorners) {
            path.lineTo(0.0, effectiveRadius);
            path.quadTo(0.0, 0.0, effectiveRadius, 0.0);
        }
        path.closeSubpath();
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    QPointer<QWidget> m_header;
    bool m_clipTopCorners = false;
};

DockPanelShell makeDockPanelShell(QDockWidget *dock, const QString &title,
                                  const char *slot)
{
    DockPanelShell shell;
    shell.surface = new QWidget;
    shell.surface->setObjectName(QStringLiteral("dockPanelContent"));
    shell.surface->setProperty("workspacePanelSlot", slot);
    auto *outer = new QVBoxLayout(shell.surface);
    // Keep child paint one pixel inside the host-owned surface border.  QSS
    // radii do not clip descendants, so a zero-inset body can overwrite the
    // very edge that is supposed to define the shared 8 px component shape.
    outer->setContentsMargins(1, 1, 1, 1);
    outer->setSpacing(0);
    shell.header = makeDockHeader(dock, title);
    outer->addWidget(shell.header);
    shell.body = new RoundedBottomPanelBody(shell.header, shell.surface);
    shell.body->setObjectName(QStringLiteral("dockPanelBody"));
    shell.bodyLayout = new QVBoxLayout(shell.body);
    shell.bodyLayout->setContentsMargins(0, 0, 0, 0);
    shell.bodyLayout->setSpacing(0);
    outer->addWidget(shell.body, 1);
    return shell;
}

void installFloatingDockChrome(QDockWidget *dock, QWidget *innerHeader,
                               QWidget *collapsedTitleBar, const QString &title)
{
    if (!dock || !innerHeader || !collapsedTitleBar) return;
    auto *floatingHeader = new QWidget(dock);
    floatingHeader->setObjectName(QStringLiteral("dockFloatingTitleBar"));
    floatingHeader->setFixedHeight(32);
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
    : QMainWindow(parent), m_componentRegistry(this)
{
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowTitle(QStringLiteral("Serein Scope"));
    setMinimumSize(1100, 720);
    resize(1460, 900);
    setMouseTracking(true);
    qApp->installEventFilter(this);
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AnimatedDocks);
    connect(&BackendRuntime::instance(), &BackendRuntime::runtimeError,
            this, [this](const QString &message) {
        log(QStringLiteral("error"), tr("后台运行错误：%1").arg(message));
        setStatus(tr("后台运行错误 · %1").arg(message));
    });

    prepareComponents();
    buildCentralShell();
    buildGlobalBar();
    buildDocks();
    buildMenu();
    connect(m_components, &ComponentManager::componentsChanged,
            this, &MainWindow::refreshComponentUi,
            Qt::QueuedConnection);

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
        const QString savedPage = QSettings().value(
            LastActivePageSettingsKey, HomeId).toString();
        if (savedPage == HomeId || savedPage == ComponentsId
            || m_components->isLoaded(savedPage)) {
            initialPage = savedPage;
        }
    }
    activatePage(initialPage);
    if (m_activePage.isEmpty()) activatePage(HomeId);

    QSettings settings;
    if (!settings.value(EqualSidePanelWidthMigrationKey, false).toBool()) {
        const QStringList contexts{
            QStringLiteral("serial"),
            QStringLiteral("tn160/firmware"),
            QStringLiteral("tn160/live"),
            QStringLiteral("tn160/engineering-calibration"),
            QStringLiteral("tn160/calibration-data"),
            QStringLiteral("tn160/production-calibration"),
        };
        for (const QString &context : contexts) {
            settings.remove(PageLayoutSettingsGroup + QLatin1Char('/') + context
                            + QStringLiteral("/inspectorWidth"));
        }
        settings.setValue(EqualSidePanelWidthMigrationKey, true);
    }
    if (!settings.value(MinimumSidePanelWidthMigrationKey, false).toBool()) {
        const QStringList contexts{
            QStringLiteral("serial"),
            QStringLiteral("tn160/firmware"),
            QStringLiteral("tn160/live"),
            QStringLiteral("tn160/engineering-calibration"),
            QStringLiteral("tn160/calibration-data"),
            QStringLiteral("tn160/production-calibration"),
        };
        for (const QString &context : contexts) {
            const QString key = PageLayoutSettingsGroup + QLatin1Char('/') + context
                + QStringLiteral("/inspectorWidth");
            if (settings.value(key, -1).toInt() == SidePanelMinimumWidth) {
                settings.remove(key);
            }
        }
        settings.setValue(MinimumSidePanelWidthMigrationKey, true);
    }
    if (!settings.value(Tn160LiveOutputFractionMigrationKey, false).toBool()) {
        const QString outputFractionKey = PageLayoutSettingsGroup
            + QStringLiteral("/tn160/live/outputFraction");
        if (settings.contains(outputFractionKey)
            && std::abs(settings.value(outputFractionKey).toDouble() - 0.42) < 0.015) {
            settings.remove(outputFractionKey);
        }
        settings.setValue(Tn160LiveOutputFractionMigrationKey, true);
    }
    if (!settings.value(Tn160NcmEqualPanelsMigrationKey, false).toBool()) {
        const QString inspectorWidthKey = PageLayoutSettingsGroup
            + QStringLiteral("/tn160/firmware-calibration/inspectorWidth");
        settings.remove(inspectorWidthKey);
        settings.setValue(Tn160NcmEqualPanelsMigrationKey, true);
    }
    if (!settings.value(Tn160NcmOutputFractionMigrationKey, false).toBool()) {
        const QString outputFractionKey = PageLayoutSettingsGroup
            + QStringLiteral("/tn160/firmware-calibration/outputFraction");
        if (settings.contains(outputFractionKey)
            && std::abs(settings.value(outputFractionKey).toDouble() - 0.60) < 0.015) {
            settings.remove(outputFractionKey);
        }
        settings.setValue(Tn160NcmOutputFractionMigrationKey, true);
    }
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
    m_pageLayoutsReady = true;
    switchPageLayoutContext();
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
            m_workbenchWindow->resizeDocks(
                {m_navigationDock, m_inspectorDock},
                {SidePanelDefaultWidth, SidePanelDefaultWidth}, Qt::Horizontal);
            resizeOutputDockForCurrentPage();
        });
    }
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    if (m_pageLayoutsReady) savePageLayout(m_pageLayoutContext);
    detachWorkspacePanels();
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

    // QMainWindow hides child docks from its base-class destructor, after
    // MainWindow's QString/state members have already been destroyed.  Do
    // not let those late visibilityChanged signals re-enter layout saving.
    for (QDockWidget *dock : {m_navigationDock, m_componentLibraryDock,
                              m_outputDock, m_inspectorDock}) {
        if (dock) disconnect(dock, nullptr, this, nullptr);
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

    const auto rejectWorkspace = [this, workspace, componentId, error](
                                      const QString &reason) {
        if (error) *error = reason;
        if (auto *managed = qobject_cast<IDeviceWorkspace *>(workspace)) {
            managed->shutdown();
        }
        delete workspace;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        m_components->unload(componentId, nullptr, false);
        return false;
    };
    QString contractError;
    if (!validateWorkspaceContract(workspace, &contractError)) {
        return rejectWorkspace(contractError);
    }
    if (auto *managed = qobject_cast<IDeviceWorkspace *>(workspace)) managed->shutdown();
    delete workspace;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return m_components->unload(componentId, error, false);
}

bool MainWindow::validateWorkspaceContract(QWidget *workspace, QString *error) const
{
    const auto reject = [error](const QString &reason) {
        if (error) *error = reason;
        return false;
    };
    if (!workspace) return reject(tr("插件工作区为空"));
    if (qobject_cast<QDockWidget *>(workspace)
        || qobject_cast<QMainWindow *>(workspace)
        || workspace->findChild<QDockWidget *>()
        || workspace->findChild<QMainWindow *>()) {
        return reject(
            tr("组件工作区创建了自己的 Dock/主窗口；外围面板必须由宿主模板持有"));
    }

    auto *contributor = qobject_cast<IWorkspaceUiProvider *>(workspace);
    if (!contributor) return true;
    WorkspaceUiProvider *provider = contributor->workspaceUiProvider();
    if (!provider || provider->context().id.trimmed().isEmpty()) {
        return reject(tr("组件 UI Provider 缺少稳定的上下文 ID"));
    }

    const WorkspaceUiContext &context = provider->context();
    const QList<WorkspacePanelPresentation> presentations{
        context.contextPanel,
        context.inspectorPanel,
        context.outputPanel,
    };
    QSet<QWidget *> uniquePanels;
    for (const WorkspacePanelPresentation &panel : presentations) {
        if (!panel.content) continue;
        if (panel.content != workspace && !workspace->isAncestorOf(panel.content)) {
            return reject(
                tr("组件面板内容不属于工作区生命周期：%1")
                    .arg(panel.content->objectName()));
        }
        if (uniquePanels.contains(panel.content)) {
            return reject(tr("多个宿主面板槽复用了同一个内容控件"));
        }
        QWidget *panelOwner = panel.content->parentWidget();
        if (panelOwner && panelOwner->layout()
            && panelOwner->layout()->indexOf(panel.content) >= 0) {
            return reject(
                tr("宿主面板贡献根控件必须是组件持有的独立内容，不能同时占用组件内部布局"));
        }
        uniquePanels.insert(panel.content);
    }

    const WorkspaceUiPresentationOptions presentationOptions =
        provider->presentationOptions();
    const QList<WorkspacePanelOptions> panelOptions{
        presentationOptions.contextPanel,
        presentationOptions.inspectorPanel,
        presentationOptions.outputPanel,
    };
    for (qsizetype index = 0; index < presentations.size(); ++index) {
        const WorkspacePanelPresentation &panel = presentations.at(index);
        QWidget *headerControl = panelOptions.at(index).headerControl;
        if (!headerControl) continue;
        if (!panel.content || headerControl == panel.content
            || !panel.content->isAncestorOf(headerControl)) {
            return reject(tr("面板 Header 控件不属于对应内容的生命周期"));
        }
        QWidget *headerOwner = headerControl->parentWidget();
        auto *ownerLayout = headerOwner
            ? qobject_cast<QBoxLayout *>(headerOwner->layout()) : nullptr;
        if (!ownerLayout || ownerLayout->indexOf(headerControl) < 0) {
            return reject(tr("面板 Header 控件必须位于组件自有的顺序布局中"));
        }
    }
    return true;
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
    const QString packagedPath = ComponentManager::packagedStorePath();
    QDir packagedComponents(packagedPath);
    if (!packagedPath.isEmpty()) {
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
    const DockPanelShell shell = makeDockPanelShell(
        m_navigationDock, tr("设备连接"), "context");
    QWidget *panel = shell.surface;
    panel->setMinimumWidth(SidePanelMinimumWidth);
    panel->setMaximumWidth(SidePanelMaximumWidth);

    m_contextContributionHost = new QWidget(shell.body);
    m_contextContributionHost->setObjectName(QStringLiteral("workspaceContextHost"));
    m_contextContributionLayout = new QVBoxLayout(m_contextContributionHost);
    m_contextContributionLayout->setContentsMargins(0, 0, 0, 0);
    m_contextContributionLayout->setSpacing(0);
    m_contextContributionHost->hide();
    shell.bodyLayout->addWidget(m_contextContributionHost, 1);

    m_contextFallback = new QLabel(
        tr("打开设备工作区后，此处显示对应的连接与操作。"),
        shell.body);
    m_contextFallback->setObjectName(QStringLiteral("workspaceContextFallback"));
    m_contextFallback->setWordWrap(true);
    m_contextFallback->setAlignment(Qt::AlignCenter);
    shell.bodyLayout->addWidget(m_contextFallback, 1);

    m_navigation = new QListWidget(shell.body);
    m_navigation->setObjectName(QStringLiteral("pluginNav"));
    m_navigation->hide();
    connect(m_navigation, &QListWidget::currentItemChanged,
            this, &MainWindow::navigationChanged);
    shell.bodyLayout->addWidget(m_navigation);

    auto *version = new QLabel(QStringLiteral("SEREIN SCOPE  1.0.1   ·   ABI 2"),
                                shell.body);
    version->setObjectName(QStringLiteral("panelVersion"));
    shell.bodyLayout->addWidget(version);
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
    manage->setObjectName(QStringLiteral("componentManagementButton"));
    manage->setProperty("kind", QStringLiteral("primary"));
    connect(manage, &QPushButton::clicked,
            this, &MainWindow::toggleComponentManagement);
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
    auto *exitManagement = new QPushButton(tr("退出管理"));
    exitManagement->setObjectName(QStringLiteral("exitComponentManagementButton"));
    connect(exitManagement, &QPushButton::clicked,
            this, &MainWindow::toggleComponentManagement);
    toolbarLayout->addWidget(exitManagement);
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
    m_navigationDockHeader = m_navigationDock->findChild<QWidget *>(
        QStringLiteral("dockHeader"));
    installFloatingDockChrome(m_navigationDock,
                              m_navigationDockHeader,
                              connectionTitleBar, tr("设备连接"));
    connect(m_navigationDock, &QDockWidget::topLevelChanged, this,
            [this](const bool) {
                QTimer::singleShot(0, this,
                                   &MainWindow::synchronizePanelHeaderControls);
            });
    // Device connection is a context/action surface. Keep it on the right
    // beside the data inspector so the left side remains the quiet canvas
    // edge and the two system panels have a single visual reading order.
    m_workbenchWindow->addDockWidget(Qt::RightDockWidgetArea, m_navigationDock);
    m_navigationDock->setMinimumWidth(SidePanelMinimumWidth);
    m_navigationDock->setMaximumWidth(SidePanelMaximumWidth);

    m_componentLibraryDock = new QDockWidget(tr("组件库"), m_workbenchWindow);
    m_componentLibraryDock->setObjectName(QStringLiteral("componentLibraryDock"));
    m_componentLibraryDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_componentLibraryDock->setFeatures(features);
    auto *libraryTitleBar = new QWidget(m_componentLibraryDock);
    libraryTitleBar->setFixedHeight(0);
    m_componentLibraryDock->setTitleBarWidget(libraryTitleBar);
    const DockPanelShell libraryShell = makeDockPanelShell(
        m_componentLibraryDock, tr("组件库"), "library");
    QWidget *libraryPanel = libraryShell.surface;
    QWidget *libraryBody = libraryShell.body;
    QVBoxLayout *libraryBodyLayout = libraryShell.bodyLayout;
    libraryBodyLayout->setContentsMargins(12, 10, 12, 12);
    libraryBodyLayout->setSpacing(8);
    auto *libraryHint = new QLabel(tr("拖动到中央工作区以添加组件"), libraryBody);
    libraryHint->setObjectName(QStringLiteral("pageSubtitle"));
    libraryHint->setWordWrap(true);
    libraryBodyLayout->addWidget(libraryHint);
    auto *libraryList = new ComponentLibraryList(libraryBody);
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
    libraryBodyLayout->addWidget(libraryList, 1);
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
    const DockPanelShell outputShell = makeDockPanelShell(
        m_outputDock, tr("输出终端"), "output");
    QWidget *outputPanel = outputShell.surface;
    auto *outputDockHeader = outputShell.header;
    m_outputDockHeader = outputDockHeader;
    m_outputClear = new QPushButton(tr("清空"));
    m_outputClear->setObjectName(QStringLiteral("outputClearAction"));
    m_outputClear->setProperty("kind", QStringLiteral("quiet"));
    m_outputClear->setToolTip(tr("清空终端输出"));
    m_outputClear->setCursor(Qt::PointingHandCursor);
    if (auto *headerLayout = qobject_cast<QHBoxLayout *>(outputDockHeader->layout())) {
        // Keep the action in the same chrome row as the terminal title and
        // dock controls. The former standalone hint row carried no action.
        headerLayout->insertWidget(1, m_outputClear);
    }
    QVBoxLayout *outputBodyLayout = outputShell.bodyLayout;
    m_output = new QPlainTextEdit;
    m_output->setObjectName(QStringLiteral("outputLog"));
    m_output->setStyleSheet(QStringLiteral("font-size: 16px;"));
    m_output->setReadOnly(true);
    m_output->document()->setMaximumBlockCount(2000);
    connect(m_outputClear, &QPushButton::clicked, this, [this] {
        if (m_outputStack && m_outputTrendHost
            && m_outputStack->currentWidget() == m_outputTrendHost
            && m_activeUiProvider) {
            const WorkspacePanelPresentation panel =
                m_activeUiProvider->context().outputPanel;
            if (panel.clearTarget && !panel.clearMethod.isEmpty()) {
                QMetaObject::invokeMethod(panel.clearTarget,
                                          panel.clearMethod.constData(),
                                          Qt::DirectConnection);
            }
            return;
        }
        if (m_output) m_output->clear();
    });
    m_outputStack = new QStackedWidget(outputPanel);
    m_outputStack->setObjectName(QStringLiteral("outputContentStack"));
    m_outputStack->addWidget(m_output);
    m_outputTrendHost = new QWidget(m_outputStack);
    m_outputTrendHost->setObjectName(QStringLiteral("workspaceOutputHost"));
    m_outputTrendLayout = new QVBoxLayout(m_outputTrendHost);
    m_outputTrendLayout->setContentsMargins(0, 0, 0, 0);
    m_outputTrendLayout->setSpacing(0);
    m_outputTrendEmpty = new QLabel(tr("当前工作区没有提供输出面板内容。"),
                                    m_outputTrendHost);
    m_outputTrendEmpty->setObjectName(QStringLiteral("emptyState"));
    m_outputTrendEmpty->setAlignment(Qt::AlignCenter);
    m_outputTrendLayout->addWidget(m_outputTrendEmpty);
    m_outputStack->addWidget(m_outputTrendHost);
    m_outputStack->setCurrentWidget(m_output);
    outputBodyLayout->addWidget(m_outputStack, 1);
    m_outputDock->setWidget(outputPanel);
    installFloatingDockChrome(m_outputDock,
                              outputPanel->findChild<QWidget *>(QStringLiteral("dockHeader")),
                              outputTitleBar, tr("输出终端"));
    connect(m_outputDock, &QDockWidget::topLevelChanged, this,
            [this](const bool) {
                QTimer::singleShot(0, this,
                                   &MainWindow::synchronizePanelHeaderControls);
            });
    m_centerWorkbenchWindow->addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);

    m_inspectorDock = new QDockWidget(tr("数据与属性"), m_workbenchWindow);
    m_inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    m_inspectorDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_inspectorDock->setFeatures(features);
    auto *inspectorTitleBar = new QWidget(m_inspectorDock);
    inspectorTitleBar->setFixedHeight(0);
    m_inspectorDock->setTitleBarWidget(inspectorTitleBar);
    const DockPanelShell inspectorShell = makeDockPanelShell(
        m_inspectorDock, tr("数据与属性"), "inspector");
    QWidget *inspector = inspectorShell.surface;
    inspector->setMinimumWidth(SidePanelMinimumWidth);
    inspector->setMaximumWidth(SidePanelMaximumWidth);
    auto *inspectorScroll = new QScrollArea(inspector);
    inspectorScroll->setObjectName(QStringLiteral("workspaceInspectorScroll"));
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inspectorContent = new QWidget(inspectorScroll);
    inspectorContent->setObjectName(QStringLiteral("inspectorContent"));
    auto *inspectorLayout = new QVBoxLayout(inspectorContent);
    m_inspectorBodyLayout = inspectorLayout;
    inspectorLayout->setContentsMargins(12, 10, 12, 12);
    inspectorLayout->setSpacing(8);
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
    inspectorLayout->addWidget(m_inspectorName);
    inspectorLayout->addWidget(m_inspectorDescription);
    inspectorLayout->addSpacing(8);
    inspectorLayout->addWidget(m_inspectorMeta);
    inspectorLayout->addWidget(m_inspectorCapabilities);

    m_inspectorContributionHost = new QWidget(inspectorContent);
    m_inspectorContributionHost->setObjectName(QStringLiteral("workspaceInspectorHost"));
    m_inspectorContributionLayout = new QVBoxLayout(m_inspectorContributionHost);
    m_inspectorContributionLayout->setContentsMargins(0, 0, 0, 0);
    m_inspectorContributionLayout->setSpacing(0);
    m_inspectorContributionHost->hide();
    inspectorLayout->addWidget(m_inspectorContributionHost, 1);
    inspectorLayout->addStretch();
    inspectorScroll->setWidget(inspectorContent);
    inspectorShell.bodyLayout->addWidget(inspectorScroll, 1);
    m_inspectorDock->setWidget(inspector);
    m_inspectorDockHeader = inspector->findChild<QWidget *>(
        QStringLiteral("dockHeader"));
    installFloatingDockChrome(m_inspectorDock,
                              m_inspectorDockHeader,
                              inspectorTitleBar, tr("数据与属性"));
    connect(m_inspectorDock, &QDockWidget::topLevelChanged, this,
            [this](const bool) {
                QTimer::singleShot(0, this,
                                   &MainWindow::synchronizePanelHeaderControls);
            });
    m_inspectorDock->setMinimumWidth(SidePanelMinimumWidth);
    m_inspectorDock->setMaximumWidth(SidePanelMaximumWidth);
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
    connect(m_navigationDock, &QDockWidget::visibilityChanged, this,
            [this](const bool visible) {
                if (!visible || !m_pageLayoutsReady || m_applyingPageLayout
                    || m_pageLayoutContext.isEmpty()) {
                    return;
                }
                QTimer::singleShot(0, this, [this] {
                    if (!m_navigationDock || m_navigationDock->isHidden()) return;
                    resizeSideDocksForPage(storedPageLayout(m_pageLayoutContext));
                });
            });
    connect(m_inspectorDock, &QDockWidget::visibilityChanged,
            m_inspectorToggle, &QToolButton::setChecked);
    connect(m_inspectorDock, &QDockWidget::visibilityChanged, this,
            [this](const bool visible) {
                if (m_pageLayoutsReady && !m_applyingPageLayout) {
                    if (!visible) {
                        savePageLayout(m_pageLayoutContext);
                        return;
                    }
                    QTimer::singleShot(0, this, [this] {
                        if (!m_inspectorDock || m_inspectorDock->isHidden()) return;
                        const PageLayoutPreset layout = storedPageLayout(
                            m_pageLayoutContext);
                        const bool applying = m_applyingPageLayout;
                        m_applyingPageLayout = true;
                        resizeSideDocksForPage(layout);
                        m_applyingPageLayout = applying;
                        savePageLayout(m_pageLayoutContext);
                    });
                }
            });
    connect(m_outputDock, &QDockWidget::visibilityChanged,
            m_outputToggle, &QToolButton::setChecked);
    connect(m_outputDock, &QDockWidget::visibilityChanged, this,
            [this](const bool visible) {
                if (!m_pageLayoutsReady || m_applyingPageLayout) return;
                if (!visible) {
                    savePageLayout(m_pageLayoutContext);
                    return;
                }
                const quint64 revision = ++m_pageLayoutRevision;
                QTimer::singleShot(0, this, [this, revision] {
                    if (revision != m_pageLayoutRevision) return;
                    resizeOutputDockForCurrentPage();
                    savePageLayout(m_pageLayoutContext);
                });
            });
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
}

void MainWindow::buildMenu()
{
    auto *workspace = new QMenu(this);
    auto *home = workspace->addAction(tr("返回开始"));
    home->setShortcut(QKeySequence(QStringLiteral("Ctrl+Home")));
    connect(home, &QAction::triggered, this, [this] { activatePage(HomeId); });
    auto *components = workspace->addAction(tr("打开/退出插件管理"));
    components->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(components, &QAction::triggered,
            this, &MainWindow::toggleComponentManagement);
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
    auto *reset = workspace->addAction(tr("恢复当前页面默认布局"));
    connect(reset, &QAction::triggered, this, &MainWindow::resetDockLayout);
    auto *resetAll = workspace->addAction(tr("恢复全部页面默认布局…"));
    connect(resetAll, &QAction::triggered, this, [this] {
        if (QMessageBox::question(
                this, tr("恢复全部页面默认布局"),
                tr("将清除 Serial Scope 和 TN160 各页面保存的面板比例与可见性。\n"
                   "串口、标签和标定数据不会改变。"),
                QMessageBox::RestoreDefaults | QMessageBox::Cancel,
                QMessageBox::Cancel) == QMessageBox::RestoreDefaults) {
            resetAllDockLayouts();
        }
    });
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
        edit->addAction(resetAll);

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

void MainWindow::toggleComponentManagement()
{
    if (m_activePage != ComponentsId) {
        activatePage(ComponentsId);
        return;
    }

    QString target = m_pageBeforeComponentManagement;
    if (target.isEmpty() || target == ComponentsId
        || (target != HomeId && !m_components->isLoaded(target))) {
        target = HomeId;
    }
    activatePage(target);
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
    QSettings().setValue(LastComponentImportDirectoryKey,
                         QFileInfo(source).absolutePath());
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
    if (!m_activePage.isEmpty()
        && m_activePage != HomeId && m_activePage != ComponentsId
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
        // Card actions can rebuild both grids and delete the sending card.
        // Let the button event return first and copy the component id into the
        // queued call before any card is destroyed.
        connect(card, &PluginCard::primaryRequested,
                this, &MainWindow::requestPrimaryAction,
                Qt::QueuedConnection);
        connect(card, &PluginCard::unloadRequested,
                this, &MainWindow::requestUnload,
                Qt::QueuedConnection);
        connect(card, &PluginCard::removeRequested,
                this, &MainWindow::requestRemove,
                Qt::QueuedConnection);
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
    if (m_pageLayoutsReady) {
        savePageLayout(m_pageLayoutContext);
        m_pageLayoutContext.clear();
        ++m_pageLayoutRevision;
    }
    detachWorkspacePanels();
    if (pageId == HomeId || pageId.isEmpty()) {
        m_activePage = HomeId;
        m_stack->setCurrentIndex(0);
        updateInspector(nullptr);
        m_closeWorkspace->hide();
        m_unloadComponent->hide();
        setStatus(tr("就绪 · 从组件库加载仪器"));
    } else if (pageId == ComponentsId) {
        if (m_activePage == HomeId
            || (!m_activePage.isEmpty() && m_components->isLoaded(m_activePage))) {
            m_pageBeforeComponentManagement = m_activePage;
        }
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
    QSettings().setValue(LastActivePageSettingsKey, m_activePage);
    rebuildWorkspaceTabs();
    if (synchronizeNavigation) selectNavigation(m_activePage);
    selectWorkspaceTab(m_activePage);
    synchronizeContextPanels();
    switchPageLayoutContext();
}

void MainWindow::activateComponent(const QString &componentId,
                                   const bool synchronizeNavigation)
{
    if (m_pageLayoutsReady && !m_pageLayoutContext.isEmpty()) {
        savePageLayout(m_pageLayoutContext);
        m_pageLayoutContext.clear();
        ++m_pageLayoutRevision;
    }
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
        QString contractError;
        if (!validateWorkspaceContract(workspace, &contractError)) {
            if (auto *managed = qobject_cast<IDeviceWorkspace *>(workspace)) {
                managed->shutdown();
            }
            delete workspace;
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            log(QStringLiteral("error"),
                tr("组件工作区不符合宿主模板：%1").arg(contractError));
            QMessageBox::warning(this, tr("组件工作区无效"), contractError);
            return;
        }
        auto created = std::make_unique<WorkspaceRecord>();
        created->componentId = componentId;
        created->widget = workspace;
        created->stackIndex = m_stack->addWidget(workspace);
        record = created.get();
        m_workspaces.emplace_back(std::move(created));
        if (auto *contributor = qobject_cast<IWorkspaceUiProvider *>(workspace)) {
            if (WorkspaceUiProvider *provider = contributor->workspaceUiProvider()) {
                connect(provider, &WorkspaceUiProvider::presentationAboutToChange,
                        this, [this, provider] {
                            if (provider == activeWorkspaceUiProvider()) {
                                preparePageLayoutChange();
                            }
                        });
                connect(provider, &WorkspaceUiProvider::presentationChanged,
                        this, [this, provider] {
                            if (provider == activeWorkspaceUiProvider()) {
                                synchronizeContextPanels();
                                switchPageLayoutContext();
                            }
                });
            }
        }
        if (auto *canvas = workspace->findChild<WorkspaceCanvas *>()) {
            connect(canvas, &WorkspaceCanvas::componentDropRequested, this,
                    [this, workspace](const QString &id, const QPoint &position) {
                if (m_stack && m_stack->currentWidget() == workspace) {
                    createRegisteredComponentAt(id, position);
                }
            });
        }
        if (m_sleepButton && m_sleepButton->isChecked()) applyDarkMode(true);
        log(QStringLiteral("info"), tr("已创建设备工作区：%1").arg(component.metadata.name));
        refreshComponentUi();
    }
    m_activePage = componentId;
    m_stack->setCurrentWidget(record->widget);
    QSettings().setValue(LastActivePageSettingsKey, m_activePage);
    updateInspector(&component.metadata);
    setStatus(tr("%1 · 工作区运行中").arg(component.metadata.name));
    rebuildWorkspaceTabs();
    if (synchronizeNavigation) selectNavigation(componentId);
    selectWorkspaceTab(componentId);
    synchronizeContextPanels();
    switchPageLayoutContext();
}

void MainWindow::requestPrimaryAction(const QString &componentId)
{
    // Loading emits componentsChanged.  Keep an owned copy across that
    // re-entrant boundary; the originating card can be rebuilt afterwards.
    const QString id = componentId;
    if (!m_components->isLoaded(id)) {
        QString error;
        if (!m_components->load(id, &error)) {
            QMessageBox::warning(this, tr("加载失败"), error);
            return;
        }
        activateComponent(id);
        return;
    }
    activateComponent(id);
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
    if (m_stack && m_stack->currentWidget() == widget) {
        detachWorkspacePanels();
        m_componentRegistry.clear();
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
    const QString lastDirectory = QSettings().value(
        LastComponentImportDirectoryKey).toString();
    if (!lastDirectory.isEmpty() && QDir(lastDirectory).exists()) {
        return QDir(lastDirectory).absolutePath();
    }

    const QString packagedPath = ComponentManager::packagedStorePath();
    if (!packagedPath.isEmpty()) return packagedPath;
    QDir sibling(QCoreApplication::applicationDirPath());
    sibling.cdUp();
    QDir buildComponents(sibling);
    if (buildComponents.cd(QStringLiteral("component_packages"))) {
        return buildComponents.absolutePath();
    }
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
    const QString context = currentPageLayoutContext();
    if (context.isEmpty()) {
        applyDefaultDockLayout();
        log(QStringLiteral("info"), tr("已恢复宿主默认面板布局"));
        return;
    }
    QSettings().remove(PageLayoutSettingsGroup + QLatin1Char('/') + context);
    m_applyingPageLayout = true;
    m_outputDock->setFloating(false);
    m_inspectorDock->setFloating(false);
    m_centerWorkbenchWindow->addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);
    m_workbenchWindow->addDockWidget(Qt::LeftDockWidgetArea, m_inspectorDock);
    m_applyingPageLayout = false;
    m_pageLayoutContext = context;
    applyPageLayout(context, defaultPageLayout(context));
    log(QStringLiteral("info"), tr("已恢复当前页面默认布局：%1").arg(context));
}

void MainWindow::preparePageLayoutChange()
{
    if (!m_pageLayoutsReady) return;
    savePageLayout(m_pageLayoutContext);
    m_pageLayoutContext.clear();
    ++m_pageLayoutRevision;
    // presentationAboutToChange is the ownership barrier: return the old
    // panel bodies and header controls before the component changes pages or
    // destroys any of its presentation objects.
    detachWorkspacePanels();
}

void MainWindow::resetAllDockLayouts()
{
    QSettings settings;
    settings.remove(PageLayoutSettingsGroup);
    settings.remove(DockSettingsKey);
    settings.remove(CentralDockSettingsKey);
    m_applyingPageLayout = true;
    applyDefaultDockLayout();
    m_applyingPageLayout = false;
    m_pageLayoutContext = currentPageLayoutContext();
    if (!m_pageLayoutContext.isEmpty()) {
        applyPageLayout(m_pageLayoutContext, defaultPageLayout(m_pageLayoutContext));
    }
    log(QStringLiteral("info"), tr("已恢复全部页面默认布局；连接与标定数据未改变"));
}

void MainWindow::refreshComponentLibrary()
{
    if (!m_componentLibraryList) return;
    m_componentLibraryList->clear();

    const QList<WorkspaceComponentDefinition> definitions =
        m_componentRegistry.definitions();
    QSet<QString> registeredIds;
    for (const WorkspaceComponentDefinition &definition : definitions) {
        auto *item = new QListWidgetItem(QIcon(definition.iconSource), QString(),
                                         m_componentLibraryList);
        item->setData(Qt::UserRole, definition.definitionId);
        item->setToolTip(tr("%1\n拖动到中央工作区添加")
                             .arg(definition.displayName));
        item->setStatusTip(definition.displayName);
        registeredIds.insert(definition.definitionId);
    }

    WorkspaceUiProvider *provider = activeWorkspaceUiProvider();
    const WorkspaceLibraryPresentation library = provider
        ? provider->context().componentLibrary : WorkspaceLibraryPresentation{};
    for (const WorkspaceLibraryItem &entry : library.items) {
        if (registeredIds.contains(entry.id)) continue;
        auto *item = new QListWidgetItem(QIcon(entry.iconSource), QString(),
                                         m_componentLibraryList);
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(tr("%1\n拖动到中央工作区添加").arg(entry.title));
        item->setStatusTip(entry.title);
    }
}

void MainWindow::synchronizeComponentRegistry()
{
    m_componentRegistry.clear();
    QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
    auto *provider = workspace
        ? qobject_cast<IWorkspaceComponentProvider *>(workspace) : nullptr;
    if (!provider) return;

    QString error;
    // The registry is scoped to the active workspace and cleared before each
    // refresh. The workspace itself remains owned by m_workspaces, so no
    // extra destroyed callback is needed here (and repeated refreshes cannot
    // accumulate callbacks for the same provider address).
    if (!m_componentRegistry.registerProvider(provider, nullptr, &error)) {
        log(QStringLiteral("warning"),
            tr("忽略工作区组件定义：%1").arg(error));
    }
}

bool MainWindow::createRegisteredComponentAt(const QString &componentId,
                                              const QPoint &position)
{
    if (componentId.trimmed().isEmpty() || !m_componentRegistry.contains(componentId)) {
        return false;
    }
    QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
    auto *canvas = workspace ? workspace->findChild<WorkspaceCanvas *>() : nullptr;
    if (!canvas) return false;

    // New component providers are activated through the registry. Existing
    // canvas instances are reused (important for a live device session); a
    // factory is called only when the definition has not already been added
    // to the current canvas.
    bool added = false;
    if (canvas->componentFrame(componentId)) {
        added = canvas->addOrShowComponentAt(componentId);
    } else {
        const WorkspaceComponentDefinition definition =
            m_componentRegistry.definition(componentId);
        QString error;
        QWidget *content = m_componentRegistry.createComponent(
            componentId, canvas, &error);
        if (content) {
            QRectF geometry(0.08, 0.08, 0.84, 0.60);
            if (canvas->width() > 0 && canvas->height() > 0) {
                const qreal width = std::clamp(
                    qreal(definition.defaultSize.width()) / canvas->width(),
                    qreal(0.20), qreal(0.84));
                const qreal height = std::clamp(
                    qreal(definition.defaultSize.height()) / canvas->height(),
                    qreal(0.20), qreal(0.84));
                const QPoint target = position.x() >= 0 && position.y() >= 0
                    ? position : QPoint(canvas->width() / 2,
                                        canvas->height() / 2);
                geometry = QRectF(
                    std::clamp((target.x() / qreal(canvas->width())) - width / 2.0,
                               0.0, 1.0 - width),
                    std::clamp((target.y() / qreal(canvas->height())) - height / 2.0,
                               0.0, 1.0 - height), width, height);
            }
            added = canvas->addComponent(
                definition.definitionId, definition.displayName, content,
                geometry, definition.minimumSize) != nullptr;
            if (!added) content->deleteLater();
        } else {
            log(QStringLiteral("warning"),
                tr("组件创建失败：%1").arg(error));
        }
    }
    if (added) {
        return true;
    }

    return false;
}

void MainWindow::showLibraryComponent(const QString &componentId)
{
    if (componentId.trimmed().isEmpty()) return;

    if (createRegisteredComponentAt(componentId, QPoint(-1, -1))) {
        if (m_componentLibraryDock) m_componentLibraryDock->hide();
        if (m_componentsToggle) m_componentsToggle->setChecked(false);
        return;
    }

    WorkspaceUiProvider *provider = activeWorkspaceUiProvider();
    const WorkspaceLibraryPresentation library = provider
        ? provider->context().componentLibrary : WorkspaceLibraryPresentation{};
    bool added = false;
    const bool invoked = library.activationTarget
        && !library.activationMethod.isEmpty()
        && QMetaObject::invokeMethod(
            library.activationTarget, library.activationMethod.constData(),
            Qt::DirectConnection, Q_RETURN_ARG(bool, added),
            Q_ARG(QString, componentId));
    if (invoked && added) {
        if (m_componentLibraryDock) m_componentLibraryDock->hide();
        if (m_componentsToggle) m_componentsToggle->setChecked(false);
        return;
    }
    log(QStringLiteral("warning"), tr("当前工作区不支持组件：%1").arg(componentId));
}

WorkspaceUiProvider *MainWindow::activeWorkspaceUiProvider() const
{
    QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
    auto *contributor = workspace ? qobject_cast<IWorkspaceUiProvider *>(workspace)
                                  : nullptr;
    return contributor ? contributor->workspaceUiProvider() : nullptr;
}

void MainWindow::setDockTitle(QDockWidget *dock, const QString &title)
{
    if (!dock || title.isEmpty()) return;
    dock->setWindowTitle(title);
    const auto labels = dock->findChildren<QLabel *>(
        QStringLiteral("dockTitle"), Qt::FindChildrenRecursively);
    for (QLabel *label : labels) label->setText(title);
}

void MainWindow::detachPanelHeaderControl(
    QWidget *header, HeaderControlAttachment &attachment)
{
    if (header) {
        if (attachment.control) {
            if (auto *layout = qobject_cast<QBoxLayout *>(header->layout())) {
                layout->removeWidget(attachment.control);
            }
        }
        if (auto *title = header->findChild<QLabel *>(QStringLiteral("dockTitle"))) {
            title->show();
        }
    }
    if (!attachment.control) {
        attachment.owner.clear();
        attachment.layoutIndex = -1;
        return;
    }

    QWidget *control = attachment.control;
    if (attachment.owner) {
        QWidget *owner = attachment.owner;
        control->setParent(owner);
        if (auto *layout = qobject_cast<QBoxLayout *>(owner->layout())) {
            layout->removeWidget(control);
            const int restoreIndex = attachment.layoutIndex >= 0
                ? std::min(attachment.layoutIndex, layout->count())
                : 0;
            layout->insertWidget(restoreIndex, control);
            control->show();
        } else {
            control->hide();
        }
    } else {
        control->hide();
    }
    attachment.control.clear();
    attachment.owner.clear();
    attachment.layoutIndex = -1;
}

void MainWindow::synchronizePanelHeaderControl(
    QDockWidget *dock, QWidget *panel, QWidget *header,
    const WorkspacePanelOptions &options,
    HeaderControlAttachment &attachment)
{
    detachPanelHeaderControl(header, attachment);
    QWidget *control = options.headerControl;
    if (!dock || !panel || !header || !control || control == panel
        || !panel->isAncestorOf(control)) return;

    QWidget *owner = control->parentWidget();
    auto *ownerLayout = owner
        ? qobject_cast<QBoxLayout *>(owner->layout()) : nullptr;
    const int ownerIndex = ownerLayout ? ownerLayout->indexOf(control) : -1;
    auto *headerLayout = qobject_cast<QBoxLayout *>(header->layout());
    if (!ownerLayout || ownerIndex < 0 || !headerLayout) return;

    attachment.control = control;
    attachment.owner = owner;
    attachment.layoutIndex = ownerIndex;
    if (dock->isFloating()) return;

    ownerLayout->removeWidget(control);
    control->setParent(header);
    headerLayout->insertWidget(0, control, 1);
    if (auto *title = header->findChild<QLabel *>(QStringLiteral("dockTitle"))) {
        title->hide();
    }
    control->show();
}

void MainWindow::synchronizePanelHeaderControls()
{
    const WorkspaceUiPresentationOptions options = m_activeUiProvider
        ? m_activeUiProvider->presentationOptions()
        : WorkspaceUiPresentationOptions{};
    synchronizePanelHeaderControl(m_navigationDock, m_attachedContextPanel,
                                  m_navigationDockHeader, options.contextPanel,
                                  m_contextHeaderAttachment);
    synchronizePanelHeaderControl(m_inspectorDock, m_attachedInspectorPanel,
                                  m_inspectorDockHeader, options.inspectorPanel,
                                  m_inspectorHeaderAttachment);
    synchronizePanelHeaderControl(m_outputDock, m_attachedOutputPanel,
                                  m_outputDockHeader, options.outputPanel,
                                  m_outputHeaderAttachment);
}

void MainWindow::detachWorkspaceHeaderControls()
{
    detachPanelHeaderControl(m_navigationDockHeader, m_contextHeaderAttachment);
    detachPanelHeaderControl(m_inspectorDockHeader, m_inspectorHeaderAttachment);
    detachPanelHeaderControl(m_outputDockHeader, m_outputHeaderAttachment);
}

void MainWindow::detachWorkspacePanels()
{
    detachWorkspaceHeaderControls();
    const auto detach = [](QPointer<QWidget> &content,
                           QPointer<QWidget> &owner,
                           QVBoxLayout *layout) {
        if (!content) {
            owner.clear();
            return;
        }
        if (layout) layout->removeWidget(content);
        content->hide();
        if (owner) content->setParent(owner);
        content.clear();
        owner.clear();
    };
    detach(m_attachedContextPanel, m_attachedContextPanelOwner,
           m_contextContributionLayout);
    detach(m_attachedInspectorPanel, m_attachedInspectorPanelOwner,
           m_inspectorContributionLayout);
    detach(m_attachedOutputPanel, m_attachedOutputPanelOwner,
           m_outputTrendLayout);
    m_activeUiProvider.clear();
    if (m_outputTrendEmpty) m_outputTrendEmpty->show();
}

void MainWindow::synchronizeContextPanels()
{
    synchronizeComponentRegistry();
    WorkspaceUiProvider *provider = activeWorkspaceUiProvider();
    detachWorkspacePanels();
    if (provider) {
        QWidget *workspace = m_stack ? m_stack->currentWidget() : nullptr;
        QString contractError;
        if (!validateWorkspaceContract(workspace, &contractError)) {
            log(QStringLiteral("error"),
                tr("组件 UI Context 不符合宿主模板：%1").arg(contractError));
            setStatus(tr("组件 UI Context 已拒绝 · %1").arg(contractError));
            provider = nullptr;
        }
    }
    m_activeUiProvider = provider;

    const WorkspaceUiContext context = provider ? provider->context()
                                                 : WorkspaceUiContext{};
    const WorkspaceUiPresentationOptions presentationOptions =
        provider ? provider->presentationOptions()
                 : WorkspaceUiPresentationOptions{};
    const auto attach = [this](const WorkspacePanelPresentation &panel,
                                const WorkspacePanelOptions &options,
                                const WorkspacePanelSlot slot,
                                QWidget *host, QVBoxLayout *layout,
                                QPointer<QWidget> &attached,
                                QPointer<QWidget> &owner) {
        if (!panel.content || !host || !layout) return false;
        const bool standardMargins = options.margins == WorkspacePanelMargins::Standard
            || (options.margins == WorkspacePanelMargins::Default
                && slot != WorkspacePanelSlot::Output);
        layout->setContentsMargins(standardMargins ? 12 : 0,
                                   standardMargins ? 10 : 0,
                                   standardMargins ? 12 : 0,
                                   standardMargins ? 12 : 0);
        owner = panel.content->parentWidget();
        panel.content->setParent(host);
        layout->addWidget(panel.content, 1);
        attached = panel.content;
        const bool dark = property("darkMode").toBool();
        const auto applyPanelTheme = [dark](QWidget *widget) {
            if (!widget) return;
            widget->setProperty("darkMode", dark);
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        };
        applyPanelTheme(panel.content);
        for (QWidget *child : panel.content->findChildren<QWidget *>()) {
            applyPanelTheme(child);
        }
        panel.content->show();
        return true;
    };

    const bool hasContext = attach(context.contextPanel,
                                   presentationOptions.contextPanel,
                                   WorkspacePanelSlot::Context,
                                   m_contextContributionHost,
                                   m_contextContributionLayout,
                                   m_attachedContextPanel,
                                   m_attachedContextPanelOwner);
    if (m_contextContributionHost) m_contextContributionHost->setVisible(hasContext);
    if (m_contextFallback) m_contextFallback->setVisible(!hasContext);
    setDockTitle(m_navigationDock,
                 hasContext && !context.contextPanel.title.isEmpty()
                     ? context.contextPanel.title : tr("设备连接"));

    const bool hasInspector = attach(context.inspectorPanel,
                                     presentationOptions.inspectorPanel,
                                     WorkspacePanelSlot::Inspector,
                                     m_inspectorContributionHost,
                                     m_inspectorContributionLayout,
                                     m_attachedInspectorPanel,
                                     m_attachedInspectorPanelOwner);
    if (m_inspectorBodyLayout) {
        m_inspectorBodyLayout->setContentsMargins(hasInspector ? 0 : 12,
                                                   hasInspector ? 0 : 10,
                                                   hasInspector ? 0 : 12,
                                                   hasInspector ? 0 : 12);
        m_inspectorBodyLayout->setSpacing(hasInspector ? 0 : 8);
    }
    if (m_inspectorContributionHost) m_inspectorContributionHost->setVisible(hasInspector);
    for (QWidget *generic : {static_cast<QWidget *>(m_inspectorName),
                             static_cast<QWidget *>(m_inspectorDescription),
                             static_cast<QWidget *>(m_inspectorMeta),
                             static_cast<QWidget *>(m_inspectorCapabilities)}) {
        if (generic) generic->setVisible(!hasInspector);
    }
    setDockTitle(m_inspectorDock,
                 hasInspector && !context.inspectorPanel.title.isEmpty()
                     ? context.inspectorPanel.title : tr("数据与属性"));

    const bool hasOutput = attach(context.outputPanel,
                                  presentationOptions.outputPanel,
                                  WorkspacePanelSlot::Output,
                                  m_outputTrendHost,
                                  m_outputTrendLayout,
                                  m_attachedOutputPanel,
                                  m_attachedOutputPanelOwner);
    if (m_outputTrendEmpty) m_outputTrendEmpty->setVisible(!hasOutput);
    if (m_outputStack) {
        m_outputStack->setCurrentWidget(hasOutput ? m_outputTrendHost : m_output);
    }
    const QString outputTitle = hasOutput && !context.outputPanel.title.isEmpty()
        ? context.outputPanel.title : tr("输出终端");
    setDockTitle(m_outputDock, outputTitle);
    synchronizePanelHeaderControls();
    if (m_outputClear) {
        const bool clearable = hasOutput && context.outputPanel.clearTarget
            && !context.outputPanel.clearMethod.isEmpty();
        m_outputClear->setVisible(!hasOutput || clearable);
        m_outputClear->setText(clearable ? tr("清空诊断") : tr("清空"));
        m_outputClear->setToolTip(clearable ? tr("清空当前工作区诊断历史")
                                             : tr("清空终端输出"));
    }
    if (m_outputToggle) m_outputToggle->setToolTip(outputTitle);
    refreshComponentLibrary();
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
    resizeSideDocksForPage(PageLayoutPreset{});
}

QString MainWindow::currentPageLayoutContext() const
{
    WorkspaceUiProvider *provider = m_activeUiProvider;
    return provider ? provider->context().id : QString{};
}

WorkspaceLayoutProfile MainWindow::currentLayoutProfile() const
{
    WorkspaceUiProvider *provider = m_activeUiProvider;
    return provider ? provider->context().layoutProfile
                    : WorkspaceLayoutProfile::Standard;
}

MainWindow::PageLayoutPreset MainWindow::defaultPageLayout(
    const QString &context) const
{
    switch (m_pageLayoutProfiles.value(context, WorkspaceLayoutProfile::Standard)) {
    case WorkspaceLayoutProfile::FirmwareTask:
        return {0.36, true, false, SidePanelDefaultWidth};
    case WorkspaceLayoutProfile::CompactTask:
        return {0.38, true, false, SidePanelDefaultWidth};
    case WorkspaceLayoutProfile::Diagnostics:
        return {0.60, true, true, SidePanelDefaultWidth};
    case WorkspaceLayoutProfile::CalibrationTask:
        return {0.55, true, true, SidePanelDefaultWidth};
    case WorkspaceLayoutProfile::Standard:
        return {0.40, true, false, SidePanelDefaultWidth};
    }
    return {};
}

MainWindow::PageLayoutPreset MainWindow::storedPageLayout(
    const QString &context) const
{
    PageLayoutPreset layout = defaultPageLayout(context);
    if (context.isEmpty()) return layout;
    QSettings settings;
    settings.beginGroup(PageLayoutSettingsGroup + QLatin1Char('/') + context);
    layout.outputFraction = std::clamp(
        settings.value(QStringLiteral("outputFraction"), layout.outputFraction)
            .toDouble(), 0.20, 0.78);
    layout.outputVisible = settings.value(
        QStringLiteral("outputVisible"), layout.outputVisible).toBool();
    layout.inspectorVisible = settings.value(
        QStringLiteral("inspectorVisible"), layout.inspectorVisible).toBool();
    const int legacyWidth = settings.value(QStringLiteral("inspectorWidth"),
                                           layout.sidePanelWidth).toInt();
    layout.sidePanelWidth = std::clamp(
        settings.value(QStringLiteral("sidePanelWidth"), legacyWidth).toInt(),
        SidePanelMinimumWidth, SidePanelMaximumWidth);
    return layout;
}

void MainWindow::savePageLayout(const QString &context)
{
    if (!m_pageLayoutsReady || m_applyingPageLayout || context.isEmpty()
        || !m_outputDock || !m_inspectorDock) {
        return;
    }
    PageLayoutPreset layout = storedPageLayout(context);
    const PageLayoutPreset recommended = defaultPageLayout(context);
    layout.outputVisible = !m_outputDock->isHidden();
    layout.inspectorVisible = !m_inspectorDock->isHidden();
    if (layout.outputVisible && !m_outputDock->isFloating()
        && m_centerWorkbenchWindow) {
        const int referenceHeight = m_centerWorkbenchWindow->contentsRect().height();
        if (referenceHeight > 100 && m_outputDock->height() > 40) {
            layout.outputFraction = std::clamp(
                qreal(m_outputDock->height()) / qreal(referenceHeight), 0.20, 0.78);
        }
    }
    QDockWidget *widthSource = nullptr;
    if (layout.inspectorVisible && !m_inspectorDock->isFloating()) {
        widthSource = m_inspectorDock;
    } else if (m_navigationDock && !m_navigationDock->isHidden()
               && !m_navigationDock->isFloating()) {
        widthSource = m_navigationDock;
    }
    if (widthSource && widthSource->width() >= SidePanelMinimumWidth) {
        layout.sidePanelWidth = std::clamp(widthSource->width(),
                                           SidePanelMinimumWidth,
                                           SidePanelMaximumWidth);
    }

    const bool customized = std::abs(layout.outputFraction
                                     - recommended.outputFraction) > 0.015
        || layout.outputVisible != recommended.outputVisible
        || layout.inspectorVisible != recommended.inspectorVisible
        || std::abs(layout.sidePanelWidth - recommended.sidePanelWidth) > 8;
    QSettings settings;
    const QString group = PageLayoutSettingsGroup + QLatin1Char('/') + context;
    settings.remove(group);
    if (!customized) return;
    settings.beginGroup(group);
    settings.setValue(QStringLiteral("outputFraction"), layout.outputFraction);
    settings.setValue(QStringLiteral("outputVisible"), layout.outputVisible);
    settings.setValue(QStringLiteral("inspectorVisible"), layout.inspectorVisible);
    settings.setValue(QStringLiteral("sidePanelWidth"), layout.sidePanelWidth);
}

void MainWindow::switchPageLayoutContext()
{
    if (!m_pageLayoutsReady) return;
    const QString context = currentPageLayoutContext();
    if (context == m_pageLayoutContext) return;
    savePageLayout(m_pageLayoutContext);
    if (!context.isEmpty()) m_pageLayoutProfiles[context] = currentLayoutProfile();
    m_pageLayoutContext = context;
    ++m_pageLayoutRevision;
    if (!context.isEmpty()) applyPageLayout(context, storedPageLayout(context));
}

void MainWindow::applyPageLayout(const QString &context,
                                 const PageLayoutPreset &layout)
{
    if (context.isEmpty() || !m_outputDock || !m_inspectorDock) return;
    const quint64 revision = ++m_pageLayoutRevision;
    m_applyingPageLayout = true;
    m_outputDock->setVisible(layout.outputVisible);
    m_inspectorDock->setVisible(layout.inspectorVisible);
    if (m_workbenchWindow) {
        resizeSideDocksForPage(layout);
    }
    if (layout.outputVisible && !m_outputDock->isFloating()
        && m_centerWorkbenchWindow) {
        int referenceHeight = m_centerWorkbenchWindow->contentsRect().height();
        if (referenceHeight < 100) referenceHeight = height();
        m_centerWorkbenchWindow->resizeDocks(
            {m_outputDock}, {std::max(1, qRound(referenceHeight
                                                * layout.outputFraction))},
            Qt::Vertical);
    }
    m_applyingPageLayout = false;

    QTimer::singleShot(0, this, [this, context, revision] {
        if (revision != m_pageLayoutRevision || context != m_pageLayoutContext) return;
        const PageLayoutPreset settled = storedPageLayout(context);
        const bool applying = m_applyingPageLayout;
        m_applyingPageLayout = true;
        if (settled.inspectorVisible && !m_inspectorDock->isFloating()) {
            resizeSideDocksForPage(settled);
        }
        m_applyingPageLayout = applying;
        resizeOutputDockForCurrentPage();
    });
}

void MainWindow::resizeSideDocksForPage(const PageLayoutPreset &layout)
{
    if (!m_workbenchWindow) return;
    QList<QDockWidget *> docks;
    QList<int> widths;
    if (m_inspectorDock && !m_inspectorDock->isHidden()
        && !m_inspectorDock->isFloating()) {
        docks.append(m_inspectorDock);
        widths.append(layout.sidePanelWidth);
    }
    if (m_navigationDock && !m_navigationDock->isHidden()
        && !m_navigationDock->isFloating()) {
        docks.append(m_navigationDock);
        widths.append(layout.sidePanelWidth);
    }
    if (!docks.isEmpty()) {
        m_workbenchWindow->resizeDocks(docks, widths, Qt::Horizontal);
    }
}

void MainWindow::resizeOutputDockForCurrentPage()
{
    if (m_pageLayoutContext.isEmpty() || !m_outputDock
        || m_outputDock->isHidden() || m_outputDock->isFloating()
        || !m_centerWorkbenchWindow) {
        return;
    }
    int referenceHeight = m_centerWorkbenchWindow->contentsRect().height();
    if (referenceHeight < 100) referenceHeight = height();
    const qreal fraction = storedPageLayout(m_pageLayoutContext).outputFraction;
    const bool applying = m_applyingPageLayout;
    m_applyingPageLayout = true;
    m_centerWorkbenchWindow->resizeDocks(
        {m_outputDock}, {std::max(1, qRound(referenceHeight * fraction))},
        Qt::Vertical);
    m_applyingPageLayout = applying;
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

    if (m_pageLayoutsReady && !m_applyingPageLayout && m_outputDock
        && !m_outputDock->isHidden() && !m_outputDock->isFloating()) {
        const quint64 revision = ++m_pageLayoutRevision;
        QTimer::singleShot(0, this, [this, revision] {
            if (revision != m_pageLayoutRevision) return;
            resizeOutputDockForCurrentPage();
        });
    }

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
