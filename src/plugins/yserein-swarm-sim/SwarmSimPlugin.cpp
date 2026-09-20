#include "SwarmSimPlugin.h"
#include "SwarmSimWorkspace.h"

namespace serein::scope::sWarmsim {

PluginMetadata SwarmSimPlugin::metadata() const
{
	return {
		.id = QStringLiteral("yserein-swarm-sim"),
		.name = tr("YSerein Swarm Simulator"),
		.description = tr("多机编队模拟器组件，覆盖 UDP 双协议接入、舞步播放与网格编队生成。"),
		.version = QStringLiteral("0.1.0"),
		.category = tr("设备插件"),
		.accent = QStringLiteral("#32c9a4"),
		.capabilities = {tr("双协议"), tr("舞步"), tr("网格编队"), tr("地图")},
		.interfaceVersion = PluginInterfaceVersion,
	};
}

bool SwarmSimPlugin::initialize(IHostServices* host)
{
	if (!host || m_host) return false;
	m_host = host;
	m_host->log(QStringLiteral("info"), tr("Swarm Sim 组件已加载到宿主"));
	return true;
}

QWidget* SwarmSimPlugin::createWorkspace(QWidget* parent)
{
	if (!m_host || m_workspace) return nullptr;
	auto* workspace = new SwarmSimWorkspace(m_host, parent);
	m_workspace = workspace;
	connect(workspace, &QObject::destroyed, this, [this] { m_workspace = nullptr; });
	return workspace;
}

bool SwarmSimPlugin::canUnload() const
{
	// 模拟器持有跨线程后端与 Q_APPLICATION_STATIC 控制器单例，暂不支持真实 DLL 卸载。
	return m_workspace.isNull();
}

void SwarmSimPlugin::shutdown()
{
	if (m_host) m_host->log(QStringLiteral("info"), tr("Swarm Sim 组件已从宿主卸载"));
	m_host = nullptr;
}

} // namespace serein::scope::sWarmsim
