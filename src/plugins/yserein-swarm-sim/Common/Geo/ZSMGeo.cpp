#include "ZSMGeo.h"
#include "ZSMLoggingCategory.h"
#include "../ZSMLoggingCategory.h"

#include <cmath>

ZSM_LOGGING_CATEGORY(ZSMGeoLog, "Common.Geo.ZSMGeo")

namespace
{
constexpr double kGeodesicWGS84_a = 6378137.0;
constexpr double kGeodesicWGS84_f = 1.0 / 298.257223563;
constexpr double kGeodesicWGS84_e2 = kGeodesicWGS84_f * (2.0 - kGeodesicWGS84_f);

// 原点局部东-北-天(ENU)坐标转换为 WGS84 大地坐标。
// 相比原始实现的 GeographicLib::LocalCartesian，这里用标准的
// 椭球 ECEF 转换替代，自包含以便作为 Scope 插件独立构建，不再依赖
// 外部 GeographicLib 预编译库。对模拟器几十米到数公里的编队场景，
// 两者精度差异可忽略。
}

QGeoCoordinate ZSMGeo::convertXYZToGeoFromOrigin(const double &x, const double &y, const double &z,
	const QGeoCoordinate &origin)
{
	const double lat0 = origin.latitude();
	const double lon0 = origin.longitude();
	const double alt0 = origin.altitude();

	// 原点 -> 地心地固(ECEF)
	const double sinLat0 = std::sin(lat0 * M_PI / 180.0);
	const double cosLat0 = std::cos(lat0 * M_PI / 180.0);
	const double sinLon0 = std::sin(lon0 * M_PI / 180.0);
	const double cosLon0 = std::cos(lon0 * M_PI / 180.0);

	const double N0 = kGeodesicWGS84_a / std::sqrt(1.0 - kGeodesicWGS84_e2 * sinLat0 * sinLat0);
	const double X0 = (N0 + alt0) * cosLat0 * cosLon0;
	const double Y0 = (N0 + alt0) * cosLat0 * sinLon0;
	const double Z0 = (N0 * (1.0 - kGeodesicWGS84_e2) + alt0) * sinLat0;

	// ENU -> ECEF 旋转
	const double dX = -sinLon0 * x - sinLat0 * cosLon0 * y + cosLat0 * cosLon0 * z;
	const double dY =  cosLon0 * x - sinLat0 * sinLon0 * y + cosLat0 * sinLon0 * z;
	const double dZ =  cosLat0 * y + sinLat0 * z;

	const double X = X0 + dX;
	const double Y = Y0 + dY;
	const double Z = Z0 + dZ;

	// ECEF -> 大地坐标(迭代)
	double lon = std::atan2(Y, X);
	const double p = std::sqrt(X * X + Y * Y);
	if (p < 1e-9) {
		// 极点附近：经度未定义，直接返回原点
		return origin;
	}
	if (std::fabs(Z) < 1e-9) {
		return QGeoCoordinate(0.0, lon * 180.0 / M_PI, p - kGeodesicWGS84_a);
	}

	double lat = std::atan2(Z, p * (1.0 - kGeodesicWGS84_e2));
	double alt = 0.0;
	for (int i = 0; i < 8; ++i) {
		const double sinLat = std::sin(lat);
		const double N = kGeodesicWGS84_a / std::sqrt(1.0 - kGeodesicWGS84_e2 * sinLat * sinLat);
		alt = p / std::cos(lat) - N;
		lat = std::atan2(Z, p * (1.0 - kGeodesicWGS84_e2 * N / (N + alt)));
	}

	return QGeoCoordinate(lat * 180.0 / M_PI, lon * 180.0 / M_PI, alt);
}
