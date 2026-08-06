#pragma once

#include "CoreExport.h"

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtPlugin>

class QWidget;

namespace serein::scope {

inline constexpr int PluginInterfaceVersion = 2;
inline constexpr auto DevicePluginIid = "com.serein.scope.IDevicePlugin/2.0";
inline constexpr auto WorkspaceIid = "com.serein.scope.IDeviceWorkspace/1.0";

struct PluginMetadata {
    QString id;
    QString name;
    QString description;
    QString version;
    QString category;
    QString accent = QStringLiteral("#65d1c5");
    QStringList capabilities;
    int interfaceVersion = PluginInterfaceVersion;
};

class SEREIN_SCOPE_CORE_EXPORT IHostServices {
public:
    virtual ~IHostServices();
    virtual void log(const QString &level, const QString &message) = 0;
    virtual void setStatus(const QString &message) = 0;
};

class SEREIN_SCOPE_CORE_EXPORT IDeviceWorkspace {
public:
    virtual ~IDeviceWorkspace();
    virtual void shutdown() = 0;
};

class SEREIN_SCOPE_CORE_EXPORT IDevicePlugin {
public:
    virtual ~IDevicePlugin();
    [[nodiscard]] virtual PluginMetadata metadata() const = 0;
    virtual bool initialize(IHostServices *host) = 0;
    virtual QWidget *createWorkspace(QWidget *parent) = 0;
    [[nodiscard]] virtual bool canUnload() const = 0;
    virtual void shutdown() = 0;
};

} // namespace serein::scope

Q_DECLARE_INTERFACE(serein::scope::IDevicePlugin,
                    "com.serein.scope.IDevicePlugin/2.0")
Q_DECLARE_INTERFACE(serein::scope::IDeviceWorkspace,
                    "com.serein.scope.IDeviceWorkspace/1.0")
