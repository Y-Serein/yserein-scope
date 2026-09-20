#include "ZSMQmlGlobal.h"
#include "DroneShowController.h"
#include "OSMManager.h"
#include "QmlChartLineModel.h"

#include <QQmlEngine>

// ZSMQmlGlobal 单例工厂
static QObject* zsmQmlGlobalSingletonFactory(QQmlEngine*, QJSEngine*)
{
	ZSMQmlGlobal* const zsmGlobal = new ZSMQmlGlobal();
	return zsmGlobal;
}

ZSMQmlGlobal::ZSMQmlGlobal(QObject* parent)
	: QObject(parent)
	, _droneShowController(DroneShowController::instance())
	, _droneShowPlayerController(DroneShowPlayerController::instance())
	, _mockVehicleController(MockVehicleController::instance())
{
}

ZSMQmlGlobal::~ZSMQmlGlobal()
{
}

void ZSMQmlGlobal::registerQmlTypes()
{
	qmlRegisterSingletonType<ZSMQmlGlobal>("Frontend", 1, 0, "Frontend", zsmQmlGlobalSingletonFactory);
	qmlRegisterUncreatableType<QmlObjectListModel>("Frontend", 1, 0, "QmlObjectListModel", "Reference only");
	qmlRegisterUncreatableType<QmlChartLineModel>("Frontend", 1, 0, "QmlChartLineModel", "Reference only");
	qmlRegisterType<OSMManager>("Frontend", 1, 0, "OSMManager");
	qmlRegisterType<CustomTextureData>("Frontend", 1, 0, "CustomTextureData");
}