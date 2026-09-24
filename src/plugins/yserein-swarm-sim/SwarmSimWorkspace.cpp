#include "SwarmSimWorkspace.h"
#include "ZSMApplication.h"

#include <serein/scope/PluginInterface.h>
#include <serein/scope/WorkspaceCanvas.h>

#include <QtQml/QQmlEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#include <QtQuickWidgets/QQuickWidget>
#include <QtGui/QColor>
#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTextStream>
#include <QtCore/QUrl>
#include <QtCore/QFileInfo>
#include <QtCore/QString>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#endif

#include <utility>

// 作为模块锚点的函数地址必须可靠地落在本 .sscomponent 模块的代码段内。
// 使用 __declspec(dllexport) 强制导出该符号：链接器不会折叠/合并导出符号，
// 因此 GetModuleHandleExW(FROM_ADDRESS) 一定能解析回本模块，而不是宿主 exe。
// 普通的 static 函数（即使取址）或 const 数据对象在 MinGW/LTO/ICF 下仍可能被
// 合并或折叠，导致 componentDirectory() 静默回退到宿主目录，使 QtQuick3D 的
// import 路径解析失败，QML 根对象创建失败 → 界面空白。
#ifdef Q_OS_WIN
extern "C" __declspec(dllexport) void zsmModuleAnchor() {}
#else
void zsmModuleAnchor() {}
#endif

namespace {

// The simulator UI imports the Qt `QtQuick3D` and `Qt5Compat.GraphicalEffects`
// QML modules, which a plain Scope host never deploys (its own QML does not
// use them). To keep every component self-contained, those module folders are
// bundled next to this .sscomponent; adding the component folder to the QML
// engine's import path lets `import QtQuick3D` resolve them at runtime.
//
// The anchor's address is used to locate the currently loaded module (.sscomponent)
// so we can always point at the *real* component folder, regardless of whether
// the host loaded us from its Components store, a zip pack, or a build dir.
//
// Use a function address (code segment) rather than a const data object for the
// anchor. A `const unsigned char` in an anonymous namespace can be folded by the
// compiler/LTO, so GetModuleHandleExW(FROM_ADDRESS) may resolve it to the host
// executable instead of this .sscomponent module, which makes componentDirectory()
// return the host dir and breaks the QML import path for QtQuick3D.
QString componentDirectory()
{
	static QString dir;
	if (dir.isEmpty()) {
#ifdef Q_OS_WIN
		HMODULE hModule = nullptr;
		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&zsmModuleAnchor), &hModule);
		if (hModule) {
			wchar_t path[MAX_PATH] = {};
			const DWORD len = GetModuleFileNameW(hModule, path, MAX_PATH);
			if (len > 0) {
				dir = QFileInfo(QString::fromWCharArray(path, len)).absolutePath();
			}
		}
#endif
		if (dir.isEmpty()) {
			dir = QCoreApplication::applicationDirPath();
		}
	}
	return dir;
}

// The host only adds the component folder to the Win32 DLL search path while it
// is loading the plugin; once loaded, later loads of the QML plugin DLLs (e.g.
// qquick3dplugin / qtquick3dhelpersplugin) cannot see the sibling Qt DLLs in the
// component folder. Re-add the folder so those plugin dependencies resolve.
// We intentionally keep SetDllDirectoryW (rather than SetDefaultDllDirectories +
// AddDllDirectory): the component is now self-contained, i.e. it ships every
// DLL it needs next to its .sscomponent, so a single extra search dir suffices
// and it does not disable the host's PATH-based loads the way
// SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) would.
void ensureDllSearchPath(const QString &dir)
{
#ifdef Q_OS_WIN
	if (!dir.isEmpty()) {
		SetDllDirectoryW(reinterpret_cast<LPCWSTR>(dir.utf16()));
	}
#endif
}

void applySwarmDarkMode(QQuickView *view, bool darkSpecified)
{
	if (view && view->rootContext()) {
		view->rootContext()->setContextProperty(QStringLiteral("swarmDarkMode"), darkSpecified);
	}
}

} // namespace

SwarmSimWorkspace::SwarmSimWorkspace(serein::scope::IHostServices* host, QWidget* parent)
	: QWidget(parent)
	, m_host(host)
{

	const QString dir = componentDirectory();
#ifdef Q_OS_WIN
	{
		// 把 componentDirectory() 解析到的目录写到 %TEMP%，用于确认它是否
		// 回退到了宿主目录（若 dir == appDir，说明锚点解析失败，QtQuick3D
		// 的 import 路径会指向宿主目录而解析不到，界面必然空白）。
		const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
		QFile diag(tempDir + QStringLiteral("/zsm_component_dir.txt"));
		if (diag.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream ts(&diag);
			ts << "componentDir=" << dir << "\n";
			ts << "appDir=" << QCoreApplication::applicationDirPath() << "\n";
			ts << "sameAsAppDir=" << (dir == QCoreApplication::applicationDirPath() ? "true" : "false") << "\n";
		}
	}
#endif
	m_view = new QQuickView();
	// 允许 QQuickView 内嵌到 QWidget 容器并随容器缩放
	m_view->setResizeMode(QQuickView::SizeRootObjectToView);
	// 显式设置背景色，避免渲染异常时显示宿主默认灰/白
	m_view->setColor(QColor(0x0B, 0x10, 0x20));
	m_view->engine()->addImportPath(dir);
	ensureDllSearchPath(dir);
	QObject::connect(m_view, &QQuickView::statusChanged, this,
		[this](QQuickView::Status status) {
			if (status == QQuickView::Error) {
				const auto errors = m_view->errors();
				for (const auto &error : errors) {
					onQmlError(QStringLiteral("%1:%2: %3")
						.arg(error.line())
						.arg(error.column())
						.arg(error.description()));
				}
			}
		});

	// Scaffold 按 Scope 宿主 ABI2 契约把组件贡献到宿主三块 dock（左/右/下）。
	// 中央保留 3D 主场景（World3DView）。
	// 说明：当前版本的 Head/Middle/Foot 仍集中在中央 QQuickView，因为其跨视图依赖
	// （view3DRoot 全局引用）需要重构为共享状态后才能安全迁移；后续分步迁入。
	// 左/右/下 dock 面板：各由独立的 QQuickView 承载对应页面。
	// 面板 content 以 this 为父、但不加入中央布局；宿主会通过 ABI2 接口
	// 将其 reparent 进自己的左/右/下 dock。
	auto* contextView = new QQuickWidget(this);
	contextView->setResizeMode(QQuickWidget::SizeRootObjectToView);
	contextView->engine()->addImportPath(dir);
	m_contextView = contextView;
	m_contextPanel = contextView;
	m_contextPanel->setObjectName(QStringLiteral("workspaceContextContent"));

	auto* inspectorView = new QQuickWidget(this);
	inspectorView->setResizeMode(QQuickWidget::SizeRootObjectToView);
	inspectorView->engine()->addImportPath(dir);
	m_inspectorView = inspectorView;
	m_inspectorPanel = inspectorView;
	m_inspectorPanel->setObjectName(QStringLiteral("workspaceContextContent"));

	auto* outputView = new QQuickWidget(this);
	outputView->setResizeMode(QQuickWidget::SizeRootObjectToView);
	outputView->engine()->addImportPath(dir);
	m_outputView = outputView;
	m_outputPanel = outputView;
	m_outputPanel->setObjectName(QStringLiteral("workspaceContextContent"));

	m_uiProvider = new serein::scope::WorkspaceUiProvider(this);
	serein::scope::WorkspaceUiContext uiContext;
	uiContext.id = QStringLiteral("yserein-swarm-sim");
	uiContext.layoutProfile = serein::scope::WorkspaceLayoutProfile::Diagnostics;
	uiContext.contextPanel   = {m_contextPanel,   tr("编队会话"), nullptr, {}};
	uiContext.inspectorPanel = {m_inspectorPanel, tr("机群属性"), nullptr, {}};
	uiContext.outputPanel    = {m_outputPanel,    tr("回放诊断"), nullptr, {}};
	m_uiProvider->setPanelSides(serein::scope::WorkspacePanelSide::Left,
		serein::scope::WorkspacePanelSide::Right);
	m_uiProvider->setContext(std::move(uiContext));

	m_app = ZSMApplication::instance();
	m_app->setView(m_view);

	// 面板 QML 页面必须在 setupFrontend 完成类型注册后再加载：
	// 否则 Frontend.* 等 qmlRegisterSingletonType 单例不可用，
	// 会导致面板 QML 加载失败（首次打开空白、App unavailable 一类错误）。
	connect(m_app, &ZSMApplication::_frontendReady, this,
		[this] {
			const bool dark = property("darkMode").toBool();
			if (m_contextView) {
				m_contextView->rootContext()->setContextProperty(QStringLiteral("swarmDarkMode"), dark);
				m_contextView->setSource(QUrl(QStringLiteral("qrc:/qt/qml/YSerein/SwarmSimulator/Context/ContextPage.qml")));
			}
			if (m_inspectorView) {
				m_inspectorView->rootContext()->setContextProperty(QStringLiteral("swarmDarkMode"), dark);
				m_inspectorView->setSource(QUrl(QStringLiteral("qrc:/qt/qml/YSerein/SwarmSimulator/Inspector/InspectorPage.qml")));
			}
			if (m_outputView) {
				m_outputView->rootContext()->setContextProperty(QStringLiteral("swarmDarkMode"), dark);
				m_outputView->setSource(QUrl(QStringLiteral("qrc:/qt/qml/YSerein/SwarmSimulator/Output/OutputPage.qml")));
			}
		});

	// 中央主场景使用宿主的 WorkspaceCanvas 承载，而非直接塞入根布局；

	// 面板 content 则已通过 ABI2 接口贡献给宿主左/右/下 dock。
	m_workspace = new serein::scope::WorkspaceCanvas(QStringLiteral("yserein-swarm-sim"));
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	m_container = QWidget::createWindowContainer(m_view);
	m_workspace->addComponent(QStringLiteral("swarm-sim.main"), tr("3D 舞步预览"), m_container,
		QRectF(0.0, 0.0, 1.0, 1.0), QSize(320, 240));
	layout->addWidget(m_workspace, 1);

	// 将宿主深色状态注入所有视图（必须在 QML 加载前设置 context property）。
	applySwarmDarkMode(m_view, property("darkMode").toBool());

	// 后端线程 setupBackend 完成后会通过 queued 调用 setupFrontend，
	// 由 ZSMApplication 负责注册 QML 类型并加载 UI。
	m_app->init();
}

SwarmSimWorkspace::~SwarmSimWorkspace()
{
	if (m_view) {
		m_view->setSource(QUrl());
	}
	ZSMApplication::destroyInstance();
	m_app = nullptr;
	m_view = nullptr;
	m_container = nullptr;
}

void SwarmSimWorkspace::onQmlError(const QString &description)
{
	if (m_host) {
		m_host->log(QStringLiteral("error"),
			QStringLiteral("Swarm Sim QML: %1").arg(description));
	}
}


void SwarmSimWorkspace::shutdown()
{
	if (m_app) {
		m_app->stopAll();
	}
}

serein::scope::WorkspaceUiProvider* SwarmSimWorkspace::workspaceUiProvider()
{
	return m_uiProvider;
}

bool SwarmSimWorkspace::event(QEvent *event)
{
	if (event->type() == QEvent::DynamicPropertyChange) {
		const auto *changeEvent = static_cast<QDynamicPropertyChangeEvent *>(event);
		if (changeEvent->propertyName() == QLatin1String("darkMode")) {
			setDarkMode(property("darkMode").toBool());
		}
	}
	return QWidget::event(event);
}

void SwarmSimWorkspace::setDarkMode(bool darkModeEnabled)
{
	applySwarmDarkMode(m_view, darkModeEnabled);
}
