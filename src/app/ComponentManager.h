#pragma once

#include <serein/scope/PluginInterface.h>

#include <QtCore/QObject>

#include <memory>
#include <vector>

class QPluginLoader;

namespace serein::scope::app {

struct ComponentSnapshot {
    PluginMetadata metadata;
    QString packagePath;
    QString error;
    bool loaded = false;
};

class ComponentManager final : public QObject {
    Q_OBJECT

public:
    explicit ComponentManager(IHostServices *host, QString storePath,
                              bool persistState = true, QObject *parent = nullptr);
    ~ComponentManager() override;

    [[nodiscard]] static QString defaultStorePath();
    [[nodiscard]] static QString packagedStorePath();
    [[nodiscard]] QString storePath() const;
    [[nodiscard]] QList<ComponentSnapshot> components() const;
    [[nodiscard]] ComponentSnapshot component(const QString &id) const;
    [[nodiscard]] IDevicePlugin *plugin(const QString &id) const;
    [[nodiscard]] bool isInstalled(const QString &id) const;
    [[nodiscard]] bool isLoaded(const QString &id) const;

    void discover();
    void importPackagesFrom(const QString &dirPath);
    void restorePreferredComponents();
    QString importPackage(const QString &sourcePath, QString *error = nullptr);
    bool load(const QString &id, QString *error = nullptr, bool remember = true);
    bool unload(const QString &id, QString *error = nullptr, bool remember = true);
    bool removeInstalled(const QString &id, QString *error = nullptr);

signals:
    void componentsChanged();

private:
    struct ComponentRecord {
        PluginMetadata metadata;
        QString packagePath; // 插件库(.sscomponent)文件路径
        QString packageDir;  // 插件包目录(自包含目录组件时用于 DLL 搜索路径)
        QString error;
        std::unique_ptr<QPluginLoader> loader;
        IDevicePlugin *plugin = nullptr;
    };

    [[nodiscard]] static PluginMetadata probePackage(const QString &path,
                                                     QString *error = nullptr);
    [[nodiscard]] ComponentRecord *findRecord(const QString &id);
    [[nodiscard]] const ComponentRecord *findRecord(const QString &id) const;
    [[nodiscard]] QStringList removedIds() const;
    void setRemoved(const QString &id, bool removed = true);
    void rememberLoaded(const QString &id, bool loaded);
    void report(const QString &level, const QString &message) const;

    IHostServices *m_host = nullptr;
    QString m_storePath;
    bool m_persistState = true;
    std::vector<std::unique_ptr<ComponentRecord>> m_records;
};

} // namespace serein::scope::app
