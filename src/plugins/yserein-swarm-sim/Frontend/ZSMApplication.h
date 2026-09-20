#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <functional>

#include "concurrentqueue.h"

class ZSMBackend;
class QQuickView;

#if defined(guiApp)
#undef guiApp
#endif
#define guiApp() (static_cast<ZSMApplication *>(ZSMApplication::instance()))

class ZSMApplication : public QObject
{
	Q_OBJECT
public:
	explicit ZSMApplication(QObject* parent = nullptr);
	~ZSMApplication() override;

	// 单例（由 SwarmSimWorkspace 管理生命周期）
	static ZSMApplication* instance();
	static void destroyInstance();

	// 启动后端线程（由 workspace 在创建 view 之后调用）
	void init();
	// 停止后端线程和前端控制器（由 workspace 析构时调用）
	void stopAll();

	// 与原始 ZSMApplication 保持一致的静态工具接口
	static QString applicationDir();
	static QString applicationDirPath();

	// 供后端线程投递前端任务
	void addTask(std::function<void()> func);

	// 供 workspace 绑定 QML 视图
	void setView(QQuickView* view);

public slots:
	// 后端 setupBackend 完成后被调用（从后端线程 queued 到主线程）
	void setupFrontend();

signals:
	// 后端就绪并且前端已初始化，可以加载 QML
	void _frontendReady();
	void _1HzSignal();
	void _500HzSignal();

private slots:
	void _1HzLoop();
	void _500HzLoop();

private:
	void _setLoggingCategory();
	void _registerQmlTypes();
	void _controllerInit();
	void _dequeueTask();
	void _loadQml();

	QTimer* _1HzTimer = nullptr;
	QTimer* _500HzTimer = nullptr;

	ZSMBackend* _zsmBackend = nullptr;
	QThread* _backendThread = nullptr;

	QQuickView* _view = nullptr;
	bool _qmlTypesRegistered = false;

	moodycamel::ConcurrentQueue<std::function<void()>> _taskQueue;

	static ZSMApplication* s_instance;
};
