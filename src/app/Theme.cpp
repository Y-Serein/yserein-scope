#include "Theme.h"

#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QPainter>
#include <QtGui/QPalette>
#include <QtWidgets/QApplication>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QStyleOption>

namespace {

class MaterialStyle final : public QProxyStyle {
public:
    explicit MaterialStyle(QStyle *base) : QProxyStyle(base) {}

    void drawPrimitive(const PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget) const override
    {
        if (element == PE_IndicatorArrowDown && option && painter) {
            const QRectF bounds = QRectF(option->rect).adjusted(6.0, 5.0, -6.0, -5.0);
            const bool dark = widget && widget->property("darkMode").toBool();
            QColor color = dark ? QColor(QStringLiteral("#aab5bf"))
                                : QColor(QStringLiteral("#667085"));
            if (!(option->state & State_Enabled)) color.setAlpha(95);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(color, 1.35, Qt::SolidLine,
                                 Qt::RoundCap, Qt::RoundJoin));
            const QPointF center = bounds.center();
            painter->drawLine(QPointF(center.x() - 3.5, center.y() - 1.5),
                              QPointF(center.x(), center.y() + 2.0));
            painter->drawLine(QPointF(center.x(), center.y() + 2.0),
                              QPointF(center.x() + 3.5, center.y() - 1.5));
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

} // namespace

namespace serein::scope::app {

void applyTheme(QApplication &application)
{
    application.setStyle(new MaterialStyle(application.style()));
    const QStringList families = QFontDatabase::families();
    const QString body = families.contains(QStringLiteral("Microsoft YaHei UI"))
        ? QStringLiteral("Microsoft YaHei UI")
        : families.contains(QStringLiteral("Noto Sans CJK SC"))
            ? QStringLiteral("Noto Sans CJK SC")
            : QStringLiteral("Segoe UI");
    application.setFont(QFont(body, 9));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#f7f8fa")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#1d2939")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#f4f6f8")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#344054")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#344054")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2f6feb")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    application.setPalette(palette);

    application.setStyleSheet(QString::fromUtf8(R"QSS(
QMainWindow { background: transparent; color: #1d2939; border: 0; }
QMainWindow::separator { width: 8px; height: 8px; background: #f4f5f7; border: 0; }
QMainWindow::separator:hover { background: #d7dee7; }
QWidget#globalMenuHost {
    background: transparent;
    border: 0;
    border-radius: 14px;
}
QWidget { color: #344054; font-size: 13px; }
QWidget#rootShell {
    background: #e3e6e8;
    border: 0;
    border-radius: 12px;
}
QFrame#workspaceSurface {
    background: #e3e6e8;
    border: 0;
    border-radius: 0;
}
QMainWindow#workbenchHost {
    background: #e3e6e8;
    border: 0;
}
QMainWindow#centerWorkbench {
    background: #e3e6e8;
    border: 0;
}
QMainWindow#workbenchHost::separator {
    background: transparent;
    width: 8px;
    height: 8px;
}
QMainWindow#centerWorkbench::separator {
    background: transparent;
    width: 8px;
    height: 8px;
}
QStatusBar#globalStatusBar {
    min-height: 24px;
    max-height: 30px;
    padding: 0;
    background: transparent;
    border: 0;
    color: #7a8699;
}
QStatusBar#globalStatusBar::item { border: 0; }

/* Serein shell: quiet chrome, precise alignment, one blue action color.
   Business surfaces use one 8 px outer radius. Window/top-bar/rail radii are
   shell geometry and deliberately remain separate from this component token. */
QFrame#workbenchBar {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #ffffff, stop: .68 #fbfcfd, stop: 1 #f2f5f6);
    border: 1px solid #c8d1da;
    border-top-color: #ffffff;
    border-bottom-color: #aab6c0;
    border-radius: 12px;
    padding: 0 2px;
}
QLabel#topbarIdentity {
    color: #1d2939;
    font-size: 12px;
    font-weight: 600;
    padding: 0 3px 0 2px;
}
QMenuBar#applicationMenu {
    background: transparent;
    border: 0;
    min-height: 32px;
    padding: 0;
}
QMenuBar#applicationMenu::item {
    color: #667085;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 5px;
    padding: 5px 8px;
}
QMenuBar#applicationMenu::item:selected,
QMenuBar#applicationMenu::item:pressed {
    color: #1d2939;
    background: #f4f6f8;
    border-color: #e1e7ee;
}
QFrame#topbarDivider {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #f8fafc, stop: .5 #bcc7d2, stop: 1 #f8fafc);
    border: 0;
}
QToolButton#topChromeButton,
QToolButton#panelChromeButton {
    color: #667085;
    background: transparent;
    border: 0;
    border-radius: 4px;
}
QToolButton#topChromeButton {
    min-width: 30px; max-width: 30px;
    min-height: 30px; max-height: 30px;
}
QToolButton#panelChromeButton {
    min-width: 26px; max-width: 26px;
    min-height: 26px; max-height: 26px;
}
QToolButton#topChromeButton:hover,
QToolButton#panelChromeButton:hover {
    background: #eaf1ff;
}
QToolButton#windowControl {
    min-width: 28px; max-width: 28px;
    min-height: 28px; max-height: 28px;
    color: #667085;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 5px;
}
QToolButton#windowControl:hover {
    color: #1d2939;
    background: #f4f6f8;
    border-color: #e1e7ee;
}
QToolButton#windowControl[windowAction="close"]:hover {
    color: #ffffff;
    background: #b54747;
    border-color: #b54747;
}
QLabel#sessionState {
    color: #667085;
    background: #f4f6f8;
    border: 1px solid #e1e7ee;
    border-radius: 9px;
    padding: 2px 9px;
    font-size: 10px;
}
QLabel#sessionState[state="connected"] {
    color: #1c8c62;
    background: #eaf6f0;
    border-color: #c9e8d8;
}

QTabBar#hostWorkspaceTabs { background: transparent; border: 0; }
QTabBar#hostWorkspaceTabs::tab {
    min-width: 82px; max-width: 210px;
    min-height: 36px;
    padding: 0 12px;
    color: #667085;
    background: transparent;
    border: 0;
    border-bottom: 2px solid transparent;
}
QTabBar#hostWorkspaceTabs::tab:hover { color: #1d2939; background: #f8fafb; }
QTabBar#hostWorkspaceTabs::tab:selected {
    color: #2f6feb;
    background: #ffffff;
    border-bottom-color: #2f6feb;
}
QTabBar#hostWorkspaceTabs QToolButton {
    min-width: 18px; max-width: 18px;
    min-height: 18px; max-height: 18px;
    margin-right: 4px;
    color: #98a2b3;
    border: 0;
    border-radius: 4px;
    background: transparent;
}
QTabBar#hostWorkspaceTabs QToolButton:hover { color: #1d2939; background: #eef2f6; }

QFrame#activityRail {
    background: #e3e6e8;
    border: 0;
    border-right: 1px solid #cbd4dc;
    border-radius: 0;
}
QFrame#surfaceRail {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #ffffff, stop: .68 #fbfcfd, stop: 1 #f2f5f6);
    border: 1px solid #c8d1da;
    border-top-color: #ffffff;
    border-bottom-color: #aab6c0;
    border-radius: 12px;
    padding: 0;
    min-width: 45px; max-width: 45px;
    min-height: 161px; max-height: 161px;
}
QFrame#surfaceRailDivider {
    min-width: 36px;
    max-width: 36px;
    background: rgba(111,123,133,.18);
    border: 0;
}
QFrame#panelRail { background: transparent; border: 0; }
QDockWidget#navigationDock,
QDockWidget#outputDock,
QDockWidget#inspectorDock {
    background: transparent;
    border: 0;
}
QWidget#dockHeader {
    /* QSS radii do not clip child widgets. The surface paints the rounded
       material; a transparent header cannot fill its corner cut-outs. */
    background: transparent;
    border: 0;
    border-bottom: 1px solid #e2e5e9;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
    min-height: 32px;
    max-height: 32px;
}
QWidget#dockFloatingTitleBar {
    background: #fafafb;
    border: 1px solid #cfd4db;
    border-bottom-color: #e2e5e9;
    border-radius: 0;
    border-top-left-radius: 8px;
    border-top-right-radius: 8px;
    min-height: 32px;
    max-height: 32px;
}
QLabel#dockTitle { color: #1d1f23; font-size: 12px; font-weight: 600; }
QDockWidget { color: #344054; }
QDockWidget > QWidget { background: transparent; border: 0; }
QDockWidget::title { background: transparent; border: 0; }
QWidget#dockPanelContent {
    background: #ffffff;
    border: 1px solid #e2e5e9;
    border-radius: 8px;
}
QWidget[workspacePanelContribution="true"],
QScrollArea[workspacePanelContribution="true"],
QScrollArea[workspacePanelContribution="true"] > QWidget > QWidget {
    background: transparent;
    border: 0;
}
QWidget#dockPanelBody { background: transparent; border: 0; }
QLabel#connectionTitle { color: #1d2939; font-size: 14px; font-weight: 600; }
QLabel#fieldLabel { color: #526171; font-size: 12px; font-weight: 600; letter-spacing: 0; }
QPushButton#workspaceAction { min-height: 28px; }
QWidget#workspaceAdvancedSection {
    background: #f8fafc;
    border: 1px solid #d7dee7;
    border-radius: 8px;
}
QWidget#advancedSectionHeader { background: transparent; border: 0; }
QPushButton#workspaceAdvancedToggle {
    min-height: 42px;
    color: #344054;
    background: transparent;
    border: 0;
    border-radius: 7px;
    padding: 0 34px 0 11px;
    text-align: left;
    font-size: 12px;
    font-weight: 600;
}
QPushButton#workspaceAdvancedToggle:hover { color: #1d2939; background: #eef2f6; }
QPushButton#workspaceAdvancedToggle:checked {
    border-radius: 0;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QLabel#advancedSectionIndicator {
    color: #667085;
    background: transparent;
    border: 0;
    padding-right: 11px;
    font-size: 16px;
    font-weight: 500;
}
QWidget#workspaceAdvancedBody {
    background: transparent;
    border: 0;
    border-top: 1px solid #e1e7ee;
    border-radius: 0;
}
QLabel#sectionLabel {
    color: #98a2b3;
    font-size: 9px;
    font-weight: 600;
    letter-spacing: 1px;
    padding: 2px 4px;
}
QLabel#panelHintTitle { color: #344054; font-size: 11px; padding: 4px 4px 0; }
QLabel#panelHint { color: #7a8699; font-size: 10px; padding: 0 4px; }
QLabel#panelVersion { color: #a0a8b7; font-size: 9px; padding: 4px; }
QLabel#workspaceContextFallback {
    color: #7a8699;
    padding: 16px;
    font-size: 11px;
}

QListWidget#pluginNav, QListWidget#channelList {
    background: transparent;
    border: 0;
    outline: 0;
}
QListWidget#componentLibraryList {
    background: transparent;
    border: 0;
    outline: 0;
    padding: 4px;
}
QListWidget#componentLibraryList::item {
    min-width: 54px;
    max-width: 54px;
    min-height: 54px;
    max-height: 54px;
    margin: 2px;
    padding: 0;
    color: #526171;
    border: 1px solid transparent;
    border-radius: 8px;
}
QListWidget#componentLibraryList::item:hover {
    color: #2f6feb;
    background: #eaf1ff;
    border-color: #b9cdf5;
}
QListWidget#componentLibraryList::item:selected {
    color: #2f6feb;
    background: #eef6ff;
    border-color: #b9cdf5;
}
QListWidget#pluginNav::item {
    min-height: 34px;
    margin: 2px 0;
    padding: 0 10px;
    color: #667085;
    border: 0;
    border-radius: 4px;
}
QListWidget#pluginNav::item:hover { color: #1d2939; background: #f4f6f8; }
QListWidget#pluginNav::item:selected { color: #2f6feb; background: #eaf1ff; }
QListWidget#channelList::item { min-height: 30px; border-bottom: 1px solid #edf0f4; }

/* Work surface. The light shell is deliberately quieter than the instrument canvas. */
QStackedWidget#workspaceStack,
QWidget#workbenchPage,
QScrollArea#workbenchScroll,
QScrollArea#workbenchScroll > QWidget > QWidget,
QStackedWidget[shellRole="workspace-stack"],
QWidget#workspaceCanvas {
    background: #e3e6e8;
    border: 0;
    border-radius: 8px;
}
QWidget[shellRole="workspace-content"] { background: #e3e6e8; }
QFrame#functionTabsComponent {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #ffffff, stop: .68 #fbfcfd, stop: 1 #f2f5f6);
    border: 1px solid #c8d1da;
    border-top-color: #ffffff;
    border-bottom-color: #aab6c0;
    border-radius: 8px;
}
QTabBar#workspaceTabs {
    background: transparent;
    border: 0;
}
QTabBar#workspaceTabs::tab {
    min-width: 96px;
    min-height: 32px;
    padding: 0 13px;
    color: #667085;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 5px;
}
QTabBar#workspaceTabs::tab:hover { color: #1d2939; background: #f8fafb; border-color: #e1e7ee; }
QTabBar#workspaceTabs::tab:selected {
    color: #2f6feb;
    background: #eef6ff;
    border-color: #b9cdf5;
}

QFrame#pageToolbar { background: transparent; border: 0; }
QFrame#pageToolbar {
    min-height: 44px;
    max-height: 44px;
    padding: 0 2px;
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #ffffff, stop: .68 #fbfcfd, stop: 1 #f2f5f6);
    border: 1px solid #c8d1da;
    border-top-color: #ffffff;
    border-bottom-color: #aab6c0;
    border-radius: 8px;
}
QFrame#liveToolbar {
    background: #ffffff;
    border: 1px solid #d7dee7;
    border-radius: 8px;
}
QLabel#toolbarLabel, QLabel#metricEyebrow, QLabel#cardKicker {
    color: #98a2b3;
    font-size: 9px;
    font-weight: 600;
    letter-spacing: 1.1px;
}
QFrame#toolbarDivider { background: #d7dee7; border: 0; }
QLabel#muted, QLabel#cardDescription, QLabel#pageSubtitle { color: #7a8699; font-size: 11px; }
QLabel#badPointSummary { color: #98a2b3; font-size: 11px; padding: 12px 4px; }

QFrame#pluginCard {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #ffffff, stop: .68 #fbfcfd, stop: 1 #f2f5f6);
    border: 1px solid #c8d1da;
    border-top-color: #ffffff;
    border-bottom-color: #aab6c0;
    border-radius: 8px;
}
QFrame#pluginCard:hover { border-color: #9db9ed; }
QFrame#cardRule {
    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0,
                                stop: 0 #e8edf1, stop: .45 #9da8b2, stop: 1 #f8fafb);
    border: 0;
}
QLabel#cardTitle { color: #1d2939; font-size: 15px; }
QLabel#chip {
    color: #667085;
    background: #f4f6f8;
    border: 1px solid #e1e7ee;
    border-radius: 4px;
    padding: 3px 7px;
    font-size: 9px;
}
QLabel#emptyState {
    color: #7a8699;
    background: #ffffff;
    border: 1px dashed #c5cfda;
    border-radius: 8px;
    padding: 32px;
}

QFrame#commandBar, QFrame#panel, QFrame#resultPanel {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #ffffff, stop: .68 #fbfcfd, stop: 1 #f2f5f6);
    border: 1px solid #c8d1da;
    border-top-color: #ffffff;
    border-bottom-color: #aab6c0;
    border-radius: 8px;
}
QFrame#serialControlsComponent {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #ffffff, stop: .68 #fbfcfd, stop: 1 #f2f5f6);
    border: 0;
    border-radius: 8px;
}
QFrame#workspaceComponent QFrame#serialControlsComponent,
QFrame#workspaceComponent QFrame#panel { border: 0; }
QFrame#serialSurfaceTools {
    background: transparent;
    border: 0;
}
QFrame#serialViewTools, QFrame#liveViewTools {
    min-height: 30px;
    max-height: 30px;
    background: transparent;
    border: 0;
}
QFrame#workspaceComponent {
    background: #ffffff;
    border: 1px solid #e2e5e9;
    border-radius: 8px;
}
QFrame#workspaceComponent[editing="true"] { border-color: #9db9ed; }
QFrame#workspaceComponent[selected="true"] { border-color: #2f6feb; }
QWidget#componentContentClip,
QWidget#componentContent,
QWidget#metricsPanel { background: transparent; border: 0; }
QFrame#componentTitleBar {
    background: transparent;
    border: 0;
    border-bottom: 1px solid #e2e5e9;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QFrame#componentTitleBar[active="true"] { background: #f5f6f8; border-bottom-color: #cfd4db; }
QFrame#componentTitleBar[selected="true"] { background: #f2f6ff; border-bottom-color: #9db9ed; }
QLabel#componentTitle { color: #1d1f23; font-size: 12px; font-weight: 600; }
QToolButton#componentChromeButton {
    min-width: 24px; max-width: 24px;
    min-height: 24px; max-height: 24px;
    background: transparent;
    border: 0;
    border-radius: 6px;
}
QToolButton#componentChromeButton::menu-indicator { image: none; width: 0; }
QToolButton#componentChromeButton:hover { background: #eaf1ff; }
QMenu {
    background: #ffffff;
    border: 1px solid #c8d1da;
    border-radius: 8px;
    padding: 4px;
}
QMenu::item {
    color: #344054;
    min-height: 28px;
    padding: 4px 18px 4px 10px;
    border-radius: 5px;
}
QMenu::item:selected { color: #1d2939; background: #eaf1ff; }

QFrame#metricPrimary, QFrame#metricRow {
    background: transparent;
    border: 0;
    border-bottom: 1px solid #e8edf2;
}
QLabel#metricGroupLabel {
    color: #949aa5;
    background: transparent;
    border: 0;
    min-height: 28px;
    padding: 8px 4px 2px;
    font-size: 11px;
    font-weight: 600;
}
QScrollArea#workspaceInspectorScroll, QScrollArea#workspaceInspectorScroll > QWidget > QWidget {
    background: transparent;
    border: 0;
}
QLabel#metricPrimaryLabel, QLabel#metricLabel { color: #667085; font-size: 12px; }
QLabel#metricPrimaryValue {
    color: #2e9d71;
    font-family: "Bahnschrift", "Segoe UI Variable Text";
    font-size: 16px;
    font-weight: 600;
}
QLabel#metricValue {
    color: #1d2939;
    font-family: "Bahnschrift", "Segoe UI Variable Text";
    font-size: 13px;
    font-weight: 500;
}
QWidget#workspaceInspectorAdvanced {
    background: transparent;
    border: 0;
    border-top: 1px solid #e2e5e9;
}
QPushButton#workspaceDiagnosticToggle {
    min-height: 30px;
    color: #626873;
    background: transparent;
    border: 0;
    border-radius: 0;
    padding: 0 20px 0 4px;
    text-align: left;
    font-size: 11px;
    font-weight: 500;
}
QPushButton#workspaceDiagnosticToggle:hover { background: #fafafb; }
QWidget#workspaceDiagnosticBody { background: transparent; border: 0; }
QLabel#workspaceDiagnosticDetails {
    color: #475467;
    background: #f7f9fb;
    border: 1px solid #dce3ea;
    border-radius: 3px;
    padding: 8px;
    font-family: "Cascadia Mono", "Consolas", monospace;
    font-size: 11px;
}
QLabel#chartTitle { color: #344054; font-size: 11px; }
QFrame#trendToolbar {
    background: transparent;
    border: 0;
    border-bottom: 1px solid #e2e5e9;
}
QFrame#trendToolbarSeparator { background: #e2e5e9; border: 0; }
QToolButton#trendModeButton,
QToolButton#trendSeriesButton,
QToolButton#trendExportButton,
QToolButton#trendPauseButton {
    min-height: 24px;
    max-height: 24px;
    color: #626873;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 0 7px;
    font-size: 12px;
    font-weight: 500;
}
QToolButton#trendModeButton:hover,
QToolButton#trendSeriesButton:hover,
QToolButton#trendExportButton:hover,
QToolButton#trendPauseButton:hover { background: #f2f3f5; }
QToolButton#trendModeButton:checked {
    color: #1d1f23;
    background: #ffffff;
    border-color: #cfd4db;
}
QToolButton#trendSeriesButton:checked { color: #1d1f23; border-color: #e2e5e9; }
QToolButton#trendPauseButton:checked { color: #a46a00; background: #fff6df; }
QToolButton#trendPauseButton[resumeReady="true"] {
    color: #ffffff; background: #2f6feb; border-color: #2f6feb;
}
QToolButton#trendPauseButton[resumeReady="true"]:hover {
    color: #ffffff; background: #245fcf; border-color: #245fcf;
}
QLabel#statusPill {
    color: #667085;
    background: #f4f6f8;
    border: 1px solid #e1e7ee;
    border-radius: 9px;
    padding: 3px 9px;
    font-size: 10px;
}
QLabel#statusPill[state="connected"] { color: #1c8c62; background: #eaf6f0; border-color: #c9e8d8; }
QLabel#statusPill[state="connecting"] { color: #946200; background: #fff8e8; border-color: #f0dfb0; }
QLabel#statusPill[state="error"] { color: #b54747; background: #fff2f0; border-color: #f2d2ce; }
QLabel#statusPill[state="installed"] { color: #667085; background: #f4f6f8; border-color: #e1e7ee; }

QPushButton {
    min-height: 30px;
    padding: 0 11px;
    color: #475467;
    background: #ffffff;
    border: 1px solid #cbd5df;
    border-radius: 6px;
}
QPushButton:hover { color: #1d2939; background: #f4f6f8; border-color: #9facbd; }
QPushButton:pressed { background: #eaf1ff; border-color: #2f6feb; }
QPushButton:focus { border-color: #2f6feb; }
QPushButton:disabled { color: #a0a8b7; background: #f4f6f8; border-color: #e1e7ee; }
QPushButton[kind="primary"] { color: #ffffff; background: #2f6feb; border-color: #2f6feb; }
QPushButton[kind="primary"]:hover { background: #245fcf; border-color: #245fcf; }
QPushButton[kind="secondary"] { color: #344054; background: #f8fafc; border-color: #b8c3cf; }
QPushButton[kind="secondary"]:hover { color: #1d2939; background: #eef2f6; border-color: #8f9dab; }
QPushButton[kind="quiet"] { color: #667085; background: transparent; border-color: transparent; }
QPushButton[kind="warning"] { color: #b54747; background: #fff6f6; border-color: #e7b9b9; }
QPushButton[layoutEditing="true"] { color: #2f6feb; background: #eaf1ff; border-color: #b9cdf5; }
QPushButton#workspaceAction { min-height: 29px; padding: 0 9px; }

QLineEdit, QComboBox, QSpinBox {
    min-height: 30px;
    padding: 0 8px;
    color: #1d2939;
    background: #ffffff;
    border: 1px solid #cbd5df;
    border-radius: 6px;
    selection-background-color: #dbe8ff;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #2f6feb; }
QComboBox QAbstractItemView {
    color: #344054;
    background: #ffffff;
    border: 1px solid #cbd5df;
    selection-background-color: #eaf1ff;
    selection-color: #2f6feb;
}
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 26px;
    border: 0;
    border-left: 1px solid #e8edf2;
    background: rgba(244,246,248,.7);
}
QComboBox::drop-down:hover { background: #eaf1ff; }
QComboBox::down-arrow {
    image: url(:/serein/chevron-down.svg);
    width: 12px;
    height: 12px;
}
QSpinBox::up-button, QSpinBox::down-button {
    width: 0;
    border: 0;
}
QPushButton::menu-indicator { image: none; width: 0; }
QPlainTextEdit {
    color: #344054;
    background: #fbfcfd;
    border: 1px solid #e1e7ee;
    border-radius: 6px;
    padding: 7px;
    font-family: "Cascadia Mono", "Consolas", monospace;
    font-size: 10px;
    selection-background-color: #dbe8ff;
}
QPlainTextEdit#outputLog {
    background: transparent;
    border: 0;
    border-radius: 0;
    padding: 6px 8px 8px;
}

QFrame#statusStrip {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #ffffff, stop: .68 #fbfcfd, stop: 1 #f2f5f6);
    border: 1px solid #c8d1da;
    border-top-color: #ffffff;
    border-bottom-color: #aab6c0;
    border-radius: 8px;
}
QStatusBar#globalStatusBar[darkMode="true"] {
    background: transparent;
    border: 0;
    color: #9ba6b1;
}
QFrame#statusStrip[darkMode="true"] {
    background: #20262b;
    border-color: #3a454e;
}
QLabel#statusText, QLabel#buildInfo { color: #7a8699; font-size: 10px; }
QMenu { background: #ffffff; color: #344054; border: 1px solid #d7dee7; padding: 4px; }
QMenu::item { min-height: 27px; padding: 0 25px 0 9px; border-radius: 4px; }
QMenu::item:selected { color: #2f6feb; background: #eaf1ff; }
QMenu::separator { height: 1px; background: #e8edf2; margin: 4px 6px; }
QScrollBar:vertical { width: 8px; background: transparent; }
QScrollBar::handle:vertical { background: #c5ccd7; min-height: 26px; border-radius: 4px; }
QScrollBar::handle:vertical:hover { background: #a6b0bf; }
QScrollBar:horizontal { height: 0; min-height: 0; background: transparent; border: 0; }
QScrollBar::handle:horizontal { height: 0; min-height: 0; background: transparent; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0; height: 0; border: 0; background: transparent;
}
QToolTip { color: #f7f9fc; background: #263247; border: 0; border-radius: 4px; padding: 5px 7px; }

/* Sleep mode: the same material language carries across the shell and every panel. */
QMainWindow[darkMode="true"] { background: #171b1f; color: #e6e9ed; }
QWidget[darkMode="true"] { color: #d8dde3; }
QWidget#rootShell[darkMode="true"] {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #252a2f, stop: 1 #171b1f);
    border-color: #3a454e;
}
QFrame#workbenchBar[darkMode="true"] {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #30363c, stop: .48 #272d33, stop: 1 #20252a);
    border-color: #4a535c;
    border-top-color: #68727b;
    border-bottom-color: #15191d;
}
QFrame#workspaceSurface[darkMode="true"] {
    background: #20252a;
    border: 0;
}
QMainWindow#workbenchHost[darkMode="true"] {
    background: #20252a;
    border: 0;
}
QMainWindow#centerWorkbench[darkMode="true"] {
    background: #20252a;
    border: 0;
}
QFrame#pageToolbar[darkMode="true"], QFrame#dockHeader[darkMode="true"],
QFrame#liveToolbar[darkMode="true"] {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #30363c, stop: .52 #272d33, stop: 1 #20252a);
    border-color: #4a535c;
    border-top-color: #68727b;
    border-bottom-color: #15191d;
}
QFrame#surfaceRail[darkMode="true"] {
    background: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,
                                stop: 0 #30353a, stop: .68 #24292d, stop: 1 #1b1f23);
    border-color: rgba(255,255,255,51);
    border-top-color: rgba(255,255,255,75);
    border-bottom-color: rgba(0,0,0,128);
}
QFrame#surfaceRailDivider[darkMode="true"] { background: rgba(255,255,255,.08); }
QFrame#panelRail[darkMode="true"] { background: transparent; }
QFrame#activityRail[darkMode="true"] {
    background: #20252a;
    border: 0;
    border-right: 1px solid #3a454e;
}
QDockWidget[darkMode="true"], QDockWidget[darkMode="true"] > QWidget,
QWidget#dockPanelContent[darkMode="true"],
QWidget#workspaceStack[darkMode="true"], QWidget#workbenchPage[darkMode="true"],
QScrollArea#workbenchScroll[darkMode="true"],
QScrollArea#workbenchScroll[darkMode="true"] > QWidget > QWidget,
QStackedWidget[shellRole="workspace-stack"][darkMode="true"], QWidget[shellRole="workspace-content"][darkMode="true"],
QWidget#workspaceCanvas[darkMode="true"] { background: #20252a; }
QDockWidget#navigationDock[darkMode="true"],
QDockWidget#outputDock[darkMode="true"],
QDockWidget#inspectorDock[darkMode="true"] {
    background: transparent;
    border: 0;
}
QWidget#dockHeader[darkMode="true"] {
    background: transparent;
    border: 0;
    border-bottom: 1px solid #3d474f;
}
QWidget#dockPanelContent[darkMode="true"] {
    background: #252b31;
    border: 1px solid #4a555f;
    border-radius: 8px;
}
QTabBar#hostWorkspaceTabs[darkMode="true"]::tab,
QTabBar#workspaceTabs[darkMode="true"]::tab { color: #aab3bd; background: transparent; }
QTabBar#hostWorkspaceTabs[darkMode="true"]::tab:selected,
QTabBar#workspaceTabs[darkMode="true"]::tab:selected {
    color: #dfe5eb; background: #303840; border-color: #56616c;
}
QLabel#topbarIdentity[darkMode="true"], QLabel#connectionTitle[darkMode="true"],
QLabel#cardTitle[darkMode="true"], QLabel#dockTitle[darkMode="true"] { color: #e6e9ed; }
QLabel#fieldLabel[darkMode="true"] { color: #c0cad3; }
QLabel#muted[darkMode="true"], QLabel#pageSubtitle[darkMode="true"],
QLabel#panelHint[darkMode="true"], QLabel#panelVersion[darkMode="true"],
QLabel#workspaceContextFallback[darkMode="true"],
QLabel#statusText[darkMode="true"], QLabel#buildInfo[darkMode="true"] { color: #9ba6b1; }
QLabel#badPointSummary[darkMode="true"] { color: #9ba6b1; }
QLabel#chip[darkMode="true"] {
    color: #aab5bf;
    background: #303840;
    border-color: #46535e;
}
QLabel#statusPill[darkMode="true"], QLabel#sessionState[darkMode="true"] {
    color: #aab5bf;
    background: #303840;
    border-color: #46535e;
}
QLabel#statusPill[state="connected"][darkMode="true"],
QLabel#sessionState[state="connected"][darkMode="true"] {
    color: #8bd5b1;
    background: #1d3b31;
    border-color: #356b56;
}
QLabel#emptyState[darkMode="true"] {
    color: #aab5bf;
    background: #252b31;
    border-color: #4a555f;
}
QLabel#workspaceDiagnosticDetails[darkMode="true"] {
    color: #d8dde3;
    background: #181e24;
    border-color: #3d474f;
}
QLabel#metricPrimaryLabel[darkMode="true"], QLabel#metricLabel[darkMode="true"] {
    color: #aab5bf;
}
QLabel#metricGroupLabel[darkMode="true"] { color: #8f9ba6; }
QLabel#metricPrimaryValue[darkMode="true"] { color: #65d6a3; }
QLabel#metricValue[darkMode="true"] { color: #e6e9ed; }
QFrame#trendToolbar[darkMode="true"] { background: transparent; border-bottom-color: #3d474f; }
QFrame#trendToolbarSeparator[darkMode="true"] { background: #3d474f; }
QToolButton#trendModeButton[darkMode="true"],
QToolButton#trendSeriesButton[darkMode="true"],
QToolButton#trendExportButton[darkMode="true"],
QToolButton#trendPauseButton[darkMode="true"] { color: #aab5bf; background: transparent; }
QToolButton#trendModeButton:hover[darkMode="true"],
QToolButton#trendSeriesButton:hover[darkMode="true"],
QToolButton#trendExportButton:hover[darkMode="true"],
QToolButton#trendPauseButton:hover[darkMode="true"] { background: #303840; }
QToolButton#trendModeButton:checked[darkMode="true"] {
    color: #e6e9ed; background: #303840; border-color: #56616c;
}
QToolButton#trendSeriesButton:checked[darkMode="true"] {
    color: #e6e9ed; border-color: #56616c;
}
QToolButton#trendPauseButton:checked[darkMode="true"] { color: #d7a85b; background: #40351f; }
QToolButton#trendPauseButton[resumeReady="true"][darkMode="true"] {
    color: #ffffff; background: #2f6feb; border-color: #2f6feb;
}
QToolButton#trendPauseButton[resumeReady="true"]:hover[darkMode="true"] {
    color: #ffffff; background: #3b7bf0; border-color: #3b7bf0;
}
QPushButton#workspaceDiagnosticToggle[darkMode="true"] { color: #b8c2cb; background: transparent; }
QPushButton#workspaceDiagnosticToggle:hover[darkMode="true"] { background: #303840; }
QWidget#workspaceInspectorAdvanced[darkMode="true"] { border-top-color: #3d474f; }
QPushButton[darkMode="true"], QLineEdit[darkMode="true"],
QComboBox[darkMode="true"], QSpinBox[darkMode="true"] {
    color: #e6e9ed; background: #252b31; border-color: #4a555f;
}
QComboBox::drop-down[darkMode="true"] { background: rgba(255,255,255,.04); border-left-color: #3d474f; }
QComboBox::drop-down:hover[darkMode="true"] { background: rgba(255,255,255,.08); }
QPushButton[darkMode="true"]:hover { background: #303840; border-color: #71808c; }
QPushButton[kind="primary"][darkMode="true"] { color: #ffffff; background: #2f6feb; border-color: #2f6feb; }
QPushButton[kind="secondary"][darkMode="true"] { color: #d8dde3; background: #293038; border-color: #56616c; }
QWidget#workspaceAdvancedSection[darkMode="true"] { background: #252b31; border-color: #4a555f; }
QPushButton#workspaceAdvancedToggle[darkMode="true"] { color: #d8dde3; }
QPushButton#workspaceAdvancedToggle:hover[darkMode="true"] { background: #303840; }
QLabel#advancedSectionIndicator[darkMode="true"] { color: #aab5bf; }
QWidget#workspaceAdvancedBody[darkMode="true"] { border-top-color: #3d474f; }
QPlainTextEdit[darkMode="true"] { color: #d8dde3; background: #181c20; border-color: #3d474f; }
QFrame#functionTabsComponent[darkMode="true"], QFrame#commandBar[darkMode="true"],
QFrame#panel[darkMode="true"], QFrame#resultPanel[darkMode="true"],
QFrame#serialControlsComponent[darkMode="true"], QFrame#pluginCard[darkMode="true"],
QFrame#workspaceComponent[darkMode="true"] {
    background: #252b31;
    border-color: #4a555f;
    border-radius: 8px;
}
QFrame#cardRule[darkMode="true"] {
    background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 0,
                                stop: 0 #2a3238, stop: .45 #7a8792, stop: 1 #313a41);
}
QWidget#componentContentClip[darkMode="true"],
QWidget#componentContent[darkMode="true"],
QWidget#metricsPanel[darkMode="true"] { background: transparent; }
QFrame#componentTitleBar[darkMode="true"] {
    background: transparent;
    border: 0;
    border-bottom: 1px solid #3d474f;
}
QFrame#componentTitleBar[active="true"][darkMode="true"] {
    background: rgba(48,56,64,.34);
    border-bottom-color: #56636d;
}
QFrame#componentTitleBar[selected="true"][darkMode="true"] {
    background: rgba(45,67,86,.72);
    border-bottom-color: #5a9de0;
}
QLabel#componentTitle[darkMode="true"] { color: #dbe4eb; }
QToolButton#componentChromeButton[darkMode="true"] { color: #c8d2db; }
QToolButton#componentChromeButton:hover[darkMode="true"] { background: #3a4853; }
QMenu[darkMode="true"] {
    background: #252b31;
    border-color: #4a555f;
}
QMenu::item[darkMode="true"] { color: #d8dde3; }
QMenu::item:selected[darkMode="true"] { color: #eaf1ff; background: #303f4d; }
)QSS"));
}

} // namespace serein::scope::app
