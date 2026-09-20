//
// Created by Hoppz on 25-6-29.
//
#pragma once

#include <QtCore/QtTypes>
#include <QtCore/QMetaType>
#include "MavlinkLib.h"
#include <QGeoCoordinate>

struct GPSStruct
{
    double latitude;
    double longitude;
    double altitudeAMSL;
    double altitudeRelative;

    uint8_t fix_type;

    double x;                       ///< x in show
    double y;                       ///< y in show
    double z;                       ///< z in show
};
Q_DECLARE_METATYPE(GPSStruct)