//
// Created by Hoppz on 25-7-21.
//

#include "MockVehicleController.h"
#include "ZSMLoggingCategory.h"

#include "MockVehicleManager.h"
#include "ZSMBackend.h"
#include "MavlinkProtocol.h"
#include "DroneShowController.h"
#include "DroneShowPlayerController.h"

#include <qapplicationstatic.h>
#include <QtQml/QQmlEngine>
#include <algorithm>
#include <cmath>
#include <utility>

Q_APPLICATION_STATIC(MockVehicleController, _mockVehicleControllerInstancer)
ZSM_LOGGING_CATEGORY(MockVehicleControllerLog, "Frontend.MockVehicle.MockVehicleController")

MockVehicleController::MockVehicleController(QObject *parent)
    : QObject(parent)
{
    _danceTimer.setInterval(100);
    connect(&_danceTimer, &QTimer::timeout, this, &MockVehicleController::_tickDance);
}

MockVehicleController::~MockVehicleController()
{
}

MockVehicleController * MockVehicleController::instance()
{
    return _mockVehicleControllerInstancer();
}

void MockVehicleController::registerQmlTypes()
{
}

void MockVehicleController::init()
{
    qmlRegisterUncreatableType<MockVehicleController>("Frontend.Vehicle", 1, 0, "MockVehicleController", "Reference only");
    qmlRegisterUncreatableType<MockVehiclePropertys>("Frontend.Vehicle", 1, 0, "MockVehiclePropertys", "Reference only");
    connect(DroneShowController::instance(), &DroneShowController::isLoadShowChanged,
            this, &MockVehicleController::_onShowLoad, Qt::UniqueConnection);
}

std::vector<QVector3D> MockVehicleController::vehiclePositions() const
{
    if (_dancePrepared) return _dancePositions;
    std::vector<QVector3D> positions;
    positions.reserve(_vehicleIdMap.size());
    for (const auto& [id, vehicle] : _vehicleIdMap) {
        positions.push_back(vehicle->position());
    }
    return positions;
}

MockVehiclePropertys * MockVehicleController::getVehiclePropertys(int vehicleId)
{
    auto it = _vehicleIdMap.find(vehicleId);
    if (it == _vehicleIdMap.end()) return nullptr;
    return it->second;
}

void MockVehicleController::setDetailVehicle(int vehicleId)
{
    auto it = _vehicleIdMap.find(vehicleId);
    if ( it == _vehicleIdMap.end() ) return;

    MockVehiclePropertys * vehicle = it->second;

    if ( vehicleId == _detailVehicleId ) {
        // 取消 detail
        _detailVehicleId = -1;
        _detailVehicle = nullptr;
    } else {
        _detailVehicleId = vehicleId;

        if ( _detailVehicleId == -1 )   _detailVehicle = nullptr;
        else                            _detailVehicle = vehicle;
    }

    qInfo(MockVehicleControllerLog) << "VehicleId:" << _detailVehicleId << _detailVehicle;

    emit detailVehicleChanged();
    emit detailVehicleIdChanged();
}

int MockVehicleController::activeProtocolDomain() const
{
    return static_cast<int>(zsm::currentProtocolDomain());
}

int MockVehicleController::configuredProtocolDomain() const
{
    return static_cast<int>(zsm::configuredProtocolDomain());
}

void MockVehicleController::setConfiguredProtocolDomain(int domain)
{
    if (domain < 0 || domain > 1 || domain == configuredProtocolDomain()) return;
    zsm::configureProtocolDomainForNextStart(static_cast<zsm::ProtocolDomain>(domain));
    emit configuredProtocolDomainChanged();
}

void MockVehicleController::clearVehicles()
{
    if (_clearing) return;
    _clearing = true;
    emit clearingChanged();
    emit dancePlanChanged();
    _danceTimer.stop();
    _dancePlaybackState = 0;
    emit dancePlaybackStateChanged();
    DroneShowController::instance()->clearShow();
    zsmApp()->addTask([]() { MockVehicleManager::instance()->deleteAllMockVehicles(); });
}

void MockVehicleController::finishClearingVehicles()
{
    if (!_clearing) return;
    // All workers acknowledged destruction, so no old vehicle can enqueue new player work.
    DroneShowPlayerController::instance()->discardVehicleTasks();
    _detailVehicle = nullptr;
    _detailVehicleId = -1;
    emit detailVehicleChanged();
    emit detailVehicleIdChanged();
    for (const auto& [id, vehicle] : _vehicleIdMap) vehicle->deleteLater();
    _vehicleIdMap.clear();
    _danceIds.clear();
    _dancePositions.clear();
    _dancePlan.aircraftCount = 0;
    _dancePrepared = false;
    _dancePosition = 0;
    _sceneRadius = 0.0f;
    _danceRadius = 0.0f;
    _backendClock.invalidate();
    setDanceViewActive(true);
    _clearing = false;
    emit countChanged();
    emit sceneRadiusChanged();
    emit previewRadiusChanged();
    emit dancePreparedChanged();
    emit dancePositionChanged();
    emit danceDurationChanged();
    emit dancePlanChanged();
    emit vehicleSnapshotChanged();
    emit clearingChanged();
    emit vehiclesCleared();
    zInfo(MockVehicleControllerLog) << "All simulated vehicles cleared";
}

void MockVehicleController::generateGridVehicles(int rows, int cols, double spacing)
{
    if (_clearing) return;
    if ( rows <= 0 || cols <= 0 ) {
        zWarning(MockVehicleControllerLog) << "generateGridVehicles invalid rows/cols:" << rows << cols;
        return;
    }
    zsmApp()->addTask([rows, cols, spacing]() {
        MockVehicleManager::instance()->generateGridMockVehicles(rows, cols, spacing);
    });
}

void MockVehicleController::updataVehicleSnapshot(const std::vector<VehicleStruct>& list)
{
    if (_clearing) return;
    bool countIncreased = false;
    bool radiusIncreased = false;
    for (const auto& vehicleStruct : list) {
        const float radius = static_cast<float>(std::max({
            std::abs(vehicleStruct.gps.x),
            std::abs(vehicleStruct.gps.y),
            std::abs(vehicleStruct.gps.z)}));
        if (radius > _sceneRadius) {
            _sceneRadius = radius;
            radiusIncreased = true;
        }
        auto it = _vehicleIdMap.find(vehicleStruct.vehicleId);

        if ( it == _vehicleIdMap.end() ) {
            MockVehiclePropertys* te_vehicle = new MockVehiclePropertys(vehicleStruct, this);
            _vehicleIdMap[vehicleStruct.vehicleId] = te_vehicle;
            countIncreased = true;
        } else {
            it->second->updateFromStrut(vehicleStruct);
        }
    }
    if (countIncreased) {
        if (_dancePrepared) {
            _danceTimer.stop();
            _dancePrepared = false;
            _dancePlaybackState = 0;
            _dancePosition = 0;
            emit dancePlaybackStateChanged();
            emit dancePositionChanged();
            emit dancePreparedChanged();
            emit previewRadiusChanged();
        }
        emit countChanged();
        emit dancePlanChanged();
        emit danceDurationChanged();
    }
    if (radiusIncreased) {
        emit sceneRadiusChanged();
        if (!_dancePrepared) emit previewRadiusChanged();
    }
    if (!list.empty()) emit vehicleSnapshotChanged();
}

qint64 MockVehicleController::danceDuration() const
{
    BoxDancePlan plan = _dancePlan;
    plan.aircraftCount = count();
    return static_cast<qint64>(std::ceil(plan.durationSeconds() * 1000.0));
}

qint64 MockVehicleController::danceLaunchDuration() const
{
    BoxDancePlan plan = _dancePlan;
    plan.aircraftCount = count();
    return static_cast<qint64>(std::ceil(plan.launchSeconds() * 1000.0));
}

void MockVehicleController::setBoxRows(int rows)
{
    if (rows < 1 || rows > 100 || rows == _dancePlan.rows || _dancePlaybackState == 1) return;
    _dancePlan.rows = rows;
    if (_dancePrepared) {
        _dancePrepared = false;
        _dancePlaybackState = 0;
        _dancePosition = 0;
        emit dancePlaybackStateChanged();
        emit dancePositionChanged();
        emit dancePreparedChanged();
        emit previewRadiusChanged();
    }
    emit boxLayoutChanged();
    emit dancePlanChanged();
    emit danceDurationChanged();
}

void MockVehicleController::setBoxCols(int cols)
{
    if (cols < 1 || cols > 100 || cols == _dancePlan.cols || _dancePlaybackState == 1) return;
    _dancePlan.cols = cols;
    if (_dancePrepared) {
        _dancePrepared = false;
        _dancePlaybackState = 0;
        _dancePosition = 0;
        emit dancePlaybackStateChanged();
        emit dancePositionChanged();
        emit dancePreparedChanged();
        emit previewRadiusChanged();
    }
    emit boxLayoutChanged();
    emit dancePlanChanged();
    emit danceDurationChanged();
}

void MockVehicleController::_prepareDance()
{
    if (!danceCanPlay()) return;
    _danceIds.clear();
    _danceIds.reserve(_vehicleIdMap.size());
    for (const auto& [id, vehicle] : _vehicleIdMap) _danceIds.push_back(id);
    std::sort(_danceIds.begin(), _danceIds.end());
    _dancePlan.aircraftCount = static_cast<int>(_danceIds.size());
    _dancePositions.resize(_danceIds.size());
    _dancePrepared = true;
    emit dancePreparedChanged();
    _dancePosition = 0;
    _sampleDance();
    _pushDanceToBackend();
    emit dancePositionChanged();
}

void MockVehicleController::_sampleDance()
{
    if (!_dancePrepared) return;
    const double seconds = _dancePosition / 1000.0;
    float radius = 0.0f;
    for (size_t i = 0; i < _dancePositions.size(); ++i) {
        const auto pos = _dancePlan.position(static_cast<int>(i), seconds);
        _dancePositions[i] = pos;
        radius = std::max({radius, std::abs(pos.x()), std::abs(pos.y()), std::abs(pos.z())});
    }
    if (std::abs(radius - _danceRadius) > 0.1f) {
        _danceRadius = radius;
        emit previewRadiusChanged();
    }
    emit vehicleSnapshotChanged();
}

void MockVehicleController::_pushDanceToBackend()
{
    if (!_dancePrepared) return;
    std::vector<Pair_IdVector3D_t> positions;
    positions.reserve(_danceIds.size());
    for (size_t i = 0; i < _danceIds.size(); ++i)
        positions.emplace_back(_danceIds[i], _dancePositions[i]);
    zsmApp()->addTask([positions = std::move(positions)]() {
        MockVehicleManager::instance()->updateMockVehiclePositionTask(positions);
    });
    _backendClock.restart();
}

void MockVehicleController::_tickDance()
{
    if (_dancePlaybackState != 1) return;
    const qint64 elapsed = _danceClock.restart();
    _dancePosition = std::min(danceDuration(),
                              _dancePosition + static_cast<qint64>(elapsed * _dancePlaybackRate));
    _sampleDance();
    emit dancePositionChanged();
    if (!_backendClock.isValid() || _backendClock.elapsed() >= 1000
        || _dancePosition == danceDuration()) _pushDanceToBackend();
    if (_dancePosition == danceDuration()) {
        _danceTimer.stop();
        _dancePlaybackState = 0;
        emit dancePlaybackStateChanged();
    }
}

void MockVehicleController::playDance()
{
    if (!danceCanPlay()) return;
    if (!_dancePrepared) _prepareDance();
    if (_dancePosition >= danceDuration()) setDancePosition(0);
    if (_dancePlaybackState == 1) return;
    _dancePlaybackState = 1;
    _danceClock.start();
    _danceTimer.start();
    emit dancePlaybackStateChanged();
}

void MockVehicleController::pauseDance()
{
    if (_dancePlaybackState != 1) return;
    _tickDance();
    if (_dancePlaybackState != 1) return;
    _danceTimer.stop();
    _dancePlaybackState = 2;
    _pushDanceToBackend();
    emit dancePlaybackStateChanged();
}

void MockVehicleController::stopDance()
{
    _danceTimer.stop();
    _dancePlaybackState = 0;
    if (_dancePrepared) setDancePosition(0);
    emit dancePlaybackStateChanged();
}

void MockVehicleController::setDancePosition(qint64 position)
{
    if (_clearing) return;
    if (!_dancePrepared) _prepareDance();
    if (!_dancePrepared) return;
    _dancePosition = std::clamp(position, qint64(0), danceDuration());
    _sampleDance();
    _pushDanceToBackend();
    emit dancePositionChanged();
    if (_dancePlaybackState == 1) _danceClock.restart();
}

void MockVehicleController::setDancePlaybackRate(qreal rate)
{
    if (!std::isfinite(rate) || rate <= 0.0 || rate > 100.0 || qFuzzyCompare(rate, _dancePlaybackRate)) return;
    _dancePlaybackRate = rate;
    emit dancePlaybackRateChanged();
}

void MockVehicleController::setDanceViewActive(bool active)
{
    if (_danceViewActive == active) return;
    if (!active) pauseDance();
    _danceViewActive = active;
    emit danceViewActiveChanged();
    emit previewRadiusChanged();
}

void MockVehicleController::_onShowLoad(bool loaded)
{
    if (!loaded) return;
    if (_dancePrepared) stopDance();
    setDanceViewActive(false);
}
