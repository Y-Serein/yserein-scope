#include "YsereinUniversePlugin.h"

#include "YsereinUniverseWorkspace.h"

namespace serein::scope::yserein_universe {

PluginMetadata YsereinUniversePlugin::metadata() const
{
    return {
        .id = QStringLiteral("yserein-universe"),
        .name = QStringLiteral("YSerein Universe"),
        .description = tr("GMAT → YSerein Adapter → OpenSpace 全链路闭环：一键运行轨道生成、轨迹统一格式转换、"
                          "OpenSpace 资产可视化与运行时验证。"),
        .version = QStringLiteral("0.1.0"),
        .category = tr("仿真工具"),
        .accent = QStringLiteral("#d97706"),
        .capabilities = {tr("GMAT 轨道求解"), tr("轨迹统一格式转换"), tr("OpenSpace 资产可视化"),
                         tr("OpenSpace 运行时验证"), tr("一键闭环运行")},
        .interfaceVersion = PluginInterfaceVersion,
    };
}

bool YsereinUniversePlugin::initialize(IHostServices *host)
{
    if (!host || m_host) return false;
    m_host = host;
    m_host->log(QStringLiteral("info"), tr("YSerein Universe 闭环调用工具已加载到宿主"));
    return true;
}

QWidget *YsereinUniversePlugin::createWorkspace(QWidget *parent)
{
    if (!m_host || m_workspace) return nullptr;
    auto *workspace = new YsereinUniverseWorkspace(m_host, parent);
    m_workspace = workspace;
    connect(workspace, &QObject::destroyed, this, [this] { m_workspace = nullptr; });
    return workspace;
}

bool YsereinUniversePlugin::canUnload() const
{
    return m_workspace.isNull();
}

void YsereinUniversePlugin::shutdown()
{
    if (m_host) m_host->log(QStringLiteral("info"), tr("YSerein Universe 闭环调用工具已从宿主卸载"));
    m_host = nullptr;
}

} // namespace serein::scope::yserein_universe
