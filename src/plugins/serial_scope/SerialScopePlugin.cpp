#include "SerialScopePlugin.h"
#include "SerialScopeWorkspace.h"

namespace serein::scope::serial {

PluginMetadata SerialScopePlugin::metadata() const
{
    return {
        .id = QStringLiteral("serial-scope"),
        .name = QStringLiteral("Serial Scope"),
        .description = tr("连接任意串口设备，解析多通道文本数据，并同时观察波形、通道和终端。"),
        .version = QStringLiteral("1.0.1"),
        .category = tr("仪器插件"),
        .accent = QStringLiteral("#65d1c5"),
        .capabilities = {tr("串口"), tr("波形"), tr("多通道"), tr("终端")},
        .interfaceVersion = PluginInterfaceVersion,
    };
}

bool SerialScopePlugin::initialize(IHostServices *host)
{
    if (!host || m_host) return false;
    m_host = host;
    m_host->log(QStringLiteral("info"), tr("Serial Scope 组件已加载到宿主"));
    return true;
}

QWidget *SerialScopePlugin::createWorkspace(QWidget *parent)
{
    if (!m_host || m_workspace) return nullptr;
    auto *workspace = new SerialScopeWorkspace(m_host, parent);
    m_workspace = workspace;
    connect(workspace, &QObject::destroyed, this, [this] { m_workspace = nullptr; });
    return workspace;
}

bool SerialScopePlugin::canUnload() const
{
    return m_workspace.isNull();
}

void SerialScopePlugin::shutdown()
{
    if (m_host) m_host->log(QStringLiteral("info"), tr("Serial Scope 组件已从宿主卸载"));
    m_host = nullptr;
}

} // namespace serein::scope::serial
