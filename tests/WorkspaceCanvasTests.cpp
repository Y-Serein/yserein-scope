#include "WorkspaceCanvas.h"

#include <QtCore/QMimeData>
#include <QtCore/QCoreApplication>
#include <QtCore/QSettings>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDropEvent>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QToolButton>

using namespace serein::scope::tn160;

class WorkspaceCanvasTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void defaultTemplateIsLockedAndComponentsAreIndependent();
    void componentCanMoveMaximizeHideAndReset();
    void componentChromeMenuControlsFullscreenAndClose();
    void componentDropRevealsHiddenInstance();
    void componentDropPlacesAndReusesInstance();
    void componentResizesFromAllEightDirections();
    void layoutPersistsAcrossCanvasRecreation();
};

void WorkspaceCanvasTests::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("SereinTests"));
    QCoreApplication::setApplicationName(QStringLiteral("WorkspaceCanvasTests"));
    QSettings().clear();
}

void WorkspaceCanvasTests::cleanup()
{
    QSettings settings;
    settings.clear();
    settings.sync();
}

void WorkspaceCanvasTests::defaultTemplateIsLockedAndComponentsAreIndependent()
{
    WorkspaceCanvas canvas(QStringLiteral("tests/default"));
    canvas.resize(1000, 700);
    ComponentFrame *thermal = canvas.addComponent(
        QStringLiteral("thermal"), QStringLiteral("Thermal"), new QLabel(QStringLiteral("T")),
        QRectF(0.01, 0.01, 0.68, 0.60), QSize(300, 220));
    ComponentFrame *metrics = canvas.addComponent(
        QStringLiteral("metrics"), QStringLiteral("Metrics"), new QLabel(QStringLiteral("M")),
        QRectF(0.70, 0.01, 0.29, 0.60), QSize(200, 220));
    ComponentFrame *trend = canvas.addComponent(
        QStringLiteral("trend"), QStringLiteral("Trend"), new QLabel(QStringLiteral("R")),
        QRectF(0.01, 0.62, 0.98, 0.37), QSize(400, 160));
    canvas.show();
    QCoreApplication::processEvents();

    QVERIFY(canvas.editLocked());
    QVERIFY(thermal->editLocked());
    QVERIFY(metrics->editLocked());
    QVERIFY(trend->editLocked());
    QCOMPARE(canvas.componentIds().size(), 3);
    QVERIFY(thermal->geometry() != metrics->geometry());
    QVERIFY(trend->geometry().top() > thermal->geometry().top());

    const QRect lockedGeometry = thermal->geometry();
    auto *titleBar = thermal->findChild<QFrame *>(QStringLiteral("componentTitleBar"));
    QVERIFY(titleBar);
    QTest::mousePress(titleBar, Qt::LeftButton, Qt::NoModifier, QPoint(30, 14));
    QTest::mouseMove(titleBar, QPoint(78, 46), 20);
    QTest::mouseRelease(titleBar, Qt::LeftButton, Qt::NoModifier, QPoint(78, 46));
    QCOMPARE(thermal->geometry(), lockedGeometry);

    canvas.setEditLocked(false);
    QVERIFY(!thermal->editLocked());
    QVERIFY(!metrics->editLocked());
    QVERIFY(!trend->editLocked());
}

void WorkspaceCanvasTests::componentCanMoveMaximizeHideAndReset()
{
    WorkspaceCanvas canvas(QStringLiteral("tests/interaction"));
    canvas.resize(900, 600);
    ComponentFrame *first = canvas.addComponent(
        QStringLiteral("first"), QStringLiteral("First"), new QLabel(QStringLiteral("A")),
        QRectF(0.02, 0.02, 0.58, 0.58), QSize(220, 160));
    ComponentFrame *second = canvas.addComponent(
        QStringLiteral("second"), QStringLiteral("Second"), new QLabel(QStringLiteral("B")),
        QRectF(0.62, 0.02, 0.36, 0.58), QSize(180, 160));
    canvas.show();
    QCoreApplication::processEvents();
    const QRect original = first->geometry();

    canvas.setEditLocked(false);
    auto *titleBar = first->findChild<QFrame *>(QStringLiteral("componentTitleBar"));
    QVERIFY(titleBar);
    QTest::mousePress(titleBar, Qt::LeftButton, Qt::NoModifier, QPoint(30, 14));
    QTest::mouseMove(titleBar, QPoint(78, 46), 20);
    QTest::mouseRelease(titleBar, Qt::LeftButton, Qt::NoModifier, QPoint(78, 46));
    QCoreApplication::processEvents();
    QVERIFY(first->geometry().topLeft() != original.topLeft());

    const QRect moved = first->geometry();
    canvas.toggleComponentMaximized(QStringLiteral("first"));
    QVERIFY(canvas.isComponentMaximized(QStringLiteral("first")));
    QCOMPARE(first->geometry(), canvas.rect());
    QVERIFY(!second->isVisible());
    QVERIFY(first->width() > moved.width());
    QTest::keyClick(&canvas, Qt::Key_Escape);
    QCOMPARE(first->geometry(), moved);
    QVERIFY(!canvas.isComponentMaximized(QStringLiteral("first")));
    QVERIFY(second->isVisible());

    canvas.setComponentVisible(QStringLiteral("second"), false);
    QVERIFY(!canvas.isComponentVisible(QStringLiteral("second")));
    canvas.resetLayout();
    QVERIFY(canvas.editLocked());
    QVERIFY(canvas.isComponentVisible(QStringLiteral("second")));
    QVERIFY(second->isVisible());
}

void WorkspaceCanvasTests::componentChromeMenuControlsFullscreenAndClose()
{
    WorkspaceCanvas canvas(QStringLiteral("tests/chrome-menu"));
    canvas.resize(900, 600);
    ComponentFrame *frame = canvas.addComponent(
        QStringLiteral("waveform"), QStringLiteral("实时波形"),
        new QLabel(QStringLiteral("W")), QRectF(0.02, 0.02, 0.58, 0.58),
        QSize(220, 160));
    canvas.show();
    QCoreApplication::processEvents();

    auto *menuButton = frame->findChild<QToolButton *>(
        QStringLiteral("componentChromeButton"));
    QVERIFY(menuButton);
    QVERIFY(menuButton->menu());
    const QList<QAction *> actions = menuButton->menu()->actions();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions.at(0)->text(), QStringLiteral("组件全屏"));
    QCOMPARE(actions.at(1)->text(), QStringLiteral("关闭组件"));

    actions.at(0)->trigger();
    QVERIFY(canvas.isComponentMaximized(QStringLiteral("waveform")));
    QCOMPARE(actions.at(0)->text(), QStringLiteral("恢复组件"));

    actions.at(0)->trigger();
    QVERIFY(!canvas.isComponentMaximized(QStringLiteral("waveform")));
    actions.at(1)->trigger();
    QVERIFY(!canvas.isComponentVisible(QStringLiteral("waveform")));
}

void WorkspaceCanvasTests::componentDropRevealsHiddenInstance()
{
    WorkspaceCanvas canvas(QStringLiteral("tests/component-drop"));
    canvas.resize(900, 600);
    canvas.addComponent(QStringLiteral("terminal"), QStringLiteral("串口终端"),
                        new QLabel(QStringLiteral("T")),
                        QRectF(0.02, 0.02, 0.58, 0.58), QSize(220, 160));
    canvas.show();
    QCoreApplication::processEvents();

    canvas.setComponentVisible(QStringLiteral("terminal"), false);
    QVERIFY(!canvas.isComponentVisible(QStringLiteral("terminal")));

    QMimeData mime;
    mime.setData(QStringLiteral("application/x-serein-component"),
                 QByteArrayLiteral("terminal"));
    QDragEnterEvent enter(QPoint(300, 240), Qt::CopyAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(300, 240), Qt::CopyAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &drop);

    QVERIFY(drop.isAccepted());
    QVERIFY(canvas.isComponentVisible(QStringLiteral("terminal")));
}

void WorkspaceCanvasTests::componentDropPlacesAndReusesInstance()
{
    WorkspaceCanvas canvas(QStringLiteral("tests/component-placement"));
    canvas.resize(900, 600);
    ComponentFrame *frame = canvas.addComponent(
        QStringLiteral("waveform"), QStringLiteral("实时波形"), new QLabel(QStringLiteral("W")),
        QRectF(0.02, 0.02, 0.42, 0.40), QSize(220, 160));
    canvas.show();
    QCoreApplication::processEvents();
    canvas.setComponentVisible(QStringLiteral("waveform"), false);
    const QPoint dropPoint(620, 390);

    QVERIFY(canvas.addOrShowComponentAt(QStringLiteral("waveform"), dropPoint));
    QVERIFY(canvas.isComponentVisible(QStringLiteral("waveform")));
    QCOMPARE(frame->geometry().center(), dropPoint);

    const QRect before = frame->geometry();
    QVERIFY(canvas.addOrShowComponentAt(QStringLiteral("waveform"), QPoint(-1, -1)));
    QCOMPARE(frame->geometry(), before);
    QVERIFY(!canvas.addOrShowComponentAt(QStringLiteral("missing"), dropPoint));
}

void WorkspaceCanvasTests::componentResizesFromAllEightDirections()
{
    WorkspaceCanvas canvas(QStringLiteral("tests/resize"));
    canvas.resize(960, 640);
    ComponentFrame *frame = canvas.addComponent(
        QStringLiteral("resizable"), QStringLiteral("Resizable"),
        new QLabel(QStringLiteral("content")), QRectF(0.25, 0.25, 1.0 / 3.0, 0.375),
        QSize(160, 120));
    canvas.show();
    QCoreApplication::processEvents();
    canvas.setEditLocked(false);

    const QRect initial(240, 160, 320, 240);
    struct ResizeCase {
        QPoint press;
        QPoint delta;
        QRect expected;
    };
    const QList<ResizeCase> cases{
        {{1, 120}, {-16, 0}, {224, 160, 336, 240}},
        {{160, 1}, {0, -16}, {240, 144, 320, 256}},
        {{318, 120}, {16, 0}, {240, 160, 336, 240}},
        {{160, 238}, {0, 16}, {240, 160, 320, 256}},
        {{1, 1}, {-16, -16}, {224, 144, 336, 256}},
        {{318, 1}, {16, -16}, {240, 144, 336, 256}},
        {{1, 238}, {-16, 16}, {224, 160, 336, 256}},
        {{318, 238}, {16, 16}, {240, 160, 336, 256}},
    };

    for (const ResizeCase &test : cases) {
        frame->setGeometry(initial);
        QTest::mousePress(frame, Qt::LeftButton, Qt::NoModifier, test.press);
        QTest::mouseMove(frame, test.press + test.delta, 20);
        QTest::mouseRelease(frame, Qt::LeftButton, Qt::NoModifier,
                            test.press + test.delta);
        QCoreApplication::processEvents();
        QCOMPARE(frame->geometry(), test.expected);
    }
}

void WorkspaceCanvasTests::layoutPersistsAcrossCanvasRecreation()
{
    const QString settingsGroup = QStringLiteral("tests/persistence");
    QRect persisted;
    {
        WorkspaceCanvas canvas(settingsGroup);
        canvas.resize(1000, 700);
        ComponentFrame *frame = canvas.addComponent(
            QStringLiteral("thermal"), QStringLiteral("Thermal"), new QLabel(QStringLiteral("T")),
            QRectF(0.01, 0.01, 0.68, 0.60), QSize(240, 180));
        canvas.addComponent(
            QStringLiteral("metrics"), QStringLiteral("Metrics"),
            new QLabel(QStringLiteral("M")), QRectF(0.70, 0.01, 0.29, 0.60),
            QSize(180, 180));
        canvas.show();
        QCoreApplication::processEvents();
        canvas.setEditLocked(false);
        frame->setGeometry(QRect(160, 96, 448, 304));
        QVERIFY(QMetaObject::invokeMethod(frame, "geometryCommitted", Qt::DirectConnection,
                                          Q_ARG(ComponentFrame *, frame)));
        canvas.setComponentVisible(QStringLiteral("metrics"), false);
        persisted = frame->geometry();
    }

    WorkspaceCanvas restored(settingsGroup);
    restored.resize(1000, 700);
    ComponentFrame *frame = restored.addComponent(
        QStringLiteral("thermal"), QStringLiteral("Thermal"), new QLabel(QStringLiteral("T")),
        QRectF(0.01, 0.01, 0.68, 0.60), QSize(240, 180));
    restored.addComponent(
        QStringLiteral("metrics"), QStringLiteral("Metrics"),
        new QLabel(QStringLiteral("M")), QRectF(0.70, 0.01, 0.29, 0.60),
        QSize(180, 180));
    restored.show();
    QCoreApplication::processEvents();
    QCOMPARE(frame->geometry(), persisted);
    QVERIFY(!restored.editLocked());
    QVERIFY(!restored.isComponentVisible(QStringLiteral("metrics")));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    WorkspaceCanvasTests test;
    return QTest::qExec(&test, argc, argv);
}

#include "WorkspaceCanvasTests.moc"
