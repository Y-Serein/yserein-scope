#pragma once

#include <QtCore/QObject>
#include <QPointF>
#include <QList>

#include "DroneTypes.h"

#include <libzds/drone/trajectory.h>
#include <libzds/drone/light_program.h>

class DroneSpecification : public QObject
{
	Q_OBJECT

		friend class ShowSpecification;
public:

	explicit DroneSpecification(QObject* parent = nullptr);
	~DroneSpecification();

	/// called when ShowSpecification::readFromJson Completed one Drone
	void readFromJson();

	/// @brief	给定基于起飞时间的已过去时间，返回对应的位置
	/// @param pos	时间点（单位：毫秒）
	/// @return 对应的笛卡尔坐标
	QVector3D seekPointFromTime(qint64 pos);
	QColor seekColorForTime(qint64 pos);
	DroneTypes::DroneSettings_t* settings() { return &_settings; }

	QVector4D seekVelFromTime(qint64 pos);
	QVector4D seekAccelFromTime(qint64 pos);

	void initChartModel(float duration);

	Q_INVOKABLE QList<QPointF> getVelXList() const;

private:
	void _readTrajectoryFromJson(const QJsonObject& json);
	
	/// parse Light ============================================
	void _readLightsFromJson(const QJsonObject& json);

private:

	/// from json
	DroneTypes::DroneType _type;
	DroneTypes::DroneSettings_t _settings;

	/// define in qt 
	QString _trajectoryPath;
	QString _lightsPath;

	// data
	std::unique_ptr<libzds::Trajectory> _trajectory;
	std::unique_ptr<libzds::LightProgram> _lightProgram;

	bool _isLoad{ false };

	QList<std::pair<float, QVector4D>> _velChart;
};

//TODO 测试 u8 数组的解析
/*
const uint8_t testLightProgram[] = {
  1,                            // 1: nop
  2, 0x64,                      // 2: sleep 100 (varuint 100)
  3, 0xc8, 0x01,                // 3: wait until 200 (varuint 200 -> 0xc8 0x01)
  4, 255, 0, 0, 0x32,           // 4: set color (red:255,0,0), duration=50
  5, 128, 0x14,                 // 5: set gray 128, duration=20
  6, 0x0a,                     // 6: set black, duration=10
  7, 0x0a,                     // 7: set white, duration=10
  8, 0, 255, 0, 0x1e,           // 8: fade to color (green:0,255,0), duration=30
  9, 64, 0x0a,                 // 9: fade to gray 64, duration=10
  10, 0x0a,                    // 10: fade to black, duration=10
  11, 0x0a,                    // 11: fade to white, duration=10
  12, 0x02,                    // 12: loop begin, 2 iterations
	4, 0, 0, 255, 0x0a,         //   set color (blue:0,0,255), duration=10
	2, 0x05,                   //   sleep 5
  13,                         // 13: loop end
  20, 0x01,                   // 20: set pyro channel 1 (ignored in logic)
  21, 0x02,                   // 21: set pyro all channel 2 (ignored in logic)
  0                           // 0: end
};




*/