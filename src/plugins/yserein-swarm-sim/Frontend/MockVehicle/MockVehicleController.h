//
// Created by Hoppz on 25-7-21.
//
#pragma once

#include <QObject>
#include <QLoggingCategory>

#include <vector>
#include <unordered_map>

#include "MockVehiclePropertys.h"

Q_DECLARE_LOGGING_CATEGORY(MockVehicleControllerLog)

class MockVehiclePropertys;

class MockVehicleController : public QObject
{
    Q_OBJECT

    /// ------------------------------------------------------ detail

    Q_PROPERTY(MockVehiclePropertys*  detailVehicle     READ detailVehicle      NOTIFY detailVehicleChanged)
    Q_PROPERTY(int                    detailVehicleId   READ detailVehicleId    NOTIFY detailVehicleIdChanged)

    Q_PROPERTY(int count                    READ    count                   NOTIFY countChanged)

    /// ------------------------------------------------------ filter
    Q_PROPERTY(bool batteryPFilterEnabled   READ    batteryPFilterEnabled   NOTIFY batteryPFilterEnabledChanged)
    Q_PROPERTY(int  batteryPFilterVal       READ    batteryPFilterVal       NOTIFY batteryPFilterValChanged)


public:
    explicit MockVehicleController(QObject* parent = nullptr);
    ~MockVehicleController();

    static MockVehicleController* instance();
    static void registerQmlTypes();
    void init();

    /// ------------------------------------------------------ getter

    MockVehiclePropertys* detailVehicle() const {return _detailVehicle;}
    int detailVehicleId() const {return _detailVehicleId;}

    int count() const { return _vehicleIdMap.size(); }
    bool batteryPFilterEnabled() const {return _batteryPFilterEnabled;}
    int batteryPFilterVal() const {return _batteryPFilterVal;}

    Q_INVOKABLE MockVehiclePropertys* getVehiclePropertys(int vehicleId);

    /// ------------------------------------------------------- function

    /// @brief 一键生成 rows x cols 网格编队车辆 (间距 spacing 米, 自动编号)
    Q_INVOKABLE void generateGridVehicles(int rows, int cols, double spacing);

    /// ------------------------------------------------------- setter

    Q_INVOKABLE void setDetailVehicle(int vehicleId);

public slots:
    void updataVehicleSnapshot(const std::vector<VehicleStruct>& list);

signals:
    void countChanged();
    void batteryPFilterEnabledChanged();
    void batteryPFilterValChanged();

    void detailVehicleIdChanged();
    void detailVehicleChanged();

private:
    std::unordered_map<int/* vehicleId */,MockVehiclePropertys*> _vehicleIdMap;

    // filter
    bool _batteryPFilterEnabled = false;         ///< 是否开启电池百分比过滤
    int _batteryPFilterVal = 98;                ///< 大于 val 才显示

    MockVehiclePropertys* _detailVehicle = nullptr;
    int _detailVehicleId = -1;
};