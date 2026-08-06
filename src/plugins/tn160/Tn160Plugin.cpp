#include "Tn160Plugin.h"
#include "Tn160Workspace.h"

namespace serein::scope::tn160 {

PluginMetadata Tn160Plugin::metadata() const
{
    return {
        .id = QStringLiteral("tn160-thermal"),
        .name = QStringLiteral("TN160 Thermal"),
        .description = tr("TN160 专项生产与诊断组件，覆盖 UF2 烧录、实时热诊断、设备标定和 Flash 数据检查。"),
        .version = QStringLiteral("1.0.1"),
        .category = tr("设备插件"),
        .accent = QStringLiteral("#f0ad69"),
        .capabilities = {QStringLiteral("UF2"), tr("热图"), QStringLiteral("FFC"), tr("标定")},
        .interfaceVersion = PluginInterfaceVersion,
    };
}

bool Tn160Plugin::initialize(IHostServices *host)
{
    if (!host || m_host) return false;
    m_host = host;
    m_host->log(QStringLiteral("info"), tr("TN160 组件已加载到宿主"));
    return true;
}

QWidget *Tn160Plugin::createWorkspace(QWidget *parent)
{
    if (!m_host || m_workspace) return nullptr;
    auto *workspace = new Tn160Workspace(m_host, parent);
    m_workspace = workspace;
    connect(workspace, &QObject::destroyed, this, [this] { m_workspace = nullptr; });
    return workspace;
}

bool Tn160Plugin::canUnload() const
{
    return m_workspace.isNull();
}

void Tn160Plugin::shutdown()
{
    if (m_host) m_host->log(QStringLiteral("info"), tr("TN160 组件已从宿主卸载"));
    m_host = nullptr;
}

} // namespace serein::scope::tn160
