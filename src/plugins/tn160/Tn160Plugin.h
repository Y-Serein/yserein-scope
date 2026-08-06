#pragma once

#include <serein/scope/PluginInterface.h>

#include <QtCore/QPointer>
#include <QtCore/QObject>

namespace serein::scope::tn160 {

class Tn160Plugin final : public QObject, public IDevicePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.serein.scope.IDevicePlugin/2.0" FILE "plugin.json")
    Q_INTERFACES(serein::scope::IDevicePlugin)

public:
    [[nodiscard]] PluginMetadata metadata() const override;
    bool initialize(IHostServices *host) override;
    QWidget *createWorkspace(QWidget *parent) override;
    [[nodiscard]] bool canUnload() const override;
    void shutdown() override;

private:
    IHostServices *m_host = nullptr;
    QPointer<QWidget> m_workspace;
};

} // namespace serein::scope::tn160
