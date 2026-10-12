//
// Created by Hoppz on 25-7-21.
//
#pragma once

#include <QObject>
#include <QLoggingCategory>
#include <QElapsedTimer>
#include <QTimer>

#include <vector>
#include <unordered_map>

#include "MockVehiclePropertys.h"
#include "BoxDancePlan.h"

Q_DECLARE_LOGGING_CATEGORY(MockVehicleControllerLog)

class MockVehiclePropertys;

class MockVehicleController : public QObject
{
    Q_OBJECT

    /// ------------------------------------------------------ detail

    Q_PROPERTY(MockVehiclePropertys*  detailVehicle     READ detailVehicle      NOTIFY detailVehicleChanged)
    Q_PROPERTY(int                    detailVehicleId   READ detailVehicleId    NOTIFY detailVehicleIdChanged)

    Q_PROPERTY(bool clearing READ clearing NOTIFY clearingChanged)
    Q_PROPERTY(int count                    READ    count                   NOTIFY countChanged)
    Q_PROPERTY(float sceneRadius            READ    sceneRadius             NOTIFY sceneRadiusChanged)
    Q_PROPERTY(float previewRadius          READ    previewRadius           NOTIFY previewRadiusChanged)
    Q_PROPERTY(int activeProtocolDomain     READ activeProtocolDomain     CONSTANT)
    Q_PROPERTY(int configuredProtocolDomain READ configuredProtocolDomain NOTIFY configuredProtocolDomainChanged)
    Q_PROPERTY(int boxRows READ boxRows WRITE setBoxRows NOTIFY boxLayoutChanged)
    Q_PROPERTY(int boxCols READ boxCols WRITE setBoxCols NOTIFY boxLayoutChanged)
    Q_PROPERTY(int danceMinLayers READ danceMinLayers NOTIFY dancePlanChanged)
    Q_PROPERTY(int danceMaxLayers READ danceMaxLayers NOTIFY dancePlanChanged)
    Q_PROPERTY(bool danceCanPlay READ danceCanPlay NOTIFY dancePlanChanged)
    Q_PROPERTY(int dancePlaybackState READ dancePlaybackState NOTIFY dancePlaybackStateChanged)
    Q_PROPERTY(qint64 dancePosition READ dancePosition WRITE setDancePosition NOTIFY dancePositionChanged)
    Q_PROPERTY(qint64 danceDuration READ danceDuration NOTIFY danceDurationChanged)
    Q_PROPERTY(qint64 danceLaunchDuration READ danceLaunchDuration NOTIFY danceDurationChanged)
    Q_PROPERTY(qreal dancePlaybackRate READ dancePlaybackRate WRITE setDancePlaybackRate NOTIFY dancePlaybackRateChanged)
    Q_PROPERTY(bool danceViewActive READ danceViewActive WRITE setDanceViewActive NOTIFY danceViewActiveChanged)
    Q_PROPERTY(bool dancePrepared READ dancePrepared NOTIFY dancePreparedChanged)

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

    bool clearing() const { return _clearing; }
    int count() const { return _vehicleIdMap.size(); }
    float sceneRadius() const { return _sceneRadius; }
    float previewRadius() const { return _dancePrepared && _danceViewActive ? _danceRadius : _sceneRadius; }
    std::vector<QVector3D> vehiclePositions() const;
    int activeProtocolDomain() const;
    int configuredProtocolDomain() const;
    int boxRows() const { return _dancePlan.rows; }
    int boxCols() const { return _dancePlan.cols; }
    int danceMinLayers() const { return count() / _dancePlan.boxCount(); }
    int danceMaxLayers() const { return (count() + _dancePlan.boxCount() - 1) / _dancePlan.boxCount(); }
    bool danceCanPlay() const { return !_clearing && count() >= _dancePlan.boxCount(); }
    int dancePlaybackState() const { return _dancePlaybackState; }
    qint64 dancePosition() const { return _dancePosition; }
    qint64 danceDuration() const;
    qint64 danceLaunchDuration() const;
    qreal dancePlaybackRate() const { return _dancePlaybackRate; }
    bool danceViewActive() const { return _danceViewActive; }
    bool dancePrepared() const { return _dancePrepared; }
    bool batteryPFilterEnabled() const {return _batteryPFilterEnabled;}
    int batteryPFilterVal() const {return _batteryPFilterVal;}

    Q_INVOKABLE MockVehiclePropertys* getVehiclePropertys(int vehicleId);

    /// ------------------------------------------------------- function

    /// @brief 一键生成 rows x cols 网格编队车辆 (间距 spacing 米, 自动编号)
    Q_INVOKABLE void clearVehicles();
    void finishClearingVehicles();
    Q_INVOKABLE void generateGridVehicles(int rows, int cols, double spacing);
    Q_INVOKABLE void setConfiguredProtocolDomain(int domain);
    Q_INVOKABLE void setBoxRows(int rows);
    Q_INVOKABLE void setBoxCols(int cols);
    Q_INVOKABLE void playDance();
    Q_INVOKABLE void pauseDance();
    Q_INVOKABLE void stopDance();
    Q_INVOKABLE void setDancePosition(qint64 position);
    Q_INVOKABLE void setDancePlaybackRate(qreal rate);
    Q_INVOKABLE void setDanceViewActive(bool active);

    /// ------------------------------------------------------- setter

    Q_INVOKABLE void setDetailVehicle(int vehicleId);

public slots:
    void updataVehicleSnapshot(const std::vector<VehicleStruct>& list);

signals:
    void clearingChanged();
    void vehiclesCleared();
    void countChanged();
    void sceneRadiusChanged();
    void previewRadiusChanged();
    void vehicleSnapshotChanged();
    void configuredProtocolDomainChanged();
    void boxLayoutChanged();
    void dancePlanChanged();
    void dancePlaybackStateChanged();
    void dancePositionChanged();
    void danceDurationChanged();
    void dancePlaybackRateChanged();
    void danceViewActiveChanged();
    void dancePreparedChanged();
    void batteryPFilterEnabledChanged();
    void batteryPFilterValChanged();

    void detailVehicleIdChanged();
    void detailVehicleChanged();

private:
    std::unordered_map<int/* vehicleId */,MockVehiclePropertys*> _vehicleIdMap;
    bool _clearing = false;
    float _sceneRadius = 0.0f;
    float _danceRadius = 0.0f;
    BoxDancePlan _dancePlan;
    std::vector<int> _danceIds;
    std::vector<QVector3D> _dancePositions;
    QTimer _danceTimer;
    QElapsedTimer _danceClock;
    QElapsedTimer _backendClock;
    qint64 _dancePosition = 0;
    qreal _dancePlaybackRate = 1.0;
    int _dancePlaybackState = 0; // 0 stopped, 1 playing, 2 paused
    bool _dancePrepared = false;
    bool _danceViewActive = true;

    void _prepareDance();
    void _sampleDance();
    void _pushDanceToBackend();
    void _tickDance();
    void _onShowLoad(bool loaded);

    // filter
    bool _batteryPFilterEnabled = false;         ///< 是否开启电池百分比过滤
    int _batteryPFilterVal = 98;                ///< 大于 val 才显示

    MockVehiclePropertys* _detailVehicle = nullptr;
    int _detailVehicleId = -1;
};
