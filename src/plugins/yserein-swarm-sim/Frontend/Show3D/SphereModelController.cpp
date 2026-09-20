#include "SphereModelController.h"
#include "ZSMLoggingCategory.h"
#include <QtMath>

ZSM_LOGGING_CATEGORY(SphereModelControllerLog, "Frontend.DroneShow.SphereModelController")

//TODO Settings or get in a singleton C++ class , such as DroneShowPlayerController
float SphereModelController::_brightnessFactor = 1.7;

SphereModelController::SphereModelController(QString name,DroneSpecification* drone, QObject* parent)
	: _name(name)
	, _drone(drone)
	, QObject(parent)
{
	zInfo(SphereModelControllerLog) << "create" << _name;

	setPosition(_drone->settings()->home);
	_calcLinearColor();
}

SphereModelController::~SphereModelController()
{
	zInfo(SphereModelControllerLog) << "destory" << _name;
}

void SphereModelController::setColor(QColor color)
{
	if (color == _rawColor) return;

	_rawColor = color;
	_calcLinearColor();
}

void SphereModelController::updateTime(qint64 pos)
{
	setPosition(_drone->seekPointFromTime(pos));
	setColor(_drone->seekColorForTime(pos));
}

void SphereModelController::setPosition(QVector3D position)
{
	// 统一单位
	// View3D 的单位我们设定 1 -> 1cm
	// 而 舞步是以 m 为单位
	QVector3D trans{ position.x() * 100, position.z()* 100,position.y() * 100};

	if (trans == _position) return;
	_position = trans;

	emit positionChanged(_position);
}

void SphereModelController::_calcLinearColor()
{
	// 由 setColor 来检查是否和上一个颜色相同
	_linearColor.setX(qPow(_rawColor.redF(), 2.2) * _brightnessFactor);
	_linearColor.setY(qPow(_rawColor.greenF(), 2.2) * _brightnessFactor);
	_linearColor.setZ(qPow(_rawColor.blueF(), 2.2) * _brightnessFactor);

	emit colorChanged();
}
