//
// Created by Hoppz on 25-7-21.
//

#include "MockVehicleController.h"
#include "ZSMLoggingCategory.h"

#include "MockVehicleManager.h"
#include "ZSMBackend.h"

#include <qapplicationstatic.h>
#include <QtQml/QQmlEngine>

Q_APPLICATION_STATIC(MockVehicleController, _mockVehicleControllerInstancer)
ZSM_LOGGING_CATEGORY(MockVehicleControllerLog, "Frontend.MockVehicle.MockVehicleController")

MockVehicleController::MockVehicleController(QObject *parent)
    : QObject(parent)
{
}

MockVehicleController::~MockVehicleController()
{
}

MockVehicleController * MockVehicleController::instance()
{
    return _mockVehicleControllerInstancer();
}

void MockVehicleController::registerQmlTypes()
{
}

void MockVehicleController::init()
{
    qmlRegisterUncreatableType<MockVehicleController>("Frontend.Vehicle", 1, 0, "MockVehicleController", "Reference only");
    qmlRegisterUncreatableType<MockVehiclePropertys>("Frontend.Vehicle", 1, 0, "MockVehiclePropertys", "Reference only");
}

MockVehiclePropertys * MockVehicleController::getVehiclePropertys(int vehicleId)
{
    auto it = _vehicleIdMap.find(vehicleId);
    if (it == _vehicleIdMap.end()) return nullptr;
    return it->second;
}

void MockVehicleController::setDetailVehicle(int vehicleId)
{
    auto it = _vehicleIdMap.find(vehicleId);
    if ( it == _vehicleIdMap.end() ) return;

    MockVehiclePropertys * vehicle = it->second;

    if ( vehicleId == _detailVehicleId ) {
        // 取消 detail
        _detailVehicleId = -1;
        _detailVehicle = nullptr;
    } else {
        _detailVehicleId = vehicleId;

        if ( _detailVehicleId == -1 )   _detailVehicle = nullptr;
        else                            _detailVehicle = vehicle;
    }

    qInfo(MockVehicleControllerLog) << "VehicleId:" << _detailVehicleId << _detailVehicle;

    emit detailVehicleChanged();
    emit detailVehicleIdChanged();
}

void MockVehicleController::generateGridVehicles(int rows, int cols, double spacing)
{
    if ( rows <= 0 || cols <= 0 ) {
        zWarning(MockVehicleControllerLog) << "generateGridVehicles invalid rows/cols:" << rows << cols;
        return;
    }
    zsmApp()->addTask([rows, cols, spacing]() {
        MockVehicleManager::instance()->generateGridMockVehicles(rows, cols, spacing);
    });
}

void MockVehicleController::updataVehicleSnapshot(const std::vector<VehicleStruct>& list)
{
    for (const auto& vehicleStruct : list) {
        auto it = _vehicleIdMap.find(vehicleStruct.vehicleId);

        if ( it == _vehicleIdMap.end() ) {
            MockVehiclePropertys* te_vehicle = new MockVehiclePropertys(vehicleStruct, this);
            _vehicleIdMap[vehicleStruct.vehicleId] = te_vehicle;
            emit countChanged();
        } else {
            it->second->updateFromStrut(vehicleStruct);
        }
    }
}
