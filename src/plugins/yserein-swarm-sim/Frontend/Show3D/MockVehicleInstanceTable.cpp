#include "MockVehicleInstanceTable.h"
#include "MockVehicleController.h"

#include <QColor>

MockVehicleInstanceTable::MockVehicleInstanceTable(QQuick3DObject* parent)
    : QQuick3DInstancing(parent)
    , _controller(MockVehicleController::instance())
{
    _refreshTimer.setSingleShot(true);
    _refreshTimer.setInterval(100);
    connect(&_refreshTimer, &QTimer::timeout, this, [this]() {
        _dirty = true;
        markDirty();
    });
    connect(_controller, &MockVehicleController::vehicleSnapshotChanged,
            this, [this]() {
                if (!_refreshTimer.isActive()) _refreshTimer.start();
            });
}

QByteArray MockVehicleInstanceTable::getInstanceBuffer(int* instanceCount)
{
    if (_dirty) {
        const auto positions = _controller->vehiclePositions();
        _instanceData.clear();
        for (const auto& position : positions) {
            const auto entry = calculateTableEntry(
                {position.x() * 100.0f, position.z() * 100.0f, position.y() * 100.0f},
                {1.0f, 1.0f, 1.0f}, {}, QColor(Qt::white), {});
            _instanceData.append(reinterpret_cast<const char*>(&entry), sizeof(entry));
        }
        _instanceCount = static_cast<int>(positions.size());
        _dirty = false;
    }
    if (instanceCount) *instanceCount = _instanceCount;
    return _instanceData;
}
