#pragma once

#include <serein/scope/PluginInterface.h>

#include <QtWidgets/QWidget>

namespace serein::scope {
class IHostServices;
class WorkspaceCanvas;
}

class QQuickView;
class QQuickWidget;
class ZSMApplication;

class SwarmSimWorkspace : public QWidget,
                          public serein::scope::IDeviceWorkspace,
                          public serein::scope::IWorkspaceUiProvider {
	Q_OBJECT
	Q_INTERFACES(serein::scope::IDeviceWorkspace serein::scope::IWorkspaceUiProvider)

public:
	explicit SwarmSimWorkspace(serein::scope::IHostServices* host, QWidget* parent = nullptr);
	~SwarmSimWorkspace() override;

	// IDeviceWorkspace
	void shutdown() override;

	// IWorkspaceUiProvider
	serein::scope::WorkspaceUiProvider* workspaceUiProvider() override;

	// 宿主主题同步（Scope 通过 darkMode 动态属性下发）
	void setDarkMode(bool dark);

protected:
	bool event(QEvent *event) override;

private:
	void onQmlError(const QString &description);

	serein::scope::IHostServices* m_host = nullptr;
	ZSMApplication* m_app = nullptr;

	// 中央工作区：唯一一个 QQuickView，渲染 QtQuick3D 3D 主场景。
	// scope 宿主组件架构以 QWidget 为中心，左/右/下面板用 QWidget 承载，
	// 避免多个 QQuickView/QQmlEngine 在本容器中初始化导致的 QML 加载失败。
	serein::scope::WorkspaceCanvas* m_workspace = nullptr;
	QQuickView* m_view = nullptr;
	QWidget* m_container = nullptr;

	// 宿主左右下三块 dock 面板的 content。每个面板都是由独立 QQuickWidget
	// 承载的 QML 页面，满足宿主「面板 content 必须游离于组件内部布局」的约束。
	// 注意：面板 QML 必须在 setupFrontend 完成（类型注册后）才加载，
	// 因此这里延迟到 _frontendReady 信号。
	QQuickWidget* m_contextView = nullptr;
	QQuickWidget* m_inspectorView = nullptr;
	QQuickWidget* m_outputView = nullptr;
	QWidget* m_contextPanel = nullptr;
	QWidget* m_inspectorPanel = nullptr;
	QWidget* m_outputPanel = nullptr;

	// 宿主工作区 UI 贡献（context/inspector/output/componentLibrary）。
	serein::scope::WorkspaceUiProvider* m_uiProvider = nullptr;
};
