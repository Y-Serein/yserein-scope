#include "MainWindow.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <QtCore/QSettings>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QLayout>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QFrame>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QToolButton>

using namespace serein::scope::app;

class WorkbenchLayoutTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void optionalPanelsStartHiddenAndToggleIndependently();
    void optionalPanelsDoNotOverlapCentralSurface();
    void centralHostUsesSharedShellInset();
    void userLayoutIsRestoredAndDefaultLayoutIsClean();
    void sleepTogglesWholeShell();
    void connectionControlsUseStableIdsAndManualPortFallback();
};

void WorkbenchLayoutTests::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("SereinTests"));
    QCoreApplication::setApplicationName(QStringLiteral("WorkbenchLayoutTests"));
    QSettings().clear();
}

void WorkbenchLayoutTests::cleanup()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}

void WorkbenchLayoutTests::optionalPanelsStartHiddenAndToggleIndependently()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto *navigation = window.findChild<QDockWidget *>(QStringLiteral("navigationDock"));
    auto *inspector = window.findChild<QDockWidget *>(QStringLiteral("inspectorDock"));
    auto *output = window.findChild<QDockWidget *>(QStringLiteral("outputDock"));
    QVERIFY(navigation);
    QVERIFY(inspector);
    QVERIFY(output);
    auto *root = window.findChild<QWidget *>(QStringLiteral("rootShell"));
    auto *rail = window.findChild<QFrame *>(QStringLiteral("activityRail"));
    auto *workbench = window.findChild<QMainWindow *>(QStringLiteral("workbenchHost"));
    QVERIFY(root);
    QVERIFY(rail);
    QVERIFY(workbench);
    auto *workspaceTabs = window.findChild<QTabBar *>(QStringLiteral("hostWorkspaceTabs"));
    QVERIFY(workspaceTabs);
    QVERIFY(workspaceTabs->count() >= 1);
    QCOMPARE(workspaceTabs->tabData(0).toString(), QStringLiteral("__home__"));
    QCOMPARE(workspaceTabs->tabText(0), QStringLiteral("Scope"));
    qInfo() << "shell geometry" << root->geometry()
            << "rail geometry" << rail->geometry()
            << "workbench geometry" << workbench->geometry();
    QVERIFY(workbench->width() > 500);

    QVERIFY(navigation->isVisible());
    QCOMPARE(workbench->dockWidgetArea(navigation), Qt::RightDockWidgetArea);
    QCOMPARE(workbench->dockWidgetArea(inspector), Qt::LeftDockWidgetArea);
    for (QDockWidget *dock : {inspector, output}) {
        QVERIFY(!dock->isVisible());
        QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);
        QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetClosable));
        QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
        QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
    }

    navigation->toggleViewAction()->trigger();
    QCoreApplication::processEvents();
    QVERIFY(!navigation->isVisible());
    QVERIFY(!inspector->isVisible());
    QVERIFY(!output->isVisible());

    navigation->toggleViewAction()->trigger();
    QCoreApplication::processEvents();
    QVERIFY(navigation->isVisible());
    navigation->setFloating(true);
    QCoreApplication::processEvents();
    QVERIFY(navigation->isFloating());
}

void WorkbenchLayoutTests::optionalPanelsDoNotOverlapCentralSurface()
{
    MainWindow window;
    window.resize(1280, 820);
    window.show();
    QCoreApplication::processEvents();

    auto *inspector = window.findChild<QDockWidget *>(QStringLiteral("inspectorDock"));
    auto *output = window.findChild<QDockWidget *>(QStringLiteral("outputDock"));
    auto *surface = window.findChild<QStackedWidget *>(QStringLiteral("workspaceStack"));
    QVERIFY(inspector);
    QVERIFY(output);
    QVERIFY(surface);

    inspector->toggleViewAction()->trigger();
    output->toggleViewAction()->trigger();
    QCoreApplication::processEvents();
    QVERIFY(inspector->isVisible());
    QVERIFY(output->isVisible());

    const auto windowRect = [&window](QWidget *widget) {
        return QRect(widget->mapTo(&window, QPoint(0, 0)), widget->size());
    };
    QVERIFY(!windowRect(inspector).intersects(windowRect(output)));
    QVERIFY(!windowRect(inspector).intersects(windowRect(surface)));
    QVERIFY(!windowRect(output).intersects(windowRect(surface)));
}

void WorkbenchLayoutTests::centralHostUsesSharedShellInset()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto *host = window.findChild<QMainWindow *>(QStringLiteral("workbenchHost"));
    auto *center = window.findChild<QMainWindow *>(QStringLiteral("centerWorkbench"));
    auto *surface = window.findChild<QFrame *>(QStringLiteral("workspaceSurface"));
    QVERIFY(host);
    QVERIFY(center);
    QVERIFY(surface);
    QCOMPARE(host->contentsMargins(), QMargins(6, 6, 6, 6));
    QCOMPARE(center->contentsMargins(), QMargins(6, 6, 6, 6));
    QVERIFY(surface->layout());
    QCOMPARE(surface->layout()->contentsMargins(), QMargins(0, 0, 0, 0));
}

void WorkbenchLayoutTests::userLayoutIsRestoredAndDefaultLayoutIsClean()
{
    {
        MainWindow window;
        window.show();
        QCoreApplication::processEvents();
        auto *output = window.findChild<QDockWidget *>(QStringLiteral("outputDock"));
        QVERIFY(output);
        output->toggleViewAction()->trigger();
        QCoreApplication::processEvents();
        QVERIFY(output->isVisible());
    }

    {
        MainWindow window;
        window.show();
        QCoreApplication::processEvents();
        auto *navigation = window.findChild<QDockWidget *>(QStringLiteral("navigationDock"));
        auto *inspector = window.findChild<QDockWidget *>(QStringLiteral("inspectorDock"));
        auto *output = window.findChild<QDockWidget *>(QStringLiteral("outputDock"));
        QVERIFY(navigation);
        QVERIFY(inspector);
        QVERIFY(output);
        QVERIFY(output->isVisible());

        QVERIFY(QMetaObject::invokeMethod(&window, "resetDockLayout", Qt::DirectConnection));
        QCoreApplication::processEvents();
        QVERIFY(navigation->isVisible());
        QVERIFY(!inspector->isVisible());
        QVERIFY(!output->isVisible());
    }
}

void WorkbenchLayoutTests::sleepTogglesWholeShell()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    auto *shell = window.findChild<QWidget *>(QStringLiteral("rootShell"));
    auto *sleep = window.findChild<QToolButton *>(QStringLiteral("sleepButton"));
    QVERIFY(shell);
    QVERIFY(sleep);
    sleep->click();
    QCOMPARE(shell->property("darkMode").toBool(), true);
    QVERIFY(sleep->isChecked());
    sleep->click();
    QCOMPARE(shell->property("darkMode").toBool(), false);
    QVERIFY(!sleep->isChecked());
}

void WorkbenchLayoutTests::connectionControlsUseStableIdsAndManualPortFallback()
{
    MainWindow window;
    auto *port = window.findChild<QComboBox *>(QStringLiteral("connectionPort"));
    auto *engine = window.findChild<QComboBox *>(QStringLiteral("connectionEngine"));
    QVERIFY(port);
    QVERIFY(engine);
    QVERIFY(port->isEditable());
    QCOMPARE(engine->count(), 2);
    QCOMPARE(engine->itemData(0).toString(), QStringLiteral("delimited"));
    QCOMPARE(engine->itemData(1).toString(), QStringLiteral("raw"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    WorkbenchLayoutTests test;
    return QTest::qExec(&test, argc, argv);
}

#include "WorkbenchLayoutTests.moc"
