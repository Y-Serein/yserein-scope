#include "MockVehiclePool.h"
#include "ZSMLoggingCategory.h"
#include <utility>

ZSM_LOGGING_CATEGORY(MockVehiclePoolLog, "Backend.MockVehicle.MockVehiclePool")

MockVehiclePool::MockVehiclePool(QObject* parent)
	: QObject(parent)
{
	// int thread_number = QThread::idealThreadCount();

	int thread_number = 20;

	zInfo(MockVehiclePoolLog) << "create, thread count:" << thread_number;
	for (int i = 0; i < thread_number; i++) {
		// 不要 parent = this 不然无法 move to threads
		auto* worker = new MockVehicleWorker(QString("Mock Vehicle Worker %1").arg(i));
        connect(worker, &MockVehicleWorker::vehiclesCleared, this, [this]() {
            if (_pendingClears == 0 || --_pendingClears != 0) return;
            emit vehiclesCleared();
        }, Qt::QueuedConnection);
		worker->init();
		worker->start();

		_workers.push_back(MockVehicleWorkerInfo{worker, 0});
	}

}

MockVehiclePool::~MockVehiclePool()
{
	// 由于没有 parent , 手动析构
	for (auto& info : _workers) {
		delete info.worker;
	}
}

MockVehicleWorker* MockVehiclePool::addVehicle(uint16_t vehicleId)
{
	MockVehicleWorkerInfo* info = _allocateWorker();

	if (!info) {
		return nullptr;
	}

	if ( _idToWorkerMap.contains(vehicleId) ) {
		qCritical(MockVehiclePoolLog) << "Vehicle already exists";
		return nullptr;
	}

	bool success = info->worker->addVehicleTask(vehicleId);
	if (success) {
		info->vehicleNum++;

		_idToWorkerMap[vehicleId] = info->worker;

		return info->worker;
	}

	return nullptr;
}

MockVehicleWorker* MockVehiclePool::addVehicle(uint16_t vehicleId, const QVector3D& position)
{
	MockVehicleWorkerInfo* info = _allocateWorker();

	if (!info) {
		return nullptr;
	}

	if ( _idToWorkerMap.contains(vehicleId) ) {
		qCritical(MockVehiclePoolLog) << "Vehicle already exists";
		return nullptr;
	}

	bool success = info->worker->addVehicleTask(vehicleId, position);
	if (success) {
		info->vehicleNum++;

		_idToWorkerMap[vehicleId] = info->worker;

		return info->worker;
	}

	return nullptr;
}

void MockVehiclePool::clearVehicles()
{
    if (_pendingClears != 0) return;
    _idToWorkerMap.clear();
    _pendingClears = static_cast<int>(_workers.size());
    for (auto& info : _workers) {
        info.vehicleNum = 0;
        info.worker->clearVehiclesTask();
    }
    if (_pendingClears == 0) emit vehiclesCleared();
}

uint16_t MockVehiclePool::nextVehicleId() const
{
	uint16_t maxId = 0;
	for (const auto& entry : _idToWorkerMap) {
		if (entry.first > maxId) {
			maxId = entry.first;
		}
	}
	return static_cast<uint16_t>(maxId + 1);
}

int MockVehiclePool::maxCapacity() const
{
	return static_cast<int>(_workers.size()) * MockVehicleWorker::getMaxVehicleNum();
}

void MockVehiclePool::updateVehiclePosition(const std::vector<Pair_IdVector3D_t>& positions)
{
    if (_pendingClears != 0) return;
    constexpr size_t batchSize = 128;
    std::unordered_map<MockVehicleWorker*, std::vector<Pair_IdVector3D_t>> batches;
    batches.reserve(_workers.size());
	for (const auto& pair_ : positions) {
		const auto& worker = _idToWorkerMap.find(pair_.first);
		if ( worker == _idToWorkerMap.end() ) {
			zCritical(MockVehiclePoolLog) << "Vehicle does not exist";
			continue;
		}
        auto& batch = batches[worker->second];
        batch.push_back(pair_);
        if (batch.size() == batchSize) {
            worker->second->addUpdateVehiclePositionsTask(std::move(batch));
            batch = {};
        }
    }
    for (auto& [worker, batch] : batches) {
        if (!batch.empty()) worker->addUpdateVehiclePositionsTask(std::move(batch));
	}
}

MockVehicleWorker* MockVehiclePool::getWorkerById(uint16_t vehicleId)
{
	const auto& it = _idToWorkerMap.find(vehicleId);

	if (it == _idToWorkerMap.end()) {
		zCritical(MockVehiclePoolLog) << "Vehicle does not exist";
		return nullptr;
	}

	return it->second;
}

MockVehicleWorkerInfo* MockVehiclePool::_allocateWorker()
{
	if (_pendingClears != 0) return nullptr;
	MockVehicleWorkerInfo* selected = nullptr;
	int minCount = INT_MAX;

	for (auto& info : _workers) {
		if (info.vehicleNum < MockVehicleWorker::getMaxVehicleNum() && info.vehicleNum < minCount) {
			selected = &info;
			minCount = info.vehicleNum;
		}
	}

	if (!selected) {
		zCritical(MockVehiclePoolLog) << "encountered max vehicle number:" 
			<< 4 * MockVehicleWorker::getMaxVehicleNum() << "minCount:" << minCount;
		return nullptr;
	}
	
	return selected;
}
