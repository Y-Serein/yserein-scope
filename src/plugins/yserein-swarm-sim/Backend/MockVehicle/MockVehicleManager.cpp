#include "MockVehicleManager.h"
#include "ZSMLoggingCategory.h"
#include "ZSMBackend.h"
#include "MockVehiclePool.h"
#include "MavlinkProtocol.h"
#include "MavlinkDistributor.h"
#include "MockVehicleController.h"
#include "ZSMApplication.h"

#include <qapplicationstatic.h>
#include <algorithm>

ZSM_LOGGING_CATEGORY(MockVehicleManagerLog, "Backend.MockVehicle.MockVehicleManager")
Q_APPLICATION_STATIC(MockVehicleManager, _mockVehicleManagerInstance)

const QGeoCoordinate MockVehicleManager::_originLocation = QGeoCoordinate(22.978665339872173, 113.4589746945982, 0);

MockVehicleManager::MockVehicleManager(QObject* parent)
	: QObject(parent)
	, _vehiclePool(new MockVehiclePool(this))
{
	zInfo(MockVehicleManagerLog) << "create";
}

MockVehicleManager::~MockVehicleManager()
{
	zInfo(MockVehicleManagerLog) << "destory";
}

MockVehicleManager* MockVehicleManager::instance()
{
	return _mockVehicleManagerInstance();
}

void MockVehicleManager::init()
{
	// 若池已被 shutdown() 释放，则重建。每次后端线程启动时会走到这里，
	// 因此组件关闭后重新打开可以恢复一套可用的 worker 池。
	if (!_vehiclePool) {
		_vehiclePool = new MockVehiclePool(this);
	}
}

void MockVehicleManager::shutdown()
{
	// 删除车辆池会触发池析构，逐个停掉 worker 线程并释放。
	// 必须在 backend 线程内、且先切断其他线程对该池的引用。
	if (_vehiclePool) {
		delete _vehiclePool;
		_vehiclePool = nullptr;
	}
}

MockVehicleWorker* MockVehicleManager::getWorkerById(int id)
{
	return _vehiclePool->getWorkerById(id);
}

void MockVehicleManager::addMockVehicle(uint16_t vehicleId)
{
	if (zsm::currentProtocolDomain() == zsm::ProtocolDomain::Standard8
	    && (vehicleId == 0 || vehicleId > 250)) {
		zWarning(MockVehicleManagerLog) << "Standard8 vehicle ID must be 1..250:" << vehicleId;
		return;
	}
	MockVehicleWorker* worker = _vehiclePool->addVehicle(vehicleId);

	if ( worker ) {
		emit addMockVehicleSignal(worker,vehicleId);
	}
}

void MockVehicleManager::addMockVehicle(uint16_t vehicleId, const QVector3D& position)
{
	if (zsm::currentProtocolDomain() == zsm::ProtocolDomain::Standard8
	    && (vehicleId == 0 || vehicleId > 250)) {
		zWarning(MockVehicleManagerLog) << "Standard8 vehicle ID must be 1..250:" << vehicleId;
		return;
	}
	MockVehicleWorker* worker = _vehiclePool->addVehicle(vehicleId, position);

	if ( worker ) {
		emit addMockVehicleSignal(worker,vehicleId);
	}
}

void MockVehicleManager::generateGridMockVehicles(int rows, int cols, double spacing)
{
	if ( rows <= 0 || cols <= 0 ) {
		zWarning(MockVehicleManagerLog) << "generateGridMockVehicles invalid rows/cols:" << rows << cols;
		return;
	}
	if ( spacing <= 0.0 ) {
		spacing = 1.2; ///< 默认间距
	}

	const int total = rows * cols;
	const int available = _vehiclePool->maxCapacity() - _vehiclePool->vehicleCount();
	if (total > available) {
		zWarning(MockVehicleManagerLog) << "grid needs" << total
			<< "free slots, available:" << available;
		return;
	}
	const uint16_t idBase = _vehiclePool->nextVehicleId();
	const uint32_t lastId = static_cast<uint32_t>(idBase) + static_cast<uint32_t>(total) - 1U;
	const uint32_t maxId = zsm::currentProtocolDomain() == zsm::ProtocolDomain::Standard8 ? 250U : 65535U;
	if (idBase == 0 || lastId > maxId) {
		zWarning(MockVehicleManagerLog) << "grid IDs exceed active protocol range:"
		                            << idBase << lastId << maxId;
		return;
	}

	// 网格以原点为中心, x=列方向, y=行方向, z=0
	const double halfCols = (cols - 1) / 2.0;
	const double halfRows = (rows - 1) / 2.0;

	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < cols; ++c) {
			const uint16_t vehicleId = static_cast<uint16_t>(idBase + r * cols + c);
			const QVector3D pos(
				static_cast<float>((c - halfCols) * spacing),
				static_cast<float>((r - halfRows) * spacing),
				0.0f);
			addMockVehicle(vehicleId, pos);
		}
	}

	zInfo(MockVehicleManagerLog) << "generateGridMockVehicles rows:" << rows
		<< "cols:" << cols << "total:" << total
		<< "idBase:" << idBase << "spacing:" << spacing;
}

void MockVehicleManager::deleteMockVehicle(uint16_t vehicleId)
{
}

void MockVehicleManager::deleteAllMockVehicles()
{
    MavlinkDistributor::instance()->clear();
    // init() can replace the pool after reopening the component.
    connect(_vehiclePool, &MockVehiclePool::vehiclesCleared, this, [this]() {
        MavlinkDistributor::instance()->clear();
        guiApp()->addTask([]() {
            MockVehicleController::instance()->finishClearingVehicles();
        });
    }, Qt::SingleShotConnection);
    _vehiclePool->clearVehicles();
}

void MockVehicleManager::updateMockVehiclePositionTask(const std::vector<Pair_IdVector3D_t> &positions)
{
	_vehiclePool->updateVehiclePosition(positions);
}
