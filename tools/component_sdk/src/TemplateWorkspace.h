#pragma once

#include <serein/scope/PluginInterface.h>

#include <QtWidgets/QWidget>

namespace serein::scope { class WorkspaceCanvas; }

namespace templateComponent {

class TemplateWorkspace final : public QWidget,
                                public serein::scope::IDeviceWorkspace,
                                public serein::scope::IWorkspaceUiProvider {
    Q_OBJECT
    Q_INTERFACES(serein::scope::IDeviceWorkspace
                 serein::scope::IWorkspaceUiProvider)

public:
    explicit TemplateWorkspace(serein::scope::IHostServices *host,
                               QWidget *parent = nullptr);
    void shutdown() override;
    serein::scope::WorkspaceUiProvider *workspaceUiProvider() override;
    Q_INVOKABLE bool activateLibraryComponent(const QString &id);

private:
    serein::scope::IHostServices *m_host = nullptr;
    serein::scope::WorkspaceCanvas *m_canvas = nullptr;
    serein::scope::WorkspaceUiProvider *m_uiProvider = nullptr;
};

} // namespace templateComponent
