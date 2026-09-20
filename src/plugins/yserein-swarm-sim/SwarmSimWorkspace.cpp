#include "SwarmSimWorkspace.h"
#include "ZSMApplication.h"

#include <serein/scope/PluginInterface.h>

#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickView>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>
#include <QtCore/QString>
#include <QtWidgets/QVBoxLayout>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#endif

#include <utility>

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
const unsigned char kModuleAnchor = 0;

QString componentDirectory()
{
	static QString dir;
	if (dir.isEmpty()) {
#ifdef Q_OS_WIN
		HMODULE hModule = nullptr;
		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
			GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&kModuleAnchor), &hModule);
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
void ensureDllSearchPath(const QString &dir)
{
#ifdef Q_OS_WIN
	if (!dir.isEmpty()) {
		SetDllDirectoryW(reinterpret_cast<LPCWSTR>(dir.utf16()));
	}
#endif
}

} // namespace

SwarmSimWorkspace::SwarmSimWorkspace(serein::scope::IHostServices* host, QWidget* parent)
	: QWidget(parent)
	, m_host(host)
{
	QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

	const QString dir = componentDirectory();
	m_view = new QQuickView();
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

	m_app = ZSMApplication::instance();
	m_app->setView(m_view);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	m_container = QWidget::createWindowContainer(m_view, this);
	layout->addWidget(m_container);

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
