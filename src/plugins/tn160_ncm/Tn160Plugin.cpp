#include "Tn160Plugin.h"
#include "Tn160Workspace.h"

namespace serein::scope::tn160ncm {

PluginMetadata Tn160Plugin::metadata() const
{
    return {
        .id = QStringLiteral("tn160-thermal-ncm"),
        .name = QStringLiteral("TN160 USB NCM"),
        .description = tr("TN160 USB CDC-NCM 专项组件，覆盖 BOOTSEL UF2、实时热诊断、两状态标定、逐机热模型回读和B补偿清除。"),
        .version = QStringLiteral("1.2.0"),
        .category = tr("设备插件"),
        .accent = QStringLiteral("#f0ad69"),
        .capabilities = {QStringLiteral("USB NCM"), QStringLiteral("UF2"),
                         tr("热图"), QStringLiteral("FFC"), tr("生产标定"),
                         tr("逐机热模型")},
        .interfaceVersion = PluginInterfaceVersion,
    };
}

bool Tn160Plugin::initialize(IHostServices *host)
{
    if (!host || m_host) return false;
    m_host = host;
    m_host->log(QStringLiteral("info"), tr("TN160 USB NCM 组件已加载到宿主"));
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
    if (m_host) m_host->log(QStringLiteral("info"), tr("TN160 USB NCM 组件已从宿主卸载"));
    m_host = nullptr;
}

} // namespace serein::scope::tn160ncm
