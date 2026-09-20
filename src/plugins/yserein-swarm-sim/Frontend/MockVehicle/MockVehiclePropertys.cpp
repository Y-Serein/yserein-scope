//
// Created by Hoppz on 25-7-21.
//

#include "MockVehiclePropertys.h"

#include "DroneShowPlayerController.h"
#include "MockVehicleController.h"
#include "ZSMBackend.h"
#include "MockVehicleManager.h"
#include "MockVehiclePool.h"
#include "MockVehicleWorker.h"
#include "ZSMLoggingCategory.h"

Q_LOGGING_CATEGORY(MockVehiclePropertysLog, "Frontend.MockVehicle.MockVehiclePropertys")

MockVehiclePropertys::MockVehiclePropertys(QObject *parent)
    : QObject(parent)
{
}

MockVehiclePropertys::MockVehiclePropertys(const VehicleStruct &vehicle, QObject *parent)
    : QObject(parent)
    , _vehicleStruct(vehicle)
{
}

MockVehiclePropertys::~MockVehiclePropertys()
{
}

void MockVehiclePropertys::updateFromStrut(const VehicleStruct &other)
{
    _updateBatteryRemaining(other.battery.battery_remaining);
    _updateHeading(other.yaw);
    _updatePosition(other.gps.x, other.gps.y, other.gps.z);
    _updateCommLost(other.commLost);

    checkPassFilter();
}

void MockVehiclePropertys::postTaskToMockVehicleThread(std::function<void(MockVehicle*)> task)
{
    const int te_vehicleId = _vehicleStruct.vehicleId;

    zsmApp()->addTask([te_vehicleId, task]() {
        MockVehicleWorker* worker = MockVehicleManager::instance()->getWorkerById(te_vehicleId);
        if ( !worker ) {
            zWarning(MockVehicleManagerLog) << "not find vehicle:" << te_vehicleId << "in any worker";
            return ;
        }

        worker->addTask([te_vehicleId, worker, task]() {
            MockVehicle* te_vehicle = worker->getVehicle(te_vehicleId);
            if ( !te_vehicle ) {
                zWarning(MockVehicleWorkerLog) << "vehicle:" << te_vehicleId << "not found in worker";
                return ;
            }

            task(te_vehicle);
        });
    });
}

void MockVehiclePropertys::setBatteryRemaining(int batteryRemaining)
{
    // zInfo(MockVehiclePropertysLog) << "get value:" << batteryRemaining;
    if ( batteryRemaining > INT8_MAX || batteryRemaining < INT8_MIN  ) return ;
    if ( batteryRemaining == _vehicleStruct.battery.battery_remaining ) return ;

    postTaskToMockVehicleThread([batteryRemaining](MockVehicle* vehicle) {
        vehicle->setBatteryRemaining(batteryRemaining);
    });
}

void MockVehiclePropertys::setHeading(int heading)
{
    if ( heading < 0. || heading > 360. ) return ;

    postTaskToMockVehicleThread([heading](MockVehicle* vehicle) {
        vehicle->setHeading(heading);
    });
}

void MockVehiclePropertys::setPosition(QVector3D position)
{
    DroneShowPlayerController::instance()->updateVehiclePosition(vehicleId(),position);

    postTaskToMockVehicleThread([position](MockVehicle* vehicle) {
        vehicle->updatePosition(position);
    });
}

void MockVehiclePropertys::setCommLost(bool val)
{
    postTaskToMockVehicleThread([val](MockVehicle* vehicle) {
        vehicle->setCommLost(val);
    });
}

void MockVehiclePropertys::setPassFilter(bool passFilter)
{
    if ( passFilter == _passFilter ) return ;
    _passFilter = passFilter;
    emit passFilterChanged();
}

void MockVehiclePropertys::setCanBeChecked(bool canBeChecked)
{
    if ( canBeChecked == _canBeChecked ) return ;
    _canBeChecked = canBeChecked;
    emit canBeCheckedChanged();
}

void MockVehiclePropertys::checkPassFilter()
{
    const auto controller = MockVehicleController::instance();

    // 检查电池过滤
    if ( controller->batteryPFilterEnabled() &&
         _vehicleStruct.battery.battery_remaining <= controller->batteryPFilterVal()) {
        setPassFilter(false);
    } else {
        setPassFilter(true);
    }
}

bool MockVehiclePropertys::_updateBatteryRemaining(int batteryRemaining)
{
    if ( batteryRemaining > INT8_MAX || batteryRemaining < INT8_MIN  ) return false;
    if ( batteryRemaining == _vehicleStruct.battery.battery_remaining ) return false;

    _vehicleStruct.battery.battery_remaining =  static_cast<int8_t>(batteryRemaining);
    emit batteryRemainingChanged();
    return true;
}

void MockVehiclePropertys::_updateHeading(float heading)
{
    if ( heading == _vehicleStruct.yaw ) return ;

    _vehicleStruct.yaw = heading;
    emit headingChanged();
}

void MockVehiclePropertys::_updatePosition(float x, float y, float z)
{
    if ( x == _vehicleStruct.gps.x && y == _vehicleStruct.gps.y && z == _vehicleStruct.gps.z ) return ;

    _vehicleStruct.gps.x = x;
    _vehicleStruct.gps.y = y;
    _vehicleStruct.gps.z = z;
    emit positionChanged();
}

void MockVehiclePropertys::_updateCommLost(bool val)
{
    if ( val == _vehicleStruct.commLost ) return;

    _vehicleStruct.commLost = val;
    emit commLostChanged();
}
