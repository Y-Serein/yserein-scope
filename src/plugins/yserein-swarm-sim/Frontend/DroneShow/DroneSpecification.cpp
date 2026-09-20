#include "DroneSpecification.h"
#include "ZSMLoggingCategory.h"
#include "ZSMJsonHelper.h"
#include "QJsonArray"

#include <algorithm>
#include <iostream>

ZSM_LOGGING_CATEGORY(DroneSpecificationLog, "Frontend.DroneShow.DroneSpecification")

DroneSpecification::DroneSpecification(QObject* parent)
	: QObject(parent)
{
	//zInfo(DroneSpecificationLog) << "create";
}

DroneSpecification::~DroneSpecification()
{
	//zInfo(DroneSpecificationLog) << "destory";
}

void DroneSpecification::readFromJson()
{
	//zDebug(DroneSpecificationLog) << "traj path :" << _trajectoryPath;
	//zDebug(DroneSpecificationLog) << "lights path :" << _lightsPath;

	_lightProgram.reset();
	_trajectory.reset();

	_lightProgram = std::make_unique<libzds::LightProgram>();
	_trajectory = std::make_unique<libzds::Trajectory>();


	// read trajectory

	QString errorStr;
	QJsonObject trajectoryJson = ZSMJsonHelper::openShowJsonFile(_trajectoryPath,errorStr);

	if (!errorStr.isEmpty()) {
		zWarning(DroneSpecificationLog) << errorStr;
		return;
	}
	if (trajectoryJson.isEmpty()) {
		zWarning(DroneSpecificationLog) << "trajectory json file is empty!";
		return;
	}
	_readTrajectoryFromJson(trajectoryJson);

	//  read lights

	QJsonObject lightsJson = ZSMJsonHelper::openShowJsonFile(_lightsPath, errorStr);
	if (!errorStr.isEmpty()) {
		zWarning(DroneSpecificationLog) << errorStr;
		return;
	}
	if (lightsJson.isEmpty()) {
		zWarning(DroneSpecificationLog) << "lights json file is empty!";
		return;
	}
	_readLightsFromJson(lightsJson);

	// init chart
	initChartModel(_trajectory->landTime());

	_isLoad = true;	
}

QVector3D DroneSpecification::seekPointFromTime(qint64 pos)
{
	//TODO 判断无效
	if (!_isLoad) return QVector3D();

	float pos_s = pos / 1000.0;

	libzds::Vector4D loc;
	libzds::Status s =_trajectory->getPositionAt(pos_s, loc);

	if ( !s.ok() ) {
		zWarning(DroneSpecificationLog) << s.ToString();
		libzds::Vector3D to = _trajectory->takeoffAt();
		return {to.x, to.y, to.z};
	}

	return {loc.x, loc.y, loc.z};
}

QColor DroneSpecification::seekColorForTime(qint64 pos)
{
	if (!_isLoad) return QColor(0, 0, 0);

	libzds::Color libzdsColor;

	libzds::Status s = _lightProgram->getColorAt(pos, libzdsColor);

	if ( !s.ok() ) {
		zWarning(DroneSpecificationLog) << s.ToString();
		return QColor(255, 0, 0);
	}

	if ( libzdsColor.r == 0 && libzdsColor.g == 0 && libzdsColor.b == 0 ) {
		libzdsColor.r = 255;
	}

	return {libzdsColor.r, libzdsColor.g, libzdsColor.b};
}

QVector4D DroneSpecification::seekVelFromTime(qint64 pos)
{
	float pos_s = pos / 1000.0;
	libzds::Vector4D vel;
	libzds::Status s = _trajectory->getVelocityAt(pos_s, vel);
	if ( !s.ok() ) {
		return {};
	}  else {
		return {vel.x, vel.y, vel.z, vel.w};
	}
}

QVector4D DroneSpecification::seekAccelFromTime(qint64 pos)
{
	float pos_s = pos / 1000.0;
	libzds::Vector4D accel;
	libzds::Status s = _trajectory->getAccelAt(pos_s, accel);
	if ( !s.ok() ) {
		return {};
	} else {
		return {accel.x, accel.y, accel.z, accel.w};
	}
}

void DroneSpecification::initChartModel(float duration)
{

	_velChart.clear();

	int range = duration * 10;

	_velChart.reserve(range);
	for (int i = 0; i < range; i++) {
		int seekTime = i * 100;
		float time = i / 10.f;
		QVector4D vel = seekVelFromTime(seekTime);

		_velChart.emplace_back(time, vel);
	}
}

QList<QPointF> DroneSpecification::getVelXList() const
{
	if ( _velChart.isEmpty() ) return {};

	QList<QPointF> list;
	list.reserve(_velChart.size());
	for (const auto& [time, vec] : _velChart) {
		// if (   ) {
			list.push_back({time, vec.x()});
		// }

	}

	return list;
}

void DroneSpecification::_readTrajectoryFromJson(const QJsonObject& trajectoryObject)
{
	QJsonArray array = trajectoryObject["points"].toArray();

    if( array.count() <= 0 ) {
        zWarning(DroneSpecificationLog) <<"drone no data in [points], init failed";
        return ;
    }

    //! 所有轨迹段的时间要基于此进行偏移
    float takeoffTime = static_cast<float>(trajectoryObject["takeoffTime"].toDouble());
    float landTime = static_cast<float>(trajectoryObject["landingTime"].toDouble());

    // 预分配内存
	std::vector<libzds::TrajectoryInit> initSeg;
	initSeg.reserve(array.size());

	float last_time  = 0.f;
	int i = 0;
	libzds::Vector3D last_point;

	QJsonArray first_point = array[0].toArray();
	last_point = libzds::Vector3D(static_cast<float>(first_point[1][0].toDouble()),
		static_cast<float>(first_point[1][1].toDouble()),
		static_cast<float>(first_point[1][2].toDouble()));

	if ( takeoffTime > 0.f) {
		i = 0;
	}
	else if (takeoffTime == 0) {
		i = 1;
	}
	else {
        zCritical(DroneSpecificationLog) << "takeoffTime < 0, init failed";
		return;
	}

	while( i < array.size() ) {
		QJsonArray point = array[i].toArray();
		float end_time = point[0].toDouble() + takeoffTime;
		QJsonArray end_point = point[1].toArray();
		QJsonArray control_points = point[2].toArray();

		// zDebug(DroneSpecificationLog)<< "time:" << end_time << "duration:" << end_time - last_time;

		if (end_point.size() != 3) {
			zWarning(DroneSpecificationLog) << "error point format.";
			return;
		}

		initSeg.emplace_back(libzds::TrajectoryInit{});
		auto& seg = initSeg.back();
		seg.startTime = last_time;
		seg.endTime = end_time;

		last_time = end_time;

		seg.points.reserve(2 + control_points.size());

		libzds::Vector3D now_point = libzds::Vector3D(static_cast<float>(end_point[0].toDouble()),
			static_cast<float>(end_point[1].toDouble()),
			static_cast<float>(end_point[2].toDouble()));

		seg.points.push_back(last_point);

		for (auto && j : control_points) {
			QJsonArray control_point = j.toArray();
			if (control_point.size() != 3) {
				zWarning(DroneSpecificationLog) << "error point format.";
				return;
			}

			seg.points.emplace_back(static_cast<float>(control_point[0].toDouble()),
			static_cast<float>(control_point[1].toDouble()),
			static_cast<float>(control_point[2].toDouble()));
		}

		seg.points.push_back(now_point);
		last_point = now_point;
		i++;
	}

	libzds::Status s = _trajectory->init(landTime, initSeg);
	if ( !s.ok() ) {
		zWarning(DroneSpecificationLog) << s.ToString();
		return ;
	}


}

void DroneSpecification::_readLightsFromJson(const QJsonObject& json)
{
	// _settings.lights()->version = json["version"].toInt();
	// _settings.lights()->data = json["data"].toString();

	QString dataStr = json["data"].toString();

	// 以 data 生成 LightSegment_t
	QByteArray u8data = QByteArray::fromBase64(dataStr.toUtf8());

	libzds::Status s = _lightProgram->init(u8data.constData(), u8data.size());
	if ( !s.ok() ) {
		zWarning(DroneSpecificationLog) << s.ToString()
			<< "- falling back to a no-op light program";

		// Some production show files contain light opcodes that this bundled
		// libzds version does not support. Keep trajectory playback usable and
		// prevent getColorAt() from flooding the log with "not init" errors.
		_lightProgram = std::make_unique<libzds::LightProgram>();
		constexpr char neutralLightProgram[] = { 1, 0 }; // nop, end
		s = _lightProgram->init(neutralLightProgram, sizeof(neutralLightProgram));
		if ( !s.ok() ) {
			zWarning(DroneSpecificationLog) << s.ToString();
		}
	}
}


