#include "ComponentManager.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtTest/QTest>

using namespace serein::scope::app;

class PluginLoadingTest final : public QObject {
    Q_OBJECT

private slots:
    void versionedImportAndLifecycle()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString store = temporary.filePath(QStringLiteral("components"));
        ComponentManager manager(nullptr, store, false);
        manager.discover();
        QVERIFY(manager.components().isEmpty());

        QString error;
        const QString first = QString::fromUtf8(TEST_PLUGIN_V1_PATH);
        const QString second = QString::fromUtf8(TEST_PLUGIN_V2_PATH);
        const QString third = QString::fromUtf8(TEST_PLUGIN_V3_PATH);
        QCOMPARE(manager.inspectPackage(first, &error).version, QStringLiteral("1.0.0"));
        QVERIFY(manager.components().isEmpty()); // 检查包不改变仓库

        const QString baseId = manager.importPackage(first, &error);
        QCOMPARE(baseId, QStringLiteral("scope-test-versioned"));
        const QString oldPath = manager.component(baseId).packagePath;
        QVERIFY(QFileInfo::exists(oldPath));
        QVERIFY(manager.importPackage(second, &error).isEmpty()); // 手动路径拒绝静默覆盖
        QCOMPARE(manager.component(baseId).metadata.version, QStringLiteral("1.0.0"));
        const QString parallelId = manager.importPackage(
            second, &error, ComponentManager::ImportMode::KeepBoth);
        QVERIFY2(!parallelId.isEmpty() && parallelId != baseId, qPrintable(error));
        QCOMPARE(manager.components().size(), 2);
        QCOMPARE(manager.component(baseId).metadata.version, QStringLiteral("1.0.0"));
        QCOMPARE(manager.component(parallelId).metadata.version, QStringLiteral("2.0.0"));
        QVERIFY(QFileInfo::exists(oldPath));

        QVERIFY(manager.importPackage(second, &error,
                                     ComponentManager::ImportMode::KeepBoth).isEmpty());
        QCOMPARE(manager.components().size(), 2);
        QVERIFY2(manager.load(baseId, &error), qPrintable(error));
        QVERIFY(!manager.load(parallelId, &error)); // 同一插件只能加载一个版本
        QVERIFY(manager.importPackage(second, &error,
                                      ComponentManager::ImportMode::Replace,
                                      baseId).isEmpty()); // 运行中的安装项不可覆盖
        QCOMPARE(manager.component(baseId).metadata.version, QStringLiteral("1.0.0"));
        QVERIFY(QFileInfo::exists(oldPath));
        QVERIFY2(manager.unload(baseId, &error), qPrintable(error));
        QVERIFY2(manager.load(parallelId, &error), qPrintable(error));
        QVERIFY2(manager.unload(parallelId, &error), qPrintable(error));
        QCOMPARE(manager.importPackage(third, &error,
                 ComponentManager::ImportMode::Replace, parallelId), parallelId);
        QCOMPARE(manager.component(baseId).metadata.version, QStringLiteral("1.0.0"));
        QCOMPARE(manager.component(parallelId).metadata.version, QStringLiteral("3.0.0"));

        ComponentManager rediscovered(nullptr, store, false);
        rediscovered.discover();
        QCOMPARE(rediscovered.components().size(), 2);
        QCOMPARE(rediscovered.component(parallelId).metadata.version,
                 QStringLiteral("3.0.0"));
        QVERIFY2(rediscovered.removeInstalled(parallelId, &error), qPrintable(error));
        QCOMPARE(rediscovered.components().size(), 1);
        QVERIFY(QFileInfo::exists(oldPath));

        QFile invalid(temporary.filePath(QStringLiteral("invalid.sscomponent")));
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        QCOMPARE(invalid.write("invalid", 7), 7);
        invalid.close();
        QVERIFY(rediscovered.importPackage(invalid.fileName(), &error).isEmpty());
        QCOMPARE(rediscovered.component(baseId).metadata.version,
                 QStringLiteral("1.0.0"));
        QVERIFY(QFileInfo::exists(oldPath));

        QCOMPARE(rediscovered.importPackage(second, &error,
                 ComponentManager::ImportMode::Replace, baseId), baseId);
        QCOMPARE(rediscovered.component(baseId).metadata.version,
                 QStringLiteral("2.0.0"));
        rediscovered.discover();
        QCOMPARE(rediscovered.components().size(), 1);
        QCOMPARE(rediscovered.component(baseId).metadata.version,
                 QStringLiteral("2.0.0"));
    }
};

QTEST_GUILESS_MAIN(PluginLoadingTest)
#include "PluginLoading_test.moc"
