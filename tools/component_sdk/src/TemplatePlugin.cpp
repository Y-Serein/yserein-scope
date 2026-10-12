#include "TemplatePlugin.h"
#include "TemplateWorkspace.h"

namespace templateComponent {

serein::scope::PluginMetadata TemplatePlugin::metadata() const
{
    // 这里字段与 src/plugin.json 保持一致；运行时以这里返回的值为准。
    return {
        .id = QStringLiteral("template-component"),
        .name = QStringLiteral("Template Component"),
        .description = tr("Serein Scope 组件模板：新建组件的最小骨架。"),
        .version = QStringLiteral("1.0.0"),
        .category = tr("示例组件"),
        .accent = QStringLiteral("#65d1c5"),
        .capabilities = {QStringLiteral("示例")},
        .interfaceVersion = serein::scope::PluginInterfaceVersion,
    };
}

bool TemplatePlugin::initialize(serein::scope::IHostServices *host)
{
    if (!host || m_host) return false;
    m_host = host;
    m_host->log(QStringLiteral("info"), tr("Template 组件已加载到宿主"));
    return true;
}

QWidget *TemplatePlugin::createWorkspace(QWidget *parent)
{
    if (!m_host || m_workspace) return nullptr;
    auto *workspace = new TemplateWorkspace(m_host, parent);
    m_workspace = workspace;
    connect(workspace, &QObject::destroyed, this, [this] { m_workspace = nullptr; });
    return workspace;
}

bool TemplatePlugin::canUnload() const
{
    return m_workspace.isNull();
}

void TemplatePlugin::shutdown()
{
    if (m_host) {
        m_host->log(QStringLiteral("info"), tr("Template 组件已从宿主卸载"));
    }
    m_host = nullptr;
}

} // namespace templateComponent
