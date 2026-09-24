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
    // 该组件持有跨线程后端（UDP 收发线程、每个 mock 车一个 worker 线程），
    // 并通过多个 Q_APPLICATION_STATIC 控制器单例（ZSMBackend/LinkManager/
    // MockVehicleManager/MavlinkDistributor 等）共享全局状态。这些单例的
    // 生命周期绑定宿主 QCoreApplication，宿主无法在 QLibrary::unload 前
    // 显式销毁它们；一旦 DLL 被卸载，单例对象及其指向的 vtable/析构代码
    // 仍在已卸载模块内，任何后续访问（包括程序退出时的静态析构）都会
    // 触发访问已卸载 DLL 代码的崩溃。
    //
    // 在彻底重构为显式生命周期管理之前，禁止真实卸载：宁可明确拒绝，
    // 也不允许点击卸载后进程崩溃。关闭工作区请使用工作区关闭，它将
    // 走资源清理路径停掉所有后台线程。
    return false;
}

void SwarmSimPlugin::shutdown()
{
	if (m_host) m_host->log(QStringLiteral("info"), tr("Swarm Sim 组件已从宿主卸载"));
	m_host = nullptr;
}

} // namespace serein::scope::sWarmsim
