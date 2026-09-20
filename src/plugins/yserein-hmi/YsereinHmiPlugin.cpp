#include "YsereinHmiPlugin.h"

#include "YsereinHmiWorkspace.h"

namespace serein::scope::yserein_hmi {

PluginMetadata YsereinHmiPlugin::metadata() const
{
    return {
        .id = QStringLiteral("yserein-hmi"),
        .name = QStringLiteral("Y-Serein HMI"),
        .description = tr("面向自有串口屏的离线页面设计器：工程、页面、九类控件、数据绑定、事件与预览；本机目标包和 Runtime 模拟已接入，真实串口与硬件下载需目标后端。"),
        .version = QStringLiteral("0.1.0"),
        .category = tr("设备插件"),
        .accent = QStringLiteral("#2f6feb"),
        .capabilities = {tr("离线 HMI 设计器"), tr("文本/数值/按钮/进度/状态灯/开关控件"),
                         tr("页面绑定与跳转预览"),
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
