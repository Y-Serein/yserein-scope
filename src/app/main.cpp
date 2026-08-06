#include "MainWindow.h"
#include "Theme.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
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
    for (const QString &argument : QCoreApplication::arguments()) {
        if (argument.startsWith(QStringLiteral("--screenshot="))) {
            screenshotPath = argument.sliced(QStringLiteral("--screenshot=").size());
        } else if (argument.startsWith(QStringLiteral("--component-cycle-test="))) {
            componentCycleId = argument.sliced(QStringLiteral("--component-cycle-test=").size());
        }
    }
    const bool smokeTest = QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"));
    const bool nonInteractiveTest = smokeTest || !componentCycleId.isEmpty();

    std::unique_ptr<QTemporaryDir> testSettingsDirectory;
    if (nonInteractiveTest) {
        testSettingsDirectory = std::make_unique<QTemporaryDir>();
        if (!testSettingsDirectory->isValid()) {
            qCritical() << "无法创建测试设置隔离目录";
            return 3;
        }
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           testSettingsDirectory->path());
    }

    auto window = std::make_unique<serein::scope::app::MainWindow>();
    serein::scope::app::MainWindow *nativeWindow = window.get();
    if (!nonInteractiveTest) window->show();
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
