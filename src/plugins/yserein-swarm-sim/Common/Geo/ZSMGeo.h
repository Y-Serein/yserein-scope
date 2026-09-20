#pragma once

#include <QtPositioning/QGeoCoordinate>

namespace ZSMGeo
{
    QGeoCoordinate convertXYZToGeoFromOrigin(const double& x, const double& y, const double& z, const QGeoCoordinate& origin);

}

