#include "MockVehicleManager.h"
#include "ZSMLoggingCategory.h"
#include "ZSMBackend.h"
#include "MockVehiclePool.h"
#include "MavlinkProtocol.h"

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

}

MockVehicleWorker* MockVehicleManager::getWorkerById(int id)
{
	return _vehiclePool->getWorkerById(id);
}

void MockVehicleManager::addMockVehicle(uint16_t vehicleId)
{
	MockVehicleWorker* worker = _vehiclePool->addVehicle(vehicleId);

	if ( worker ) {
		emit addMockVehicleSignal(worker,vehicleId);
	}
}

void MockVehicleManager::addMockVehicle(uint16_t vehicleId, const QVector3D& position)
{
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
	const int maxTotal = _vehiclePool->maxCapacity();
	if ( total > maxTotal ) {
		zWarning(MockVehicleManagerLog) << "网格" << total << "架超过当前容量上限"
			<< maxTotal << ", 超出部分将被忽略";
	}
	const uint16_t idBase = _vehiclePool->nextVehicleId();

	if ( zsm::currentProtocolDomain() == zsm::ProtocolDomain::Standard8 && total > 255 ) {
		zWarning(MockVehicleManagerLog) << "Standard8 协议系统 ID 为 8 位(上限 255), 网格"
			<< total << "架已超出, 超限部分可能无法被地面站正确识别";
	}

	// 网格以原点为中心, x=列方向, y=行方向, z=0
	const double halfCols = (cols - 1) / 2.0;
	const double halfRows = (rows - 1) / 2.0;

	const int createCount = std::min(total, maxTotal);
	int created = 0;
	for ( int r = 0; r < rows && created < createCount; ++r ) {
		for ( int c = 0; c < cols && created < createCount; ++c, ++created ) {
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
}

void MockVehicleManager::updateMockVehiclePositionTask(const std::vector<Pair_IdVector3D_t> &positions)
{
	_vehiclePool->updateVehiclePosition(positions);
}
