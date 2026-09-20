//
// Created by Hoppz on 25-6-26.
//
#pragma once

#include <QtCore/QtTypes>
#include <QtCore/QMetaType>
#include "MavlinkLib.h"

struct BatteryStruct
{
    uint8_t id;
    uint8_t battery_function;
    uint8_t type;
    uint16_t temperature;
    uint16_t voltages[10]{};
    int16_t current_battery;
    int32_t current_consumed;
    int32_t energy_consumed;
    int8_t battery_remaining;
    int32_t time_remaining;
    uint8_t charge_state;
    uint16_t voltages_ext[4]{};
    uint8_t mode;
    uint32_t fault_bitmask;
    mavlink_battery_status_t battery_status;
};
Q_DECLARE_METATYPE(BatteryStruct)