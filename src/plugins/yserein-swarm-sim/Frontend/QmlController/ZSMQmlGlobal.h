#pragma once

#include <QtCore/QObject>

/// 作为 Q_PROPERTY 的类型必须是完整的定义
#include "DroneShowController.h"
#include "DroneShowPlayerController.h"
#include "MockVehicleController.h"

class ZSMQmlGlobal : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString						appName						READ appName					CONSTANT)
	Q_PROPERTY(DroneShowController*			droneShowController			READ droneShowController		CONSTANT)
	Q_PROPERTY(DroneShowPlayerController*	droneShowPlayerController	READ droneShowPlayerController	CONSTANT)
	Q_PROPERTY(MockVehicleController*		mockVehicleController		READ mockVehicleController		CONSTANT)
public:

	ZSMQmlGlobal(QObject* parent = nullptr);
	~ZSMQmlGlobal();

	static void registerQmlTypes();

	/// property getter ==================================================================
	DroneShowController* droneShowController() { return _droneShowController; }
	DroneShowPlayerController* droneShowPlayerController() { return _droneShowPlayerController; }
	MockVehicleController* mockVehicleController() {return _mockVehicleController;}
	QString appName() const { return _appName; }

private:

	DroneShowController* _droneShowController = nullptr;
	DroneShowPlayerController* _droneShowPlayerController = nullptr;
	MockVehicleController* _mockVehicleController = nullptr;

	
	static constexpr const char* _appName = "YSerein Swarm Simulator";
};
