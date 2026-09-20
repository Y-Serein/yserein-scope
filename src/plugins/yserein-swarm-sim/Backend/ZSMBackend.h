#pragma once

#include <QtCore/QObject>
#include <qloggingcategory.h>
#include <QTimer>

#include <functional>
#include "concurrentqueue.h"

// zsmApp 作为全局访问的入口
#if defined(zApp)
#undef zApp
#endif

#define zApp (static_cast<ZSMBackend *> (ZSMBackend::instance()) )

#define zsmApp() zApp

class ZSMBackend : public QObject
{
	Q_OBJECT
public:
	explicit ZSMBackend(QObject* parent = nullptr);
	~ZSMBackend();

	static ZSMBackend* instance();

	void setupBackend();

	Q_INVOKABLE void stopBackend();

	void addTask(const std::function<void()>& task);

signals:// 前端
	void backStopped();
	void back2HzSignal();
	void back1HzSignal();
	void back10HzSignal();
	void back500HzSignal();

private slots:
	void _back1HzLoop();
	void _back2HzLoop();
	void _back10HzLoop();
	void _back500HzLoop();

private:

	void _setLoggingCategory();
	void _dequeueTask();


private:
	QTimer* _back2HzTimer = nullptr;					///< 全局 2hz
	QTimer* _back10HzTimer = nullptr;
	QTimer* _back500HzTimer = nullptr;

	moodycamel::ConcurrentQueue<std::function<void()>> _taskQueue;		///< 从前端线程添加后端线程的任务都进入这里
};