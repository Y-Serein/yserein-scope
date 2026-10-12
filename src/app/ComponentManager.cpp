#include "ComponentManager.h"

#include <serein/scope/AppPaths.h>

#include <QtCore/QDir>
#include <QtCore/QCoreApplication>
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
#include <QtCore/QTemporaryDir>
#include <QtCore/QUuid>
#include <QtCore/private/qzipreader_p.h>

#include <algorithm>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

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

#ifdef Q_OS_WIN
// 目录式组件加载期间,临时把组件目录加入 DLL 搜索路径,使组件自带依赖可被解析。
// SetDllDirectory 只影响进程内的后续 LoadLibrary 搜索,加载完立即恢复。
class WindowsDllSearchPathGuard {
public:
    explicit WindowsDllSearchPathGuard(const QString &dir)
    {
        if (dir.isEmpty())
            return;
        m_active = SetDllDirectoryW(reinterpret_cast<LPCWSTR>(dir.utf16()));
    }
    ~WindowsDllSearchPathGuard()
    {
        if (m_active)
            SetDllDirectoryW(nullptr);
    }
    WindowsDllSearchPathGuard(const WindowsDllSearchPathGuard &) = delete;
    WindowsDllSearchPathGuard &operator=(const WindowsDllSearchPathGuard &) = delete;

private:
    bool m_active = false;
};
#endif

bool copyFileRecursively(const QFileInfo &sourceFile, const QString &destDir)
{
    QFile source(sourceFile.absoluteFilePath());
    if (!source.open(QIODevice::ReadOnly))
        return false;
    QSaveFile target(QDir(destDir).filePath(sourceFile.fileName()));
    target.setDirectWriteFallback(true);
    if (!target.open(QIODevice::WriteOnly))
        return false;
    while (!source.atEnd()) {
        const QByteArray block = source.read(1024 * 1024);
        if (block.isEmpty() && source.error() != QFileDevice::NoError)
            return false;
        if (target.write(block) != block.size())
            return false;
    }
    return target.commit();
}

bool copyDirectoryRecursively(const QString &sourceDir, const QString &destDir)
{
    QDir source(sourceDir);
    QDir dest(destDir);
    if (!dest.exists() && !dest.mkpath(QStringLiteral(".")))
        return false;
    for (const QFileInfo &entry : source.entryInfoList(
             QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (entry.isDir()) {
            if (!copyDirectoryRecursively(entry.absoluteFilePath(),
                                          dest.filePath(entry.fileName())))
                return false;
        } else {
            if (!copyFileRecursively(entry, dest.absolutePath()))
                return false;
        }
    }
    return true;
}

// 解压 .zip 组件包到指定目录。会拒绝把文件解压到目标目录之外的路径(防 zip slip)。
bool extractZipPackage(const QString &zipPath, const QString &outDir)
{
    QFile zipFile(zipPath);
    if (!zipFile.open(QIODevice::ReadOnly))
        return false;

    QZipReader reader(&zipFile);
    if (!reader.isReadable())
        return false;

    const QString base = QDir::cleanPath(outDir);
    const QList<QZipReader::FileInfo> files = reader.fileInfoList();
    for (const QZipReader::FileInfo &info : files) {
        // 某些 Windows 工具(.NET ZipFile、资源管理器“压缩文件夹”)生成的 zip
        // 内部用反斜杠分隔路径,Qt 的 QZipReader 识别不了,fileData() 会返回
        // 空数据。这里统一转成正斜杠再计算目标路径。
        const QString name = QString(info.filePath).replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString target = QDir::cleanPath(base + QLatin1Char('/') + name);
        if (target != base && !target.startsWith(base + QLatin1Char('/'))) {
            // 防 zip slip：拒绝把文件解压到目标目录之外
            reader.close();
            return false;
        }
        if (info.isDir) {
            QDir().mkpath(target);
        } else if (info.isFile) {
            if (!QDir().mkpath(QFileInfo(target).absolutePath()))
                return false;
            QFile out(target);
            if (!out.open(QIODevice::WriteOnly))
                return false;
            QByteArray data = reader.fileData(info.filePath);
            // 原始反斜杠名匹配失败时,用正斜杠名再试一次。
            if (data.isEmpty())
                data = reader.fileData(name);
            if (data.isEmpty()) {
                out.close();
                return false;
            }
            if (out.write(data) != data.size()) {
                out.close();
                return false;
            }
            out.close();
        }
    }
    reader.close();
    return true;
}

// 在解压目录中查找 .sscomponent 组件包文件(兼容 zip 内含顶层目录与不含两种形式)。
QFileInfo findPackageFile(const QString &dirPath)
{
    QDir dir(dirPath);
    const QFileInfoList packageFiles = dir.entryInfoList(
        {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name);
    if (!packageFiles.isEmpty())
        return packageFiles.first();

    const QFileInfoList subDirs = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &sub : subDirs) {
        const QFileInfo found = findPackageFile(sub.absoluteFilePath());
        if (!found.filePath().isEmpty())
            return found;
    }
    return {};
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
    // 组件仓库与打包组件合并到同一处：exe 目录 / Components。
    return AppPaths::componentsDir();
}

QString ComponentManager::packagedStorePath()
{
    QDir direct(QCoreApplication::applicationDirPath());
    if (direct.cd(QStringLiteral("Components"))) return direct.absolutePath();

    QDir legacy(QCoreApplication::applicationDirPath());
    legacy.cdUp();
    if (legacy.cd(QStringLiteral("Components"))) return legacy.absolutePath();
    return {};
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
                       record->plugin != nullptr, record->installId});
    }
    return result;
}

ComponentSnapshot ComponentManager::component(const QString &id) const
{
    const ComponentRecord *record = findRecord(id);
    if (!record) return {};
    return {record->metadata, record->packagePath, record->error,
            record->plugin != nullptr, record->installId};
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
    const QStringList removed = removedIds();

    // 自包含目录式组件优先:子目录内含 .sscomponent 及该组件专属的依赖 DLL。
    // 若同名组件同时存在扁平单文件(.sscomponent 直接位于仓库根目录),
    // 优先使用目录式组件,因为单文件组件缺少其依赖 DLL,加载必然失败并
    // 在启动时反复报"找不到指定的模块"。
    for (const QFileInfo &sub : directory.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QFileInfoList packages = QDir(sub.absoluteFilePath()).entryInfoList(
            {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name);
        if (packages.isEmpty())
            continue;
        const QFileInfo &package = packages.first();
        QString error;
        const PluginMetadata metadata = probePackage(package.absoluteFilePath(), &error);
        if (metadata.id.isEmpty()) {
            report(QStringLiteral("warning"),
                   tr("忽略无效插件包 %1：%2").arg(package.fileName(), error));
            continue;
        }
        if (removed.contains(metadata.id)) {
            report(QStringLiteral("info"),
                   tr("跳过用户已移除的插件包：%1").arg(sub.fileName()));
            continue;
        }
        if (ids.contains(metadata.id)) {
            report(QStringLiteral("error"), tr("插件 ID 重复，已忽略 %1").arg(sub.fileName()));
            continue;
        }
        ids.insert(metadata.id);
        auto record = std::make_unique<ComponentRecord>();
        record->installId = metadata.id;
        record->metadata = metadata;
        record->packagePath = package.absoluteFilePath();
        record->packageDir = sub.absoluteFilePath();
        m_records.emplace_back(std::move(record));
    }

    // 扁平 .sscomponent(兼容旧式单文件组件)。若同名自包含目录式组件
    // 已注册,则忽略扁平单文件版本,避免加载缺依赖的裸包。
    for (const QFileInfo &file : directory.entryInfoList(QDir::Files, QDir::Name)) {
        QString error;
        const PluginMetadata metadata = probePackage(file.absoluteFilePath(), &error);
        if (metadata.id.isEmpty()) {
            report(QStringLiteral("warning"),
                   tr("忽略无效插件包 %1：%2").arg(file.fileName(), error));
            continue;
        }
        if (removed.contains(metadata.id)) {
            report(QStringLiteral("info"),
                   tr("跳过用户已移除的插件包：%1").arg(file.fileName()));
            continue;
        }
        if (ids.contains(metadata.id)) {
            report(QStringLiteral("info"),
                   tr("同名自包含目录式组件已优先注册，忽略扁平单文件 %1").arg(file.fileName()));
            continue;
        }
        ids.insert(metadata.id);
        auto record = std::make_unique<ComponentRecord>();
        record->installId = metadata.id;
        record->metadata = metadata;
        record->packagePath = file.absoluteFilePath();
        record->packageDir = directory.absolutePath();
        m_records.emplace_back(std::move(record));
    }
    // 并存版本使用独立目录；安装标识与原插件 ID 分开，不修改插件 ABI。
    QDir versions(directory.filePath(QStringLiteral("__versions")));
    for (const QFileInfo &sub : versions.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (QUuid(sub.fileName()).isNull()) continue;
        const QFileInfoList packages = QDir(sub.absoluteFilePath()).entryInfoList(
            {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name);
        if (packages.isEmpty()) continue;
        QString error;
        const PluginMetadata metadata = probePackage(packages.first().absoluteFilePath(), &error);
        if (metadata.id.isEmpty()) {
            report(QStringLiteral("warning"),
                   tr("忽略无效并存插件包 %1：%2").arg(sub.fileName(), error));
            continue;
        }
        const QString installId = metadata.id + QStringLiteral("#install:") + sub.fileName();
        if (removed.contains(installId)) continue;
        auto record = std::make_unique<ComponentRecord>();
        record->installId = installId;
        record->metadata = metadata;
        record->packagePath = packages.first().absoluteFilePath();
        record->packageDir = sub.absoluteFilePath();
        m_records.emplace_back(std::move(record));
    }
    emit componentsChanged();
}

void ComponentManager::importPackagesFrom(const QString &dirPath)
{
    QDir directory(dirPath);
    if (!directory.exists())
        return;

    // 打包目录与组件仓库是同一物理目录时(安装后二者本就重合),组件已就地
    // 存在,无需再导入。否则重复导入会触发 importPackage 的更新逻辑,把目录式
    // 组件扁平化并删除打包目录中的原始 .sscomponent 文件。
    const QString canonicalStore = QFileInfo(m_storePath).canonicalFilePath();
    const QString canonicalSource = QFileInfo(dirPath).canonicalFilePath();
    if (!canonicalStore.isEmpty() && canonicalStore == canonicalSource)
        return;

    const QStringList removed = removedIds();

    // 跳过用户已移除的组件(自动导入不应复活它们);用户手动导入走 importPackage()
    auto importOne = [&](const QFileInfo &packageFile) {
        QString probeError;
        const PluginMetadata metadata = probePackage(packageFile.absoluteFilePath(), &probeError);
        if (metadata.id.isEmpty() || removed.contains(metadata.id))
            return;
        QString error;
        if (importPackage(packageFile.absoluteFilePath(), &error,
                          ImportMode::Replace).isEmpty()) {
            report(QStringLiteral("warning"),
                   tr("内置插件包导入失败：%1").arg(error));
        }
    };

    // 扁平 .sscomponent
    for (const QFileInfo &package : directory.entryInfoList(
             {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name))
        importOne(package);

    // 自包含目录式组件
    for (const QFileInfo &sub : directory.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QDir subDir(sub.absoluteFilePath());
        const QFileInfoList packages =
            subDir.entryInfoList({QStringLiteral("*.sscomponent")}, QDir::Files);
        if (packages.isEmpty())
            continue;
        importOne(packages.first());
    }
}

void ComponentManager::restorePreferredComponents()
{
    if (!m_persistState) return;
    const QStringList removed = removedIds();
    const QStringList preferred = QSettings().value(LoadedComponentsKey).toStringList();
    for (const QString &id : preferred) {
        if (removed.contains(id)) continue;
        QString error;
        if (!load(id, &error, false)) {
            report(QStringLiteral("error"), tr("恢复插件 %1 失败：%2").arg(id, error));
        }
    }
}

PluginMetadata ComponentManager::inspectPackage(const QString &sourcePath,
                                                QString *error) const
{
    const QFileInfo source(sourcePath);
    QString packageFile = sourcePath;
    QTemporaryDir extracted;
    if (source.isDir()) {
        const QFileInfoList packages = QDir(source.absoluteFilePath()).entryInfoList(
            {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name);
        if (packages.isEmpty()) {
            setError(error, tr("目录中未找到 .sscomponent 组件包：%1").arg(sourcePath));
            return {};
        }
        packageFile = packages.first().absoluteFilePath();
    } else if (source.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0) {
        if (!extracted.isValid()
            || !extractZipPackage(source.absoluteFilePath(), extracted.path())) {
            setError(error, tr("压缩包解压失败：%1").arg(sourcePath));
            return {};
        }
        const QFileInfo package = findPackageFile(extracted.path());
        if (package.filePath().isEmpty()) {
            setError(error, tr("压缩包中未找到 .sscomponent：%1").arg(sourcePath));
            return {};
        }
        packageFile = package.absoluteFilePath();
    }
    return probePackage(packageFile, error);
}

QString ComponentManager::importPackage(const QString &sourcePath, QString *error,
                                        const ImportMode mode,
                                        const QString &replaceId)
{
    const QFileInfo sourceInfo(sourcePath);
    QString packageFile = sourcePath;
    QString sourceDir;
    QTemporaryDir zipTempDir;
    if (sourceInfo.isDir()) {
        const QFileInfoList packages = QDir(sourceInfo.absoluteFilePath()).entryInfoList(
            {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name);
        if (packages.isEmpty()) {
            setError(error, tr("目录中未找到 .sscomponent 组件包：%1").arg(sourcePath));
            return {};
        }
        packageFile = packages.first().absoluteFilePath();
        sourceDir = sourceInfo.absoluteFilePath();
    } else if (sourceInfo.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0) {
        if (!zipTempDir.isValid()
            || !extractZipPackage(sourceInfo.absoluteFilePath(), zipTempDir.path())) {
            setError(error, tr("压缩包解压失败：%1").arg(sourcePath));
            return {};
        }
        const QFileInfo package = findPackageFile(zipTempDir.path());
        if (package.filePath().isEmpty()) {
            setError(error, tr("压缩包中未找到 .sscomponent：%1").arg(sourcePath));
            return {};
        }
        packageFile = package.absoluteFilePath();
        sourceDir = package.absolutePath();
    }

    QString probeError;
    const PluginMetadata metadata = probePackage(packageFile, &probeError);
    if (metadata.id.isEmpty()) {
        setError(error, probeError);
        return {};
    }

    if (mode == ImportMode::RejectConflict) {
        for (const auto &record : m_records) {
            if (record->metadata.id == metadata.id) {
                setError(error, tr("插件 ID 已存在，请在插件管理界面选择替换或新建：%1")
                                    .arg(metadata.id));
                return {};
            }
        }
    }
    ComponentRecord *existing = nullptr;
    if (mode != ImportMode::KeepBoth) {
        if (!replaceId.isEmpty()) {
            existing = findRecord(replaceId);
            if (!existing || existing->metadata.id != metadata.id) {
                setError(error, tr("替换目标与导入包的插件 ID 不一致"));
                return {};
            }
        } else {
            existing = findRecord(metadata.id);
            if (!existing) {
                for (auto &record : m_records) {
                    if (record->metadata.id == metadata.id) {
                        existing = record.get();
                        break;
                    }
                }
            }
        }
    } else {
        for (const auto &record : m_records) {
            if (record->metadata.id == metadata.id
                && record->metadata.version == metadata.version) {
                setError(error, tr("此版本已安装：%1 v%2")
                                    .arg(metadata.name, metadata.version));
                return {};
            }
        }
    }
    if (existing && std::any_of(m_records.begin(), m_records.end(),
                                 [&metadata, existing](const auto &record) {
                                     return record.get() != existing
                                         && record->metadata.id == metadata.id
                                         && record->metadata.version == metadata.version;
                                 })) {
        setError(error, tr("导入版本已作为另一安装项存在：%1 v%2")
                            .arg(metadata.name, metadata.version));
        return {};
    }
    if (existing && existing->plugin) {
        setError(error, tr("插件正在运行，请先关闭工作区并卸载后再替换：%1 v%2")
                            .arg(existing->metadata.name, existing->metadata.version));
        return {};
    }

    QDir store(m_storePath);
    if (!store.exists() && !store.mkpath(QStringLiteral("."))) {
        setError(error, tr("无法创建插件仓库：%1").arg(m_storePath));
        return {};
    }
    const QString stem = safeFileStem(metadata.id);
    QString installId = existing ? existing->installId : metadata.id;
    QString destinationDir;
    if (existing && existing->installId != metadata.id) {
        destinationDir = existing->packageDir;
    } else if (!existing && mode == ImportMode::KeepBoth
               && std::any_of(m_records.begin(), m_records.end(),
                   [&metadata](const auto &record) {
                       return record->metadata.id == metadata.id;
                   })) {
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        installId = metadata.id + QStringLiteral("#install:") + uuid;
        destinationDir = store.filePath(QStringLiteral("__versions/") + uuid);
    } else {
        destinationDir = store.filePath(stem);
    }
    const QString oldPackage = existing ? existing->packagePath : QString();
    const QString oldDir = existing ? existing->packageDir : QString();
    if (QFileInfo::exists(destinationDir)
        && (!existing || QDir(oldDir).absolutePath() != QDir(destinationDir).absolutePath())) {
        setError(error, tr("目标目录已被其他插件占用：%1").arg(destinationDir));
        return {};
    }
    if (existing && QFileInfo(packageFile).absoluteFilePath()
                        == QFileInfo(oldPackage).absoluteFilePath()
        && metadata.version == existing->metadata.version) {
        return installId;
    }

    const QString stagingRoot = store.filePath(QStringLiteral("__staging"));
    if (!QDir().mkpath(stagingRoot)) {
        setError(error, tr("无法创建插件临时目录：%1").arg(stagingRoot));
        return {};
    }
    const QString transaction = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString staged = QDir(stagingRoot).filePath(transaction + QStringLiteral(".new"));
    const QString backup = QDir(stagingRoot).filePath(transaction + QStringLiteral(".old"));
    const bool copied = sourceDir.isEmpty()
        ? (QDir().mkpath(staged)
           && copyFileRecursively(QFileInfo(packageFile), staged)
           && (QFileInfo(packageFile).fileName() == stem + QStringLiteral(".sscomponent")
               || QFile::rename(QDir(staged).filePath(QFileInfo(packageFile).fileName()),
                                QDir(staged).filePath(stem + QStringLiteral(".sscomponent")))))
        : copyDirectoryRecursively(sourceDir, staged);
    if (!copied) {
        QDir(staged).removeRecursively();
        setError(error, tr("无法复制插件包到临时目录：%1").arg(sourcePath));
        return {};
    }
    const QFileInfoList stagedPackages = QDir(staged).entryInfoList(
        {QStringLiteral("*.sscomponent")}, QDir::Files, QDir::Name);
    if (stagedPackages.isEmpty()
        || [&] {
            const PluginMetadata stagedMetadata =
                probePackage(stagedPackages.first().absoluteFilePath());
            return stagedMetadata.id != metadata.id
                || stagedMetadata.version != metadata.version;
        }()) {
        QDir(staged).removeRecursively();
        setError(error, tr("复制后的插件包元数据无效或 ID 不一致"));
        return {};
    }
    if (!QDir().mkpath(QFileInfo(destinationDir).absolutePath())) {
        QDir(staged).removeRecursively();
        setError(error, tr("无法创建插件版本目录"));
        return {};
    }
    const bool hadDestination = QFileInfo::exists(destinationDir);
    if (hadDestination && !QDir().rename(destinationDir, backup)) {
        QDir(staged).removeRecursively();
        setError(error, tr("无法暂存旧插件包：%1").arg(destinationDir));
        return {};
    }
    if (!QDir().rename(staged, destinationDir)) {
        if (hadDestination && !QDir().rename(backup, destinationDir)) {
            setError(error, tr("安装失败且旧插件恢复失败，请检查：%1").arg(backup));
        } else {
            setError(error, tr("无法提交插件包，旧版本已恢复"));
        }
        QDir(staged).removeRecursively();
        return {};
    }
    if (hadDestination && !QDir(backup).removeRecursively())
        report(QStringLiteral("warning"), tr("旧插件临时备份未能清理：%1").arg(backup));
    const QString destination = QDir(destinationDir).filePath(stagedPackages.first().fileName());

    if (existing) {
        existing->metadata = metadata;
        existing->packagePath = destination;
        existing->packageDir = destinationDir;
        existing->error.clear();
        if (oldDir == store.absolutePath() && oldPackage != destination)
            QFile::remove(oldPackage);
    } else {
        auto record = std::make_unique<ComponentRecord>();
        record->installId = installId;
        record->metadata = metadata;
        record->packagePath = destination;
        record->packageDir = destinationDir;
        m_records.emplace_back(std::move(record));
    }
    setRemoved(installId, false);
    report(QStringLiteral("info"), tr("已导入插件包：%1 v%2")
                                      .arg(metadata.name, metadata.version));
    emit componentsChanged();
    return installId;
}

bool ComponentManager::load(const QString &id, QString *error, const bool remember)
{
    ComponentRecord *record = findRecord(id);
    if (!record) {
        setError(error, tr("未安装插件：%1").arg(id));
        return false;
    }
    if (record->plugin) return true;
    for (const auto &other : m_records) {
        if (other.get() != record && other->plugin
            && other->metadata.id == record->metadata.id) {
            setError(error, tr("同一插件已有其他版本在运行，请先关闭其工作区并卸载：%1 v%2")
                                .arg(other->metadata.name, other->metadata.version));
            return false;
        }
    }

    auto loader = std::make_unique<QPluginLoader>();
    loader->setLoadHints(QLibrary::LoadHints{});
    loader->setFileName(record->packagePath);

#ifdef Q_OS_WIN
    // 目录式组件:加载前临时把组件目录加入 DLL 搜索路径,使组件自带依赖可被解析。
    WindowsDllSearchPathGuard dllPathGuard(record->packageDir);
#endif

    QObject *instance = loader->instance();
    if (!instance) {
        // 自愈：若当前尝试加载的是扁平单文件组件（其必然缺少依赖 DLL 而失败），
        // 而仓库同时存在同名自包含目录式组件，则自动切换到目录式组件重试。
        const QString stem = safeFileStem(record->metadata.id);
        const QString dirPkg = QDir(m_storePath).filePath(
            stem + QLatin1Char('/') + stem + QStringLiteral(".sscomponent"));
        const bool isFlatPackage =
            QFileInfo(record->packageDir).absoluteFilePath() ==
            QFileInfo(m_storePath).absoluteFilePath();
        if (isFlatPackage && QFileInfo::exists(dirPkg)) {
            record->packagePath = dirPkg;
            record->packageDir = QFileInfo(dirPkg).absolutePath();
            record->error.clear();
            loader = std::make_unique<QPluginLoader>();
            loader->setLoadHints(QLibrary::LoadHints{});
            loader->setFileName(record->packagePath);
            instance = loader->instance();
        }
        if (!instance) {
            record->error = loader->errorString();
            setError(error, record->error);
            emit componentsChanged();
            return false;
        }
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
        || runtimeMetadata.version != record->metadata.version
        || runtimeMetadata.interfaceVersion != PluginInterfaceVersion) {
        record->error = tr("插件运行时 ID/版本与包元数据不一致，或 ABI 不兼容");
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
    const QString packageDir = record->packageDir;
    const QString name = record->metadata.name;
    const QString store = QDir(m_storePath).absolutePath();
    const bool isSelfContainedDir =
        !packageDir.isEmpty() &&
        QDir(packageDir).absolutePath() != store &&
        QFileInfo(packageDir).isDir();
    if (isSelfContainedDir) {
        // 自包含目录式组件:连同依赖 DLL 一起删除整个组件目录
        if (!QDir(packageDir).removeRecursively()) {
            setError(error, tr("无法删除组件目录：%1").arg(packageDir));
            return false;
        }
    } else if (QFile::exists(packagePath) && !QFile::remove(packagePath)) {
        setError(error, tr("无法删除插件包：%1").arg(packagePath));
        return false;
    }
    // 记住用户主动移除的组件,避免启动时被自动导入重新复活
    setRemoved(id, true);
    rememberLoaded(id, false);
    std::erase_if(m_records, [&id](const auto &candidate) {
        return candidate->installId == id;
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
    const QString stem = safeFileStem(metadata.id);
    if (stem == QStringLiteral(".") || stem == QStringLiteral("..")
        || stem == QStringLiteral("__versions")
        || stem == QStringLiteral("__staging")) {
        setError(error, tr("插件 ID 与仓库保留目录冲突：%1").arg(metadata.id));
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
        if (record->installId == id) return record.get();
    }
    return nullptr;
}

const ComponentManager::ComponentRecord *ComponentManager::findRecord(const QString &id) const
{
    for (const auto &record : m_records) {
        if (record->installId == id) return record.get();
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

QStringList ComponentManager::removedIds() const
{
    if (!m_persistState) return {};
    return QSettings().value(QStringLiteral("components/removedIds")).toStringList();
}

void ComponentManager::setRemoved(const QString &id, const bool removed)
{
    if (!m_persistState) return;
    QSettings settings;
    QStringList ids = settings.value(QStringLiteral("components/removedIds")).toStringList();
    if (removed) {
        if (!ids.contains(id)) ids.append(id);
    } else {
        ids.removeAll(id);
    }
    settings.setValue(QStringLiteral("components/removedIds"), ids);
}

void ComponentManager::report(const QString &level, const QString &message) const
{
    if (m_host) m_host->log(level, message);
}

} // namespace serein::scope::app
