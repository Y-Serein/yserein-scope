#pragma once

#include <QtCore/QObject>
#include <QVector3D>
#include <QColor>
#include <QString>

#include "DroneSpecification.h"

/// @brief  每一个 Model 都有一个自己的 SphereModelController
///			用于和 Player 以及 C++ 交互
///			在导入舞步完成后，由 C++ 自动创建，创建前端 Model 时绑定。
class SphereModelController : public QObject
{
	Q_OBJECT

	Q_PROPERTY(QString		name		READ name			CONSTANT)
	Q_PROPERTY(QVector3D	position	READ position		NOTIFY positionChanged)
	Q_PROPERTY(float		x			READ x				NOTIFY positionChanged)
	Q_PROPERTY(float		y			READ y				NOTIFY positionChanged)		//! 高度
	Q_PROPERTY(float		z			READ z				NOTIFY positionChanged)
	Q_PROPERTY(QColor		rawColor	READ rawColor		NOTIFY colorChanged)
	Q_PROPERTY(QVector3D	linearColor	READ linearColor	NOTIFY colorChanged)

public:
	explicit SphereModelController(QString name, DroneSpecification* drone,QObject* parent = nullptr);
	~SphereModelController();

	/// getter ==================================================================

	QString name() const { return _name; }
	QVector3D position() const { return _position; }
	float x() const { return _position.x(); }
	float y() const { return _position.y(); }
	float z() const { return _position.z(); }
	QColor rawColor() const { return _rawColor; }
	QVector3D linearColor() const { return _linearColor; }
	int id() const {return _drone->settings()->id;}

	/// setter ==================================================================

	/// @brief	由 DroneShowPlayerEngine 调用
	/// @param pos unit : m
	void updateTime(qint64 pos);

	/// 从 trajectory 中读取的位置存储到 Model 中
	/// @param position x-y-z 坐标系
	void setPosition(QVector3D position);
	void setColor(QColor color);

signals:
	void positionChanged(QVector3D position);
	/// @brief 要在计算完 linearColor 之后再 emit
	void colorChanged();


private:

	/// @brief	在 color 变化的时候同步计算 linear color, 
	///			emit colorChanged
	void _calcLinearColor();

private:
	QString _name;
	//TODO 整个系统改为 m 为基本单位 （不然几百米的画面拉的太长了。）
	QVector3D _position{0,0,0};			///< unit : cm
	QColor _rawColor{255,0,0,255};
	QVector3D _linearColor{ 0,0,0 };		///< gamma 矫正后的线性色

	static float _brightnessFactor;			///< 灯光亮度的偏移量, 直接在后端完成计算

	DroneSpecification* _drone;
};
