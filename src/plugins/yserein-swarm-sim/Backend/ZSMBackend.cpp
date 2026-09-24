#include "ZSMBackend.h"
#include "ZSMLoggingCategory.h"
#include "ZSMApplication.h"
#include "LinkManager.h"
#include "MockVehicleManager.h"
#include "MavlinkDistributor.h"

#include <QtCore/qapplicationstatic.h>

ZSM_LOGGING_CATEGORY(ZSMBackendLog, "Backend.ZSMBackend")

Q_APPLICATION_STATIC(ZSMBackend, _zsmBackendInstance);

ZSMBackend::ZSMBackend(QObject* parent)
	: QObject(parent)
	, _back2HzTimer(new QTimer(this))
	, _back10HzTimer(new QTimer(this))
	, _back500HzTimer(new QTimer(this))
{
	zInfo(ZSMBackendLog) << "create";
}

ZSMBackend::~ZSMBackend()
{
	zInfo(ZSMBackendLog) << "destory";
}

ZSMBackend* ZSMBackend::instance()
{
	return _zsmBackendInstance();
}

void ZSMBackend::setupBackend()
{
	zInfo(ZSMBackendLog) << "start setup";

	_setLoggingCategory();

	// timer 初始化
	_back2HzTimer->setInterval(500);
	_back2HzTimer->setSingleShot(false);
	_back2HzTimer->start();
	connect(_back2HzTimer, &QTimer::timeout, this, &ZSMBackend::_back2HzLoop);

	_back10HzTimer->setInterval(100);
	_back10HzTimer->setSingleShot(false);
	_back10HzTimer->start();
	connect(_back10HzTimer, &QTimer::timeout, this, &ZSMBackend::_back10HzLoop);

	_back500HzTimer->setInterval(2);
	_back500HzTimer->setSingleShot(false);
	_back500HzTimer->start();
	connect(_back500HzTimer, &QTimer::timeout, this, &ZSMBackend::_back500HzLoop);

	// 模块初始化
	LinkManager::instance()->init();
	MockVehicleManager::instance()->init();			///< LinkManager 要在 MockVehicle 之前初始化
	MavlinkDistributor::instance()->init();

	//TODO test_num can set in UI
	int test_num = 0;

	QTimer::singleShot(2000,[=]() {
		for (int i = 1; i <= test_num; i++) {
			MockVehicleManager::instance()->addMockVehicle(i);
		}

		/// 更新每个无人机的位置
		QTimer::singleShot(5000,[=]() {
			std::vector<Pair_IdVector3D_t> vec;
			vec.reserve(test_num);
			for (int i = 1; i <= test_num; i++) {
				// 0.5 米
				vec.emplace_back(i,QVector3D(i * 0.5, 0,0));
			}
			MockVehicleManager::instance()->updateMockVehiclePositionTask(vec);
		});
	});

	// 前端初始化
	QMetaObject::invokeMethod(guiApp(), "setupFrontend");
}

void ZSMBackend::stopBackend()
{
	zInfo(ZSMBackendLog) << "stopping";

	// 停止全部定时器，确保后端线程没有遗留的周期任务
	_back2HzTimer->stop();
	_back10HzTimer->stop();
	_back500HzTimer->stop();

	// 彻底清理后端资源，顺序不能乱：
	//   1) 先切断 MavlinkDistributor 对 worker 的引用，避免悬空指针；
	//   2) 再停掉并释放全部 mock 车 worker 线程（shutdown 会触发池析构）；
	//   3) 最后停掉 UDP 收发链路（接收线程 + 发送 worker 线程）。
	// 三者都必须在 backend 线程内执行，且各自带有限时等待。
	MavlinkDistributor::instance()->clear();
	MockVehicleManager::instance()->shutdown();
	LinkManager::instance()->stop();

	emit backStopped();
}

void ZSMBackend::addTask(const std::function<void()>& task)
{
	_taskQueue.enqueue(task);
}

void ZSMBackend::_back2HzLoop()
{
	static uint8_t _back2HzCount = 0;
	if (_back2HzCount & 1) _back1HzLoop();
	++_back2HzCount;

	emit back2HzSignal();
}

void ZSMBackend::_back10HzLoop()
{
	emit back10HzSignal();
}

void ZSMBackend::_back500HzLoop()
{
	emit back500HzSignal();
	_dequeueTask();
}

void ZSMBackend::_back1HzLoop()
{
	emit back1HzSignal();
}

void ZSMBackend::_setLoggingCategory()
{
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.ZDSBackend", true);

	/// Comms
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.Comms.UDPWinLink", true);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.Comms.UDPWinReceiver", true);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.Comms.MavlinkDistributor", false);

	/// MockVehicle
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.MockVehicle.MockVehicleManager", true);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.MockVehicle.MockVehicleWorker", true);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.MockVehicle.MockVehiclePool", true);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.MockVehicle.MockVehicle", true);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.MockVehicle.MockVehicleFTP", false);
	ZSMLoggingCategoryRegister::instance()->setCategoryLogging("Backend.MockVehicle.MockVehicleVerbose", false);
	ZSMLoggingCategoryRegister::instance()->setFilterRules();
}

void ZSMBackend::_dequeueTask()
{
	size_t size = _taskQueue.size_approx();
	if (size == 0) return ;

	if (size > 500) {
		zWarning(ZSMBackendLog) << "There are too many tasks from the frontend, didn't consume yet: " << size;
	}

	std::function<void()> task;
	while ( _taskQueue.try_dequeue(task) ) {
		task();
	}
}
