//
// Created by Hoppz on 25-7-21.
//
#pragma once

#include <QObject>
#include <QVector3D>

#include "VehicleStruct.h"
#include "MockVehicle.h"

class MockVehiclePropertys : public QObject {

    Q_OBJECT
    Q_PROPERTY(int vehicleId        READ vehicleId                                      CONSTANT)
    Q_PROPERTY(int batteryRemaining READ batteryRemaining   WRITE setBatteryRemaining   NOTIFY batteryRemainingChanged)
    Q_PROPERTY(float heading        READ heading            WRITE setHeading            NOTIFY headingChanged)
    Q_PROPERTY(QVector3D position   READ position           WRITE setPosition           NOTIFY positionChanged)
    Q_PROPERTY(bool commLost        READ commLost           WRITE setCommLost              NOTIFY commLostChanged)

    Q_PROPERTY(bool passFilter      READ passFilter                                     NOTIFY passFilterChanged)
    Q_PROPERTY(bool canBeChecked    READ canBeChecked                                   NOTIFY canBeCheckedChanged)

public:
    explicit MockVehiclePropertys(QObject *parent = nullptr);
    explicit MockVehiclePropertys(const VehicleStruct& vehicle, QObject *parent = nullptr);
    ~MockVehiclePropertys();

    void updateFromStrut(const VehicleStruct& other);
    void postTaskToMockVehicleThread(std::function<void(MockVehicle*)> task);

    /// --------------------------------------------- getter
    int vehicleId() const { return _vehicleStruct.vehicleId; }
    int batteryRemaining() const { return _vehicleStruct.battery.battery_remaining; }
    float heading() const { return _vehicleStruct.yaw; }
    QVector3D position() const { return QVector3D(_vehicleStruct.gps.x, _vehicleStruct.gps.y, _vehicleStruct.gps.z); }

    bool commLost() const {return _vehicleStruct.commLost;}
    bool passFilter() const {return _passFilter;}
    bool canBeChecked() const {return _canBeChecked;}

    /// --------------------------------------------- setter
    void setBatteryRemaining(int batteryRemaining);
    void setHeading(int heading);
    void setPosition(QVector3D  position);
    void setCommLost(bool val);

    void setPassFilter(bool passFilter);
    void setCanBeChecked(bool canBeChecked);


signals:
    void batteryRemainingChanged();
    void headingChanged();
    void positionChanged();
    void commLostChanged();

    void passFilterChanged();
    void canBeCheckedChanged();

private:

    void checkPassFilter();

    /// --------------------------------------------- updater
    /// 用于更新 updateFromStruct
    bool _updateBatteryRemaining(int batteryRemaining);
    void _updateHeading(float heading);
    void _updatePosition(float x,float y,float z);
    void _updateCommLost(bool val);

private:

    VehicleStruct _vehicleStruct;

    bool _passFilter = true;
    bool _canBeChecked = true;
};

