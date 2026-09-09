#include "YsereinHmiPlugin.h"

#include "YsereinHmiWorkspace.h"

namespace serein::scope::yserein_hmi {

PluginMetadata YsereinHmiPlugin::metadata() const
{
    return {
        .id = QStringLiteral("yserein-hmi"),
        .name = QStringLiteral("Y-Serein HMI"),
        .description = tr("面向自有串口屏的离线页面设计器：工程、页面、数值控件、属性与预览；运行时与下载尚未接入。"),
        .version = QStringLiteral("0.1.0"),
        .category = tr("设备插件"),
        .accent = QStringLiteral("#2f6feb"),
        .capabilities = {tr("离线 HMI 设计器"), tr("数值显示控件（可用）"),
                         tr("D133CBS / D211DBV / ESP32-P4（规划中）")},
        .interfaceVersion = PluginInterfaceVersion,
    };
}

bool YsereinHmiPlugin::initialize(IHostServices *host)
{
    if (!host || m_host) return false;
    m_host = host;
    m_host->log(QStringLiteral("info"), tr("Y-Serein HMI 离线作者工作区已加载到宿主"));
    return true;
}

QWidget *YsereinHmiPlugin::createWorkspace(QWidget *parent)
{
    if (!m_host || m_workspace) return nullptr;
    auto *workspace = new YsereinHmiWorkspace(m_host, parent);
    m_workspace = workspace;
    connect(workspace, &QObject::destroyed, this, [this] { m_workspace = nullptr; });
    return workspace;
}

bool YsereinHmiPlugin::canUnload() const
{
    return m_workspace.isNull();
}

void YsereinHmiPlugin::shutdown()
{
    if (m_host) m_host->log(QStringLiteral("info"), tr("Y-Serein HMI 离线作者工作区已从宿主卸载"));
    m_host = nullptr;
}

} // namespace serein::scope::yserein_hmi
