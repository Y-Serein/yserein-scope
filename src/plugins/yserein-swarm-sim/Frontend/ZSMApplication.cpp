#include "ZSMApplication.h"
#include "ZSMBackend.h"
#include "ZSMLoggingCategory.h"
#include "ZSMQmlGlobal.h"
#include "DroneShowController.h"
#include "DroneShowPlayerController.h"
#include "MockVehicleController.h"

#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickView>
#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QSettings>
#include "MavlinkProtocol.h"

ZSM_LOGGING_CATEGORY(ZSMApplicationLog, "Frontend.ZDSApplication")

ZSMApplication* ZSMApplication::s_instance = nullptr;

ZSMApplication::ZSMApplication(QObject* parent)
	: QObject(parent)
	, _1HzTimer(new QTimer(this))
	, _500HzTimer(new QTimer(this))
{
	zInfo(ZSMApplicationLog) << "YSerein Swarm Simulator Host Starting";
}

ZSMApplication::~ZSMApplication()
{
	zInfo(ZSMApplicationLog) << "YSerein Swarm Simulator Host Destroy";
	stopAll();
	if (s_instance == this) s_instance = nullptr;
}

ZSMApplication* ZSMApplication::instance()
{
	if (!s_instance) s_instance = new ZSMApplication;
	return s_instance;
}

void ZSMApplication::destroyInstance()
{
	delete s_instance;
	s_instance = nullptr;
}

void ZSMApplication::init()
{
	_setLoggingCategory();

	_backendThread = new QThread(this);
	_backendThread->setObjectName(QString("Backend Thread"));
	_zsmBackend = zsmApp();   // 第一次调用初始化
	_zsmBackend->moveToThread(_backendThread);

	connect(_backendThread, &QThread::started, _zsmBackend, &ZSMBackend::setupBackend);

	_backendThread->start();
}

void ZSMApplication::stopAll()
{
	if (_1HzTimer) _1HzTimer->stop();
	if (_500HzTimer) _500HzTimer->stop();

	if (_zsmBackend && _backendThread) {
		// 请求后端停止，等待 backStopped 信号后再退出线程
		QMetaObject::invokeMethod(_zsmBackend, "stopBackend");
		QEventLoop loop;
		connect(_zsmBackend, &ZSMBackend::backStopped, &loop, &QEventLoop::quit);
		// 从后端线程发起的关闭流程需要等待
		QTimer::singleShot(3000, &loop, &QEventLoop::quit);
		loop.exec();

		_backendThread->quit();
		_backendThread->wait();
		_backendThread = nullptr;
		_zsmBackend = nullptr;
	}
}

void ZSMApplication::setView(QQuickView* view)
{
	_view = view;
}

void ZSMApplication::setupFrontend()
{
	_setLoggingCategory();

	if (!_qmlTypesRegistered) {
		_registerQmlTypes();
		_qmlTypesRegistered = true;
	}

	_1HzTimer->setInterval(1000);
	_1HzTimer->setSingleShot(false);
	connect(_1HzTimer, &QTimer::timeout, this, &ZSMApplication::_1HzLoop);
	_1HzTimer->start();

	_500HzTimer->setInterval(2);
	_500HzTimer->setSingleShot(false);
	connect(_500HzTimer, &QTimer::timeout, this, &ZSMApplication::_500HzLoop);
	_500HzTimer->start();

	_controllerInit();

	_loadQml();
	emit _frontendReady();
}

QString ZSMApplication::applicationDir()
{
	static std::once_flag initFlag;
	static QString dir;

	std::call_once(initFlag, []() {
		dir = QCoreApplication::applicationDirPath();
		zDebug(ZSMApplicationLog) << "exe Dir:" << dir;
	});

	return dir;
}

QString ZSMApplication::applicationDirPath()
{
	return QCoreApplication::applicationDirPath();
}

void ZSMApplication::addTask(std::function<void()> func)
{
	_taskQueue.enqueue(func);
}

void ZSMApplication::_1HzLoop()
{
	emit _1HzSignal();
}

void ZSMApplication::_500HzLoop()
{
	emit _500HzSignal();
	_dequeueTask();
}

void ZSMApplication::_setLoggingCategory()
{
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Frontend.ZSMApplication", true);

	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Frontend.DroneShow.DroneShowController", false);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Frontend.DroneShow.DroneSpacification", false);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Frontend.DroneShow.ShowSpecification", false);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Frontend.DroneShow.SphereModelController", true);

	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Frontend.Show3D.DroneShowPlayerController", false);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Frontend.Show3D.DroneShowPlayerEngine", false);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Frontend.Show3D.DroneShowInstanceTable", false);

	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Frontend.MockVehicle.MockVehicleController", true);

	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Common.Geo.ZSMGeo", true);

	ZSMLoggingCategoryRegister::instance()->setFilterRules();
}

void ZSMApplication::_registerQmlTypes()
{
	ZSMQmlGlobal::registerQmlTypes();
	DroneShowController::registerQmlTypes();
	DroneShowPlayerController::registerQmlTypes();
	MockVehicleController::registerQmlTypes();
}

void ZSMApplication::_controllerInit()
{
	DroneShowController::instance()->init();
	DroneShowPlayerController::instance()->init();
	MockVehicleController::instance()->init();
}

void ZSMApplication::_dequeueTask()
{
	size_t size = _taskQueue.size_approx();
	if (size == 0) return;

	if (size > 500) {
		zWarning(ZSMApplicationLog) << "There are too many tasks from the backend, didn't consume yet: " << size;
	}

	std::function<void()> task;
	while (_taskQueue.try_dequeue(task)) {
		task();
	}
}

void ZSMApplication::_loadQml()
{
	if (_view) {
		_view->loadFromModule(QStringLiteral("YSerein.SwarmSimulator"), QStringLiteral("Main"));
	}
}
