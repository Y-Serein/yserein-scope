#pragma once

#include <QtQuick3D/qquick3dinstancing.h>

#include "QmlObjectListModel.h"

class DroneShowInstanceTable : public QQuick3DInstancing
{
	Q_OBJECT
	QML_ELEMENT
	
	Q_PROPERTY(QmlObjectListModel*	drones		READ drones		WRITE setDrones		NOTIFY dronesChanged)
	Q_PROPERTY(qint64				position	READ position	WRITE setPosition	NOTIFY positionChanged)

public:
	explicit DroneShowInstanceTable(QQuick3DObject* parent = nullptr);
	~DroneShowInstanceTable() override;

	/// getter
	QmlObjectListModel* drones() { return _drones;}
	qint64 position() const { return _position; }

	/// setter
	void setDrones(QmlObjectListModel* drones);
	
	/// @brief 以 set position 作为更新 instanceBuffer 的 trigger
	void setPosition(qint64 pos);

signals:
	void dronesChanged();
	void positionChanged();

protected:
	QByteArray getInstanceBuffer(int* instanceCount) override;

private:
	int _instanceCount{ 0 };
	QByteArray _instanceData;
	bool _dirty{ true };
	QmlObjectListModel* _drones{nullptr};
	qint64 _position{0};
};