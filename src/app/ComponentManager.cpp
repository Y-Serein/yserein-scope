#include "ComponentManager.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QLibrary>
#include <QtCore/QPluginLoader>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>

#include <algorithm>

namespace serein::scope::app {
namespace {

const QString LoadedComponentsKey = QStringLiteral("components/loadedIds");

QStringList jsonStringList(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString().trimmed();
        if (!text.isEmpty()) result.append(text);
    }
    return result;
}

QString safeFileStem(QString id)
{
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
               QStringLiteral("_"));
    return id;
}

void setError(QString *target, const QString &message)
{
    if (target) *target = message;
}

PluginMetadata hostOwnedMetadata(PluginMetadata metadata)
{
    metadata.id.detach();
    metadata.name.detach();
    metadata.description.detach();
    metadata.version.detach();
    metadata.category.detach();
    metadata.accent.detach();
    metadata.capabilities.detach();
    for (QString &capability : metadata.capabilities) capability.detach();
    return metadata;
}

} // namespace

ComponentManager::ComponentManager(IHostServices *host, QString storePath,
                                   const bool persistState, QObject *parent)
    : QObject(parent), m_host(host), m_storePath(std::move(storePath)),
      m_persistState(persistState)
{
}

ComponentManager::~ComponentManager()
{
    for (auto &record : m_records) {
        if (!record->plugin || !record->loader) continue;
        if (record->plugin->canUnload()) {
            record->plugin->shutdown();
            record->plugin = nullptr;
            record->loader->unload();
        }
    }
}

QString ComponentManager::defaultStorePath()
{
    const QString overridePath = qEnvironmentVariable("SEREIN_COMPONENT_DIR");
    if (!overridePath.trimmed().isEmpty()) return QDir::cleanPath(overridePath);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("components"));
}

QString ComponentManager::storePath() const
{
    return m_storePath;
}

QList<ComponentSnapshot> ComponentManager::components() const
{
    QList<ComponentSnapshot> result;
    result.reserve(qsizetype(m_records.size()));
    for (const auto &record : m_records) {
        result.append({record->metadata, record->packagePath, record->error,
                       record->plugin != nullptr});
    }
    return result;
}

ComponentSnapshot ComponentManager::component(const QString &id) const
{
    const ComponentRecord *record = findRecord(id);
    if (!record) return {};
    return {record->metadata, record->packagePath, record->error,
            record->plugin != nullptr};
}

IDevicePlugin *ComponentManager::plugin(const QString &id) const
{
    const ComponentRecord *record = findRecord(id);
    return record ? record->plugin : nullptr;
}

bool ComponentManager::isInstalled(const QString &id) const
{
    return findRecord(id) != nullptr;
}

bool ComponentManager::isLoaded(const QString &id) const
{
    const ComponentRecord *record = findRecord(id);
    return record && record->plugin;
}

void ComponentManager::discover()
{
    m_records.clear();
    QDir directory(m_storePath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        report(QStringLiteral("error"), tr("无法创建插件仓库：%1").arg(m_storePath));
        emit componentsChanged();
        return;
    }

    QSet<QString> ids;
    for (const QFileInfo &file : directory.entryInfoList(QDir::Files, QDir::Name)) {
        QString error;
        const PluginMetadata metadata = probePackage(file.absoluteFilePath(), &error);
        if (metadata.id.isEmpty()) {
            report(QStringLiteral("warning"),
                   tr("忽略无效插件包 %1：%2").arg(file.fileName(), error));
            continue;
        }
        if (ids.contains(metadata.id)) {
            report(QStringLiteral("error"), tr("插件 ID 重复，已忽略 %1").arg(file.fileName()));
            continue;
        }
        ids.insert(metadata.id);
        auto record = std::make_unique<ComponentRecord>();
        record->metadata = metadata;
        record->packagePath = file.absoluteFilePath();
        m_records.emplace_back(std::move(record));
    }
    emit componentsChanged();
}

void ComponentManager::restorePreferredComponents()
{
    if (!m_persistState) return;
    const QStringList preferred = QSettings().value(LoadedComponentsKey).toStringList();
    for (const QString &id : preferred) {
        QString error;
        if (!load(id, &error, false)) {
            report(QStringLiteral("error"), tr("恢复插件 %1 失败：%2").arg(id, error));
        }
    }
}

QString ComponentManager::importPackage(const QString &sourcePath, QString *error)
{
    QString probeError;
    const PluginMetadata metadata = probePackage(sourcePath, &probeError);
    if (metadata.id.isEmpty()) {
        setError(error, probeError);
        return {};
    }
    ComponentRecord *existing = findRecord(metadata.id);
    if (existing && existing->plugin) {
        setError(error, tr("插件正在运行，请先卸载后再更新：%1").arg(metadata.name));
        return {};
    }
    QDir directory(m_storePath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        setError(error, tr("无法创建插件仓库：%1").arg(m_storePath));
        return {};
    }
    const QString destination = directory.filePath(
        safeFileStem(metadata.id) + QStringLiteral(".sscomponent"));
    if (QFileInfo(sourcePath).absoluteFilePath() != QFileInfo(destination).absoluteFilePath()) {
        QFile source(sourcePath);
        QSaveFile target(destination);
        target.setDirectWriteFallback(true);
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) {
            setError(error, tr("无法读取插件包或写入插件仓库"));
            return {};
        }
        while (!source.atEnd()) {
            const QByteArray block = source.read(1024 * 1024);
            if (block.isEmpty() && source.error() != QFileDevice::NoError) {
                target.cancelWriting();
                setError(error, tr("读取插件包失败：%1").arg(source.errorString()));
                return {};
            }
            if (target.write(block) != block.size()) {
                target.cancelWriting();
                setError(error, tr("写入插件仓库失败：%1").arg(target.errorString()));
                return {};
            }
        }
        if (!target.commit()) {
            setError(error, tr("提交插件包失败：%1").arg(target.errorString()));
            return {};
        }
    }

    if (existing) {
        const QString previousPath = existing->packagePath;
        existing->metadata = metadata;
        existing->packagePath = destination;
        existing->error.clear();
        if (previousPath != destination) QFile::remove(previousPath);
    } else {
        auto record = std::make_unique<ComponentRecord>();
        record->metadata = metadata;
        record->packagePath = destination;
        m_records.emplace_back(std::move(record));
    }
    report(QStringLiteral("info"), tr("已导入插件包：%1 %2")
                                      .arg(metadata.name, metadata.version));
    emit componentsChanged();
    return metadata.id;
}

bool ComponentManager::load(const QString &id, QString *error, const bool remember)
{
    ComponentRecord *record = findRecord(id);
    if (!record) {
        setError(error, tr("未安装插件：%1").arg(id));
        return false;
    }
    if (record->plugin) return true;

    auto loader = std::make_unique<QPluginLoader>();
    loader->setLoadHints(QLibrary::LoadHints{});
    loader->setFileName(record->packagePath);
    QObject *instance = loader->instance();
    if (!instance) {
        record->error = loader->errorString();
        setError(error, record->error);
        emit componentsChanged();
        return false;
    }
    auto *plugin = qobject_cast<IDevicePlugin *>(instance);
    if (!plugin) {
        record->error = tr("不是 Serein Scope ABI %1 插件").arg(PluginInterfaceVersion);
        loader->unload();
        setError(error, record->error);
        emit componentsChanged();
        return false;
    }
    const PluginMetadata runtimeMetadata = hostOwnedMetadata(plugin->metadata());
    if (runtimeMetadata.id != record->metadata.id
        || runtimeMetadata.interfaceVersion != PluginInterfaceVersion) {
        record->error = tr("插件运行时元数据不一致或 ABI 不兼容");
        loader->unload();
        setError(error, record->error);
        emit componentsChanged();
        return false;
    }
    if (!plugin->initialize(m_host)) {
        record->error = tr("插件初始化失败");
        loader->unload();
        setError(error, record->error);
        emit componentsChanged();
        return false;
    }
    record->metadata = runtimeMetadata;
    record->error.clear();
    record->plugin = plugin;
    record->loader = std::move(loader);
    if (remember) rememberLoaded(id, true);
    report(QStringLiteral("info"), tr("插件已加载：%1").arg(record->metadata.name));
    emit componentsChanged();
    return true;
}

bool ComponentManager::unload(const QString &id, QString *error, const bool remember)
{
    ComponentRecord *record = findRecord(id);
    if (!record) {
        setError(error, tr("未安装插件：%1").arg(id));
        return false;
    }
    if (!record->plugin) {
        if (remember) rememberLoaded(id, false);
        return true;
    }
    if (!record->plugin->canUnload()) {
        setError(error, tr("插件仍有活动工作区，不能卸载"));
        return false;
    }

    IDevicePlugin *plugin = record->plugin;
    plugin->shutdown();
    record->plugin = nullptr;
    if (!record->loader->unload()) {
        record->plugin = qobject_cast<IDevicePlugin *>(record->loader->instance());
        if (record->plugin) record->plugin->initialize(m_host);
        record->error = record->loader->errorString();
        setError(error, record->error);
        emit componentsChanged();
        return false;
    }
    record->loader.reset();
    record->error.clear();
    if (remember) rememberLoaded(id, false);
    report(QStringLiteral("info"), tr("插件已从内存卸载：%1").arg(record->metadata.name));
    emit componentsChanged();
    return true;
}

bool ComponentManager::removeInstalled(const QString &id, QString *error)
{
    ComponentRecord *record = findRecord(id);
    if (!record) return true;
    if (record->plugin) {
        setError(error, tr("插件仍在运行，请先卸载"));
        return false;
    }
    const QString packagePath = record->packagePath;
    const QString name = record->metadata.name;
    if (QFile::exists(packagePath) && !QFile::remove(packagePath)) {
        setError(error, tr("无法删除插件包：%1").arg(packagePath));
        return false;
    }
    rememberLoaded(id, false);
    std::erase_if(m_records, [&id](const auto &candidate) {
        return candidate->metadata.id == id;
    });
    report(QStringLiteral("info"), tr("已移除插件：%1").arg(name));
    emit componentsChanged();
    return true;
}

PluginMetadata ComponentManager::probePackage(const QString &path, QString *error)
{
    const QPluginLoader loader(path);
    const QJsonObject root = loader.metaData();
    if (root.isEmpty()) {
        setError(error, tr("无法读取 Qt 插件元数据"));
        return {};
    }
    if (root.value(QStringLiteral("IID")).toString()
        != QString::fromLatin1(DevicePluginIid)) {
        setError(error, tr("插件 IID 不兼容，需要 ABI %1").arg(PluginInterfaceVersion));
        return {};
    }
    const QJsonObject json = root.value(QStringLiteral("MetaData")).toObject();
    PluginMetadata metadata{
        .id = json.value(QStringLiteral("id")).toString().trimmed(),
        .name = json.value(QStringLiteral("name")).toString().trimmed(),
        .description = json.value(QStringLiteral("description")).toString().trimmed(),
        .version = json.value(QStringLiteral("version")).toString().trimmed(),
        .category = json.value(QStringLiteral("category")).toString().trimmed(),
        .accent = json.value(QStringLiteral("accent")).toString(QStringLiteral("#65d1c5")),
        .capabilities = jsonStringList(json.value(QStringLiteral("capabilities"))),
        .interfaceVersion = json.value(QStringLiteral("interfaceVersion")).toInt(),
    };
    if (metadata.id.isEmpty() || metadata.name.isEmpty()
        || metadata.interfaceVersion != PluginInterfaceVersion) {
        setError(error, tr("插件元数据缺失或 ABI 版本不匹配"));
        return {};
    }
    if (metadata.description.isEmpty()) metadata.description = QObject::tr("第三方插件");
    if (metadata.version.isEmpty()) metadata.version = QStringLiteral("0.0.0");
    if (metadata.category.isEmpty()) metadata.category = QStringLiteral("Component");
    return metadata;
}

ComponentManager::ComponentRecord *ComponentManager::findRecord(const QString &id)
{
    for (auto &record : m_records) {
        if (record->metadata.id == id) return record.get();
    }
    return nullptr;
}

const ComponentManager::ComponentRecord *ComponentManager::findRecord(const QString &id) const
{
    for (const auto &record : m_records) {
        if (record->metadata.id == id) return record.get();
    }
    return nullptr;
}

void ComponentManager::rememberLoaded(const QString &id, const bool loaded)
{
    if (!m_persistState) return;
    QSettings settings;
    QStringList ids = settings.value(LoadedComponentsKey).toStringList();
    ids.removeAll(id);
    if (loaded) ids.append(id);
    settings.setValue(LoadedComponentsKey, ids);
}

void ComponentManager::report(const QString &level, const QString &message) const
{
    if (m_host) m_host->log(level, message);
}

} // namespace serein::scope::app
