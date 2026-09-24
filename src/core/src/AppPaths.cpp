#include <serein/scope/AppPaths.h>

#include <QCoreApplication>
#include <QDir>

namespace serein::scope {

QString AppPaths::appDir()
{
    return QDir(QCoreApplication::applicationDirPath()).absolutePath();
}

QString AppPaths::dataRoot()
{
    return QDir(appDir()).filePath(QStringLiteral("runtime"));
}

QString AppPaths::configDir()
{
    return QDir(dataRoot()).filePath(QStringLiteral("config"));
}

QString AppPaths::logDir()
{
    return QDir(dataRoot()).filePath(QStringLiteral("logs"));
}

QString AppPaths::cacheDir()
{
    return QDir(dataRoot()).filePath(QStringLiteral("cache"));
}

QString AppPaths::componentsDir()
{
    return QDir(appDir()).filePath(QStringLiteral("Components"));
}

QString AppPaths::pluginDataDir()
{
    return QDir(dataRoot()).filePath(QStringLiteral("plugins"));
}

} // namespace serein::scope
