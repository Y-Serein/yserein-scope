#pragma once

#include <QVector3D>
#include <QString>
#include <QList>
#include <QtCore/QMetaType>
#include <QtCore/QTypeInfo>
#include <QQmlEngine>
#include <QColor>
#include <QtCore/qsize.h>

namespace DroneTypes {
	Q_NAMESPACE

/// 无人机的舞步类型。
enum class DroneType {
	Generic
};
Q_ENUM_NS(DroneType)


/// 单个无人机的舞步配置。
struct DroneSettings_t {
	Q_GADGET
	Q_PROPERTY(QString name MEMBER name)
	Q_PROPERTY(QVector3D home MEMBER home)
	Q_PROPERTY(QVector3D landAt MEMBER landAt)
	Q_PROPERTY(int id MEMBER id)
public:
	QString name;						///< 可读的无人机识别名称
	int id;

	//YawControl_t yawControl;			///< 航向角控制（未实现）

	QVector3D home;						///< 起飞位置（由轨迹起始点推断）
	QVector3D landAt;					///< 降落位置
private:

};	// DroneSettings_t



} // namespace DroneTypes

Q_DECLARE_METATYPE(DroneTypes::DroneSettings_t)
