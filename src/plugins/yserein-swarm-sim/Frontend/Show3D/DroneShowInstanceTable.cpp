#include "DroneShowInstanceTable.h"
#include "DroneShowPlayerController.h"
#include "SphereModelController.h"

#include "ZSMLoggingCategory.h"

ZSM_LOGGING_CATEGORY(DroneShowInstanceTableLog, "Frontend.Show3D.DroneShowInstanceTable")

DroneShowInstanceTable::DroneShowInstanceTable(QQuick3DObject* parent)
	: QQuick3DInstancing(parent)
{
	zInfo(DroneShowInstanceTableLog) << "create";
}

DroneShowInstanceTable::~DroneShowInstanceTable()
{
	zInfo(DroneShowInstanceTableLog) << "destory";
}

void DroneShowInstanceTable::setDrones(QmlObjectListModel* drones)
{
    if (_drones) disconnect(_drones, nullptr, this, nullptr);
	_drones = drones;
    if (_drones) {
        connect(_drones, &QmlObjectListModel::countChanged, this, [this]() {
            _dirty = true;
            markDirty();
        });
    }
	emit dronesChanged();
	markDirty();
	_dirty = true;
}

void DroneShowInstanceTable::setPosition(qint64 pos)
{
	if (pos == _position) return;
	_position = pos;
	emit positionChanged();
	markDirty();
	_dirty = true;
}

QByteArray DroneShowInstanceTable::getInstanceBuffer(int* instanceCount)
{
	if (_dirty && _drones) {

		int instanceNumber = 0;
		_instanceData.resize(0);

		for (int i = 0; i < _drones->count(); i++) {
			auto drone = qobject_cast<SphereModelController*>(_drones->get(i));
			//auto entry = calculateTableEntry({ drone->x() , drone->y(),drone->z() },	// position
			auto entry = calculateTableEntry({ drone->x(), drone->y(),drone->z()},	// position
				{ 1.0,1.0,1.0 },									// scale
				{},									// rotation
				drone->rawColor(),						// color
				{});									// customData
			_instanceData.append(reinterpret_cast<const char*>(&entry), sizeof(entry));
			instanceNumber++;

			//zDebug(DroneShowInstanceTableLog) << "color:" << drone->rawColor();
		}

		_dirty = false;
		_instanceCount = instanceNumber;
	}
	if (instanceCount) {
		*instanceCount = _instanceCount;
	}

	return _instanceData;
}
