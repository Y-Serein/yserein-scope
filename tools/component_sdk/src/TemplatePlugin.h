#pragma once

#include <serein/scope/PluginInterface.h>

#include <QtCore/QObject>
#include <QtCore/QPointer>

namespace templateComponent {

class TemplatePlugin final : public QObject, public serein::scope::IDevicePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.serein.scope.IDevicePlugin/2.0" FILE "plugin.json")
    Q_INTERFACES(serein::scope::IDevicePlugin)

public:
    [[nodiscard]] serein::scope::PluginMetadata metadata() const override;
    bool initialize(serein::scope::IHostServices *host) override;
    QWidget *createWorkspace(QWidget *parent) override;
    [[nodiscard]] bool canUnload() const override;
    void shutdown() override;

private:
    serein::scope::IHostServices *m_host = nullptr;
    QPointer<QWidget> m_workspace;
};

} // namespace templateComponent
