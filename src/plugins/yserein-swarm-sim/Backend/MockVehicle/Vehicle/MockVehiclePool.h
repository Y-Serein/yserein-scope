#pragma once

#include <QObject>
#include <unordered_map>
#include <vector>
#include <ZSMTypes.h>

#include "MockVehicleWorker.h"

struct MockVehicleWorkerInfo {
	MockVehicleWorker* worker;
	int vehicleNum = 0;
};

class MockVehiclePool : public QObject
{
	Q_OBJECT
public:
	
	explicit MockVehiclePool(QObject* parent = nullptr);
	~MockVehiclePool() override;

	MockVehicleWorker* addVehicle(uint16_t vehicleId);

	/// @brief add vehicle with initial local position (x,y,z meters)
	MockVehicleWorker* addVehicle(uint16_t vehicleId, const QVector3D& position);

	void updateVehiclePosition(const std::vector<Pair_IdVector3D_t> positions);

	/// 获取下一个可用的 VehicleId (当前最大 Id + 1, 空池返回 1)
	uint16_t nextVehicleId() const;

	/// 当前容量上限 = worker 数量 x 单 worker 上限
	int maxCapacity() const;

	/// 获取当前 vehicleId 对应的 Worker 指针 (加入队列任务)
	/// @param vehicleId
	/// @return
	MockVehicleWorker* getWorkerById(uint16_t vehicleId);

private:
	MockVehicleWorkerInfo* _allocateWorker();

private:

	std::vector<MockVehicleWorkerInfo> _workers;
	std::unordered_map<uint16_t, MockVehicleWorker*> _idToWorkerMap;		///< 通过 vehicleId 找到存储在那个 Worker 里

	// static const int MAX_WORKER_NUM = ;
};