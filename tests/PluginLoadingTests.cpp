#include "ComponentManager.h"

#include <serein/scope/PluginInterface.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QLibrary>
#include <QtCore/QPluginLoader>
#include <QtCore/QTemporaryDir>
#include <QtCore/QMetaObject>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QWidget>

#include <memory>

using namespace serein::scope;
using namespace serein::scope::app;

class TestHost final : public IHostServices {
public:
    void log(const QString &, const QString &) override {}
    void setStatus(const QString &) override {}
};

class PluginLoadingTests final : public QObject {
    Q_OBJECT

private slots:
    void componentPackagesSupportFullRuntimeLifecycle();
    void managerImportsLoadsUnloadsAndRemovesComponents();
    void repeatedWorkspaceLifecycleAndSerialFailurePaths();
};

void PluginLoadingTests::componentPackagesSupportFullRuntimeLifecycle()
{
    const QDir directory(qEnvironmentVariable("SEREIN_TEST_COMPONENT_DIR"));
    QVERIFY2(directory.exists(), qPrintable(directory.absolutePath()));
    QSet<QString> ids;
    TestHost host;
    for (const QFileInfo &file : directory.entryInfoList(
             {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name)) {
        QPluginLoader loader;
        loader.setLoadHints(QLibrary::LoadHints{});
        loader.setFileName(file.absoluteFilePath());
        QObject *instance = loader.instance();
        QVERIFY2(instance, qPrintable(loader.errorString()));
        auto *plugin = qobject_cast<IDevicePlugin *>(instance);
        QVERIFY2(plugin, qPrintable(file.fileName()));
        QCOMPARE(plugin->metadata().interfaceVersion, PluginInterfaceVersion);
        QVERIFY(plugin->initialize(&host));
        QString componentId = plugin->metadata().id;
        componentId.detach();
        ids.insert(componentId);
        QWidget *workspace = plugin->createWorkspace(nullptr);
        QVERIFY(workspace);
        workspace->show();
        QCoreApplication::processEvents();
        if (componentId == QStringLiteral("tn160-thermal")) {
            auto *calibration = workspace->findChild<QWidget *>(
                QStringLiteral("tn160CalibrationPage"));
            QVERIFY(calibration);
            QVERIFY(!calibration->findChild<QWidget *>(QStringLiteral("commandBar")));
            QVERIFY(calibration->findChild<QWidget *>(QStringLiteral("tn160CalibrationPort")));
            QVERIFY(!calibration->findChild<QWidget *>(QStringLiteral("muted")));
            auto *port = workspace->findChild<QComboBox *>(QStringLiteral("tn160Port"));
            QVERIFY(port);
            QVERIFY(QMetaObject::invokeMethod(workspace->findChild<QObject *>(
                        QStringLiteral("tn160LivePage")), "setPort", Qt::DirectConnection,
                                              Q_ARG(QString, QStringLiteral("COM28"))));
            QCOMPARE(port->currentText(), QStringLiteral("COM28"));
        } else if (componentId == QStringLiteral("serial-scope")) {
            QObject *canvas = workspace->findChild<QObject *>(
                QStringLiteral("workspaceCanvas"));
            QVERIFY(canvas);
            bool added = false;
            QVERIFY(QMetaObject::invokeMethod(
                canvas, "addOrShowComponentAt", Qt::DirectConnection,
                Q_RETURN_ARG(bool, added), Q_ARG(QString, QStringLiteral("terminal")),
                Q_ARG(QPoint, QPoint(260, 180))));
            QVERIFY(added);
            auto *terminal = workspace->findChild<QPlainTextEdit *>(
                QStringLiteral("serialTerminalOutput"));
            QVERIFY(terminal);
            auto *port = workspace->findChild<QComboBox *>(QStringLiteral("serialPort"));
            auto *engine = workspace->findChild<QComboBox *>(QStringLiteral("serialEngine"));
            QVERIFY(port);
            QVERIFY(engine);
            QVERIFY(QMetaObject::invokeMethod(workspace, "setPort", Qt::DirectConnection,
                                              Q_ARG(QString, QStringLiteral("COM28"))));
            QCOMPARE(port->currentText(), QStringLiteral("COM28"));
            QVERIFY(QMetaObject::invokeMethod(workspace, "setEngine", Qt::DirectConnection,
                                              Q_ARG(QString, QStringLiteral("raw"))));
            QCOMPARE(engine->currentData().toString(), QStringLiteral("raw"));
            QMetaObject::invokeMethod(workspace, "sendCommandText", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("ping")),
                                      Q_ARG(QString, QStringLiteral("LF")));
            QVERIFY(terminal->toPlainText().contains(QStringLiteral("请先连接串口")));
        }
        QVERIFY(!plugin->canUnload());
        if (auto *managed = qobject_cast<IDeviceWorkspace *>(workspace)) {
            managed->shutdown();
        }
        delete workspace;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(plugin->canUnload());
        plugin->shutdown();
        QVERIFY2(loader.unload(), qPrintable(loader.errorString()));
    }
    QVERIFY(ids.contains(QStringLiteral("serial-scope")));
    QVERIFY(ids.contains(QStringLiteral("tn160-thermal")));
}

void PluginLoadingTests::repeatedWorkspaceLifecycleAndSerialFailurePaths()
{
    const QDir packages(qEnvironmentVariable("SEREIN_TEST_COMPONENT_DIR"));
    QVERIFY(packages.exists());
    TestHost host;
    for (int round = 0; round < 8; ++round) {
        ComponentManager manager(&host, packages.absolutePath(), false);
        manager.discover();
        for (const ComponentSnapshot &snapshot : manager.components()) {
            QString error;
            QVERIFY2(manager.load(snapshot.metadata.id, &error), qPrintable(error));
            QWidget *workspace = manager.plugin(snapshot.metadata.id)->createWorkspace(nullptr);
            QVERIFY(workspace);
            if (auto *serial = qobject_cast<QWidget *>(workspace);
                serial && snapshot.metadata.id == QStringLiteral("serial-scope")) {
                QMetaObject::invokeMethod(serial, "sendCommandText", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("stress")),
                                          Q_ARG(QString, QStringLiteral("无换行")));
            }
            if (auto *managed = qobject_cast<IDeviceWorkspace *>(workspace)) {
                managed->shutdown();
            }
            delete workspace;
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY2(manager.unload(snapshot.metadata.id, &error), qPrintable(error));
        }
    }
}

void PluginLoadingTests::managerImportsLoadsUnloadsAndRemovesComponents()
{
    const QDir packages(qEnvironmentVariable("SEREIN_TEST_COMPONENT_DIR"));
    QTemporaryDir store;
    QVERIFY(store.isValid());
    TestHost host;
    ComponentManager manager(&host, store.path(), false);
    manager.discover();
    QVERIFY(manager.components().isEmpty());

    QStringList importedIds;
    for (const QFileInfo &file : packages.entryInfoList(
             {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name)) {
        QString error;
        const QString id = manager.importPackage(file.absoluteFilePath(), &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        importedIds.append(id);
        QVERIFY(manager.isInstalled(id));
        QVERIFY(!manager.isLoaded(id));
        QVERIFY2(manager.load(id, &error), qPrintable(error));
        QVERIFY(manager.isLoaded(id));
        QWidget *workspace = manager.plugin(id)->createWorkspace(nullptr);
        QVERIFY(workspace);
        QVERIFY(!manager.unload(id, &error));
        if (auto *managed = qobject_cast<IDeviceWorkspace *>(workspace)) {
            managed->shutdown();
        }
        delete workspace;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY2(manager.unload(id, &error), qPrintable(error));
        QVERIFY(!manager.isLoaded(id));
    }
    QCOMPARE(importedIds.size(), 2);
    for (const QString &id : importedIds) {
        QString error;
        QVERIFY2(manager.removeInstalled(id, &error), qPrintable(error));
        QVERIFY(!manager.isInstalled(id));
    }
    QVERIFY(QDir(store.path()).entryList(QDir::Files).isEmpty());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    PluginLoadingTests test;
    return QTest::qExec(&test, argc, argv);
}

#include "PluginLoadingTests.moc"
