#pragma once

#include <QObject>
#include <qgeocoordinate.h>
#include <qloggingcategory.h>
#include <unordered_map>

#include <concurrentqueue.h>
#include <functional>
#include <vector>

#include "ZSMTypes.h"

Q_DECLARE_LOGGING_CATEGORY(MockVehicleManagerLog)

class MockVehiclePool;
class MockVehicleWorker;

class MockVehicleManager : public QObject
{
	Q_OBJECT
public:

	explicit MockVehicleManager(QObject* parent = nullptr);
	~MockVehicleManager();

	static MockVehicleManager* instance();

	void init();

	/// @brief 停掉并释放全部 mock 车 worker 线程及车辆池。
	/// 只能在 backend 线程内调用；调用后 init() 可重建池以支持重新打开组件。
	void shutdown();

	/// getter ------------------------------------------------

	static QGeoCoordinate defaultLocation() { return _originLocation; }
	static double defaultLatitude() {return _originLocation.latitude();}
	static double defaultLongitude() { return _originLocation.longitude();}

	MockVehicleWorker* getWorkerById(int id);

	/// setter ------------------------------------------------
	

	/// function ---------------------------------------------

	void addMockVehicle(uint16_t vehicleId);

	void addMockVehicle(uint16_t vehicleId, const QVector3D& position);

	/// @brief 一键生成 rows x cols 网格编队位置 (间距 spacing, 自动编号 1..rows*cols)
	/// @param rows 行数
	/// @param cols 列数
	/// @param spacing 飞机间距(米)
	void generateGridMockVehicles(int rows, int cols, double spacing);

	void deleteMockVehicle(uint16_t vehicleId);

	void deleteAllMockVehicles();

	void updateMockVehiclePositionTask(const std::vector<Pair_IdVector3D_t> &positions);

signals:
	void addMockVehicleSignal(MockVehicleWorker* worker, uint16_t vehicleId);


private:
	
	static const QGeoCoordinate _originLocation;
	MockVehiclePool* _vehiclePool = nullptr;
};