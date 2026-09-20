#pragma once

#include <QtCore/QtTypes>
#include <QtCore/QMetaType>
#include <qgeocoordinate.h>
#include "MavlinkLib.h"

#include "GPSStruct.h"
#include "BatteryStruct.h"

struct VehicleStruct
{
	// define
	uint16_t vehicleId = 0;
	uint8_t componentId = 0;			///< MAV_STATE
	uint8_t autopilot;					///< MAV_AUTOPILOT
	uint8_t type;						///< MAV_TYPE
	uint8_t system_status;				///< MAV_STATE
	bool commLost{false};

	// status
	// bool armed = false;				///< armed 存储在 base_mode, https://mavlink.io/en/messages/common.html#MAV_MODE_FLAG_SAFETY_ARMED

	/// 现在使用的是 GPS 的
	// QGeoCoordinate  homePosition;		///< 起飞位置
	// QGeoCoordinate  vehiclePosition;	///< 现在的位置
	uint8_t base_mode;					///< MAV_MODE
	uint32_t custom_mode;				///< 具体的 mode
	uint16_t boardVendorId = 0;
	uint16_t boardProductId = 0;
	uint8_t uid2[18]{};

	float yaw = 0;

	GPSStruct gps;
	BatteryStruct battery;
};
Q_DECLARE_METATYPE(VehicleStruct)