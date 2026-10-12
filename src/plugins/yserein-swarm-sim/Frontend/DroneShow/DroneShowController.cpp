#include "DroneShowController.h"
#include "ZSMLoggingCategory.h"
#include "DroneTypes.h"
#include "ShowTypes.h"
#include "DroneSpecification.h"
#include "ShowSpecification.h"
#include "ZSMZipHelper.h"
#include "ZSMApplication.h"
#include "ZSMJsonHelper.h"
#include "DroneShowPlayerController.h"
#include "ZSMScopedTimer.h"
#include "ZSMBackend.h"

#include "MockVehicleManager.h"
#include "MockVehicleController.h"

#include <QtCore/qapplicationstatic.h>
#include <QtQml/QQmlEngine>
#include <vector>

ZSM_LOGGING_CATEGORY(DroneShowControllerLog, "Frontend.DroneShow.DroneShowController")

Q_APPLICATION_STATIC(DroneShowController, _droneShowControllerInstance)

DroneShowController::DroneShowController(QObject* parent)
    : QObject(parent)
{
    zInfo(DroneShowControllerLog) << "create";
}

DroneShowController::~DroneShowController()
{
    zInfo(DroneShowControllerLog) << "destory";
}

DroneShowController* DroneShowController::instance()
{
    return _droneShowControllerInstance();
}

void DroneShowController::registerQmlTypes()
{
    qmlRegisterUncreatableType<DroneShowController>("Frontend.DroneShow", 1, 0, "DroneShowController", "Reference only");
    qmlRegisterUncreatableType<DroneSpecification>("Frontend.DroneShow", 1, 0, "DroneSpecification", "Reference only");
    qmlRegisterUncreatableType<ShowSpecification>("Frontend.DroneShow", 1, 0, "ShowSpecification", "Reference only");

    /// DroneTypes ======================================================
    // Enums
    qRegisterMetaType<DroneTypes::DroneType>("DroneType");

    /// ShowTypes =======================================================
    // Enums
    qRegisterMetaType<ShowTypes::EnvironmentType>("EnvironmentType");
    qRegisterMetaType<ShowTypes::CameraType>("CameraType");
    qRegisterMetaType<ShowTypes::ShowSegmentId>("ShowSegmentId");
    qRegisterMetaType<ShowTypes::GeofenceAction>("GeofenceAction");

    // Structs
    qRegisterMetaType<ShowTypes::Camera_t>("Camera_t");
    qRegisterMetaType<ShowTypes::Location_t>("Location_t");
    qRegisterMetaType<ShowTypes::Environment_t>("Environment_t");
    qRegisterMetaType<ShowTypes::ShowSegment_t>("ShowSegment_t");
    qRegisterMetaType<ShowTypes::ShowMetadata_t>("ShowMetadata_t");
    qRegisterMetaType<ShowTypes::Cue_t>("Cue_t");
    qRegisterMetaType<ShowTypes::CueSheet_t>("CueSheet_t");
    qRegisterMetaType<ShowTypes::GeofencePolygon_t>("GeofencePolygon_t");
    qRegisterMetaType<ShowTypes::Geofence_t>("Geofence_t");
    qRegisterMetaType<ShowTypes::ValidationSettings_t>("ValidationSettings_t");
    qRegisterMetaType<ShowTypes::ShowSettings_t>("ShowSettings_t");
}

void DroneShowController::init()
{
}

ShowSpecification* DroneShowController::showSpecification() const
{
    if (!_loadShow) return nullptr;

    return _show;
}

void DroneShowController::clearShow()
{
    auto* previous = _show;
    _show = nullptr;
    _loadShow = false;
    emit isLoadShowChanged(false);
    if (previous) previous->deleteLater();
}

bool DroneShowController::droneShowFileLoad(QUrl url)
{
    if (MockVehicleController::instance()->clearing()) return false;
    QString strUrl = url.toLocalFile();
    zDebug(DroneShowControllerLog) << "show file url:" << strUrl;

    // 解压
    if (ZSMZipHelper::unzipFile(strUrl, ZSMApplication::applicationDirPath() + _showZDSFileDir)) {
        zDebug(DroneShowControllerLog) << "load show file completed.";

        // 读取 show.json 和 cues.json
        _readShowJsonFile();
    }
    else {
        zWarning(DroneShowControllerLog) << "load show file failed!";
        return false;
    }
       
    return true;
}

DroneSpecification * DroneShowController::getDroneById(int id)
{
    if ( _show == nullptr ) return nullptr;
    return _show->getDroneById(id);
}

void DroneShowController::_readShowJsonFile()
{
    zDebug(DroneShowControllerLog) << "1. read show json file";

    QString showJsonPath = ZSMApplication::applicationDir() + _showZDSFileDir + "/" + _showJsonName;
    QString errorStr;
    QJsonObject jsonObject = ZSMJsonHelper::openShowJsonFile(showJsonPath, errorStr);

    if (!errorStr.isEmpty()) {
        zWarning(DroneShowControllerLog) << errorStr;
        return;
    }
    if ( jsonObject.isEmpty() ) {
        zWarning(DroneShowControllerLog) << "show json file is empty!";
        return;
    }

    _show = new ShowSpecification(showJsonPath,this);
    _show->readFromJson(jsonObject);

    // TODO only load success we can set loadShow to true
    _loadShow = true;
    emit isLoadShowChanged(_loadShow);

    // Queue bounded batches so a large show does not flood the backend with one task per drone.
    constexpr int kVehicleBatchSize = 256;
    std::vector<int> vehicleIds;
    vehicleIds.reserve(kVehicleBatchSize);
    const auto enqueueBatch = [&vehicleIds]() {
        if (vehicleIds.empty()) return;
        zsmApp()->addTask([ids = std::move(vehicleIds)]() {
            for (const int id : ids) MockVehicleManager::instance()->addMockVehicle(id);
        });
        vehicleIds.clear();
        vehicleIds.reserve(kVehicleBatchSize);
    };
    for (int i = 0; i < _show->drones()->count(); i ++) {
        DroneSpecification* drone = _show->drones()->at(i);

        //TODO maybe need do something

        if ( drone->settings()->id == -1 ) {
            zCritical(DroneShowControllerLog) << "vehicle:" << drone->settings()->name << "calc id failed!";
        } else {
            vehicleIds.push_back(drone->settings()->id);
            if (vehicleIds.size() == kVehicleBatchSize) enqueueBatch();
        }
    }
    enqueueBatch();
}
