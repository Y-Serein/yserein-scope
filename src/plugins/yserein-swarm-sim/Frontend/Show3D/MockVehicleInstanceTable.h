#pragma once

#include <QtQuick3D/qquick3dinstancing.h>
#include <QTimer>

class MockVehicleController;

class MockVehicleInstanceTable : public QQuick3DInstancing
{
    Q_OBJECT

public:
    explicit MockVehicleInstanceTable(QQuick3DObject* parent = nullptr);

protected:
    QByteArray getInstanceBuffer(int* instanceCount) override;

private:
    MockVehicleController* _controller;
    QTimer _refreshTimer;
    QByteArray _instanceData;
    int _instanceCount = 0;
    bool _dirty = true;
};
