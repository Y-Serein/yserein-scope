#include "MainWindow.h"
#include "QuickWorkbenchController.h"
#include "Theme.h"

#include <serein/scope/BackendRuntime.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuick/QQuickWindow>
#include <QtQuickControls2/QQuickStyle>
#include <QtWidgets/QApplication>

#include <memory>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Serein Scope"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.1"));
    QCoreApplication::setOrganizationName(QStringLiteral("Serein"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    serein::scope::app::applyTheme(application);

    QString screenshotPath;
    QString componentCycleId;
    bool qmlUi = false;
    for (const QString &argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--screenshot="))) {
            screenshotPath = argument.sliced(QStringLiteral("--screenshot=").size());
        } else if (argument.startsWith(QStringLiteral("--component-cycle-test="))) {
            componentCycleId = argument.sliced(QStringLiteral("--component-cycle-test=").size());
        } else if (argument == QStringLiteral("--qml")
                   || argument == QStringLiteral("--ui=qml")
                   || argument == QStringLiteral("--qml-smoke-test")) {
            qmlUi = true;
        }
    }
    const bool smokeTest = QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"));
    const bool qmlSmokeTest = QCoreApplication::arguments().contains(
        QStringLiteral("--qml-smoke-test"));
    const bool qaLayoutMode = QCoreApplication::arguments().contains(
                                  QStringLiteral("--qa-start-maximized"))
        || QCoreApplication::arguments().contains(QStringLiteral("--qa-show-panels"));
    const bool nonInteractiveTest = smokeTest || qmlSmokeTest || !componentCycleId.isEmpty();
    const bool isolateSettings = nonInteractiveTest || !screenshotPath.isEmpty()
        || qaLayoutMode
        || QCoreApplication::arguments().contains(
            QStringLiteral("--qa-isolate-settings"));

    std::unique_ptr<QTemporaryDir> testSettingsDirectory;
    if (isolateSettings) {
        testSettingsDirectory = std::make_unique<QTemporaryDir>();
        if (!testSettingsDirectory->isValid()) {
            qCritical() << "无法创建测试设置隔离目录";
            return 3;
        }
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           testSettingsDirectory->path());
    }

    auto &backendRuntime = serein::scope::BackendRuntime::instance();
    QObject::connect(&backendRuntime, &serein::scope::BackendRuntime::runtimeError,
                     &application, [](const QString &message) {
        qCritical().noquote() << "Backend Runtime:" << message;
    });
    if (!backendRuntime.start(
            serein::scope::BackendRuntime::configuredLimits())) {
        qCritical() << "Serein Scope backend runtime failed to start";
        return 5;
    }
    struct BackendStopGuard final {
        serein::scope::BackendRuntime &runtime;
        ~BackendStopGuard() { runtime.stop(); }
    } backendStopGuard{backendRuntime};

    if (qmlUi) {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        auto controller = std::make_unique<serein::scope::app::QuickWorkbenchController>();
        if (QCoreApplication::arguments().contains(QStringLiteral("--dark"))) {
            controller->setDarkMode(true);
        }
        QQmlApplicationEngine engine;
        engine.setInitialProperties({
            {QStringLiteral("workbench"), QVariant::fromValue(controller.get())},
            {QStringLiteral("startVisible"), !nonInteractiveTest},
        });
        engine.loadFromModule(QStringLiteral("Serein.Scope"), QStringLiteral("Main"));
        if (engine.rootObjects().isEmpty()) {
            qCritical() << "QML workbench failed to load";
            return 4;
        }
        auto *quickWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (!quickWindow) {
            qCritical() << "QML root object is not a QQuickWindow";
            return 4;
        }
        if (!componentCycleId.isEmpty()) {
            QTimer::singleShot(250, &application,
                               [&application, controllerPtr = controller.get(),
                                componentCycleId] {
                const bool opened = controllerPtr->openComponent(componentCycleId);
                const bool closed = opened && controllerPtr->closeWorkspace(componentCycleId);
                const bool unloaded = closed && controllerPtr->unloadComponent(componentCycleId);
                application.exit(opened && closed && unloaded ? 0 : 3);
            });
        } else if (!screenshotPath.isEmpty()) {
            QTimer::singleShot(700, &application,
                               [&application, quickWindow, screenshotPath] {
                const bool saved = quickWindow->grabWindow().save(screenshotPath);
                application.exit(saved ? 0 : 4);
            });
        } else if (qmlSmokeTest || smokeTest) {
            QTimer::singleShot(250, &application, &QCoreApplication::quit);
        }
        return application.exec();
    }

    auto window = std::make_unique<serein::scope::app::MainWindow>();
    serein::scope::app::MainWindow *nativeWindow = window.get();
    if (!nonInteractiveTest) {
        if (QCoreApplication::arguments().contains(
                QStringLiteral("--qa-start-maximized"))) {
            window->showMaximized();
        } else {
            window->show();
        }
    }
    if (!componentCycleId.isEmpty()) {
        QTimer::singleShot(250, &application, [&application, nativeWindow, componentCycleId] {
            if (!nativeWindow) {
                qCritical() << "--component-cycle-test requires the native UI";
                application.exit(3);
                return;
            }
            QString error;
            const bool passed = nativeWindow->runComponentCycleTest(componentCycleId, &error);
            if (!passed) qCritical().noquote() << error;
            application.exit(passed ? 0 : 3);
        });
    } else if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(500, &application,
                           [&application, windowPtr = window.get(), screenshotPath] {
            windowPtr->grab().save(screenshotPath);
            application.quit();
        });
    } else if (smokeTest) {
        QTimer::singleShot(250, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
