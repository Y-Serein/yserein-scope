#include "T384Plugin.h"
#include "T384Workspace.h"

namespace serein::scope::t384 {

PluginMetadata T384Plugin::metadata() const
{
    return {
        .id = QStringLiteral("t384-thermal-ncm"),
        .name = QStringLiteral("T256/T384/T640 USB NCM"),
        .description = tr("T256/T384/T640 USB NCM 实时热图、趋势与实验两点产测标定；质量门通过后自动A/B写入、回读和运行时验证，不冒充OEM温度。"),
        .version = QStringLiteral("0.2.0"),
        .category = tr("设备插件"),
        .accent = QStringLiteral("#ef8e62"),
        .capabilities = {QStringLiteral("USB NCM"), tr("RAW16"),
                         tr("实时热图"), tr("趋势"), tr("实验两点产测"),
                         tr("A/B回读验证")},
        .interfaceVersion = PluginInterfaceVersion,
    };
}

bool T384Plugin::initialize(IHostServices *host)
{
    if (!host || m_host) return false;
    m_host = host;
    m_host->log(QStringLiteral("info"), tr("T256/T384/T640 USB NCM 组件已加载到宿主"));
    return true;
}

QWidget *T384Plugin::createWorkspace(QWidget *parent)
{
    if (!m_host || m_workspace) return nullptr;
    auto *workspace = new T384Workspace(m_host, parent);
    m_workspace = workspace;
    connect(workspace, &QObject::destroyed, this,
            [this] { m_workspace = nullptr; });
    return workspace;
}

bool T384Plugin::canUnload() const
{
    return m_workspace.isNull();
}

void T384Plugin::shutdown()
{
    if (m_host) {
        m_host->log(QStringLiteral("info"), tr("T256/T384/T640 USB NCM 组件已从宿主卸载"));
    }
    m_host = nullptr;
}

} // namespace serein::scope::t384
