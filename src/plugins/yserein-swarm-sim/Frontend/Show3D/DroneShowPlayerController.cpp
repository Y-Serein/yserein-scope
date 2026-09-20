#include "DroneShowPlayerController.h"
#include "DroneShowPlayerEngine.h"
#include "ZSMLoggingCategory.h"
#include "DroneShowController.h"
#include "ShowSpecification.h"
#include "SphereModelController.h"
#include "DroneShowInstanceTable.h"
#include "MockVehicleManager.h"

#include "ZSMApplication.h"
#include "ZSMBackend.h"
#include "ZSMTypes.h"

#include <QtCore/qapplicationstatic.h>
#include <QtQml/QQmlEngine>
#include <QtNumeric>
#include <vector>

ZSM_LOGGING_CATEGORY(DroneShowPlayerControllerLog, "Frontend.Show3D.DroneShowPlayerController")
Q_APPLICATION_STATIC(DroneShowPlayerController, _droneShowPlayerControllerInstance)

DroneShowPlayerController::DroneShowPlayerController(QObject* parent)
    : QObject(parent)
    , _drones(new QmlObjectListModel(this))
{
    zInfo(DroneShowPlayerControllerLog) << "create";

    // 66.66 hz
    _positionUpdateTimer.setInterval(15);
    _positionUpdateTimer.setTimerType(Qt::PreciseTimer);
    //TODO 
    connect(&_positionUpdateTimer, &QTimer::timeout, this, &DroneShowPlayerController::_updatePosition);

    _playbackEngine = new DroneShowPlayerEngine(this);
}

DroneShowPlayerController::~DroneShowPlayerController()
{
    zInfo(DroneShowPlayerControllerLog) << "destory";
}

DroneShowPlayerController* DroneShowPlayerController::instance()
{
    return _droneShowPlayerControllerInstance();
}

void DroneShowPlayerController::registerQmlTypes()
{
    qmlRegisterUncreatableType<DroneShowPlayerController>("Frontend.Show3D", 1, 0, "DroneShowPlayerController", "Reference only");
    qmlRegisterType<DroneShowInstanceTable>("Frontend.Show3D", 1, 0, "DroneShowInstanceTable");
}

void DroneShowPlayerController::init()
{
    connect(DroneShowController::instance(), &DroneShowController::isLoadShowChanged, this, &DroneShowPlayerController::_loadShow);
    connect(_playbackEngine, &DroneShowPlayerEngine::durationChanged, this, &DroneShowPlayerController::durationChanged);
    connect(guiApp(),&ZSMApplication::_1HzSignal,this,&DroneShowPlayerController::_1HzLoop);
    connect(guiApp(),&ZSMApplication::_500HzSignal,this,&DroneShowPlayerController::_500HzLoop);
}

qint64 DroneShowPlayerController::duration() const
{
    return _canPlay ? _playbackEngine->duration().get() : 0;
}

qint64 DroneShowPlayerController::position() const
{
    return _canPlay ? _playbackEngine->currentPosition().get() : 0;
}

//void DroneShowPlayerController::setPlaybackState(PlaybackState state)
//{
//
//}

void DroneShowPlayerController::setPlaybackRate(qreal rate)
{
    const float effectiveRate = std::max(static_cast<float>(rate), 0.0f);

    if (qFuzzyCompare(_playbackRate, effectiveRate)) return;

    _playbackRate = effectiveRate;

    if (_canPlay) _playbackEngine->setPlaybackRate(effectiveRate);

    emit playbackRateChanged(effectiveRate);
}

void DroneShowPlayerController::setPosition(qint64 pos)
{
    if (!_canPlay) return;
    _playbackEngine->seek(ZSMTime::TrackPosition(pos));
    pause();
}

void DroneShowPlayerController::play()
{
    // TODO 判断是否 Load
    if( !_canPlay ) return ;


    if (_playbackState == StoppedState) {
        _playbackEngine->seek(ZSMTime::TrackPosition(0));
        _updatePosition();
    }

    _runPlayback();
}

void DroneShowPlayerController::pause()
{
    if (!_canPlay) return;

    if (_playbackState == StoppedState) {
        _playbackEngine->seek(ZSMTime::TrackPosition(0));
        _updatePosition();
    }

    _playbackEngine->pause();
    _positionUpdateTimer.stop();

    _playbackState = PausedState;
    emit playbackStateChanged(_playbackState);
}

void DroneShowPlayerController::stop()
{
    if (!_canPlay) return;

    _playbackEngine->stop();
    _positionUpdateTimer.stop();
    _playbackEngine->seek(ZSMTime::TrackPosition(0));
    _updatePosition();

    _playbackState = StoppedState;
    emit playbackStateChanged(_playbackState);
}

void DroneShowPlayerController::updateVehiclePosition(int vehicleId, QVector3D pos)
{
    for (int i = 0 ;i < _drones->count(); i++) {
        auto drone = qobject_cast<SphereModelController*>(_drones->get(i));

        if (drone->id() == vehicleId ) {
            // zInfo(DroneShowPlayerControllerLog) << "update" << pos;
            drone->setPosition(QVector3D(pos.x(), pos.z(),pos.y()));
        }
    }
}

void DroneShowPlayerController::updateTakeoffVehicle(int vehicleId, double altitudeRelative)
{
    _taskQueue.enqueue([this,vehicleId,altitudeRelative]() {
        this->_updateTakeoffVehicle(vehicleId, altitudeRelative);
    });
}

void DroneShowPlayerController::requestStartFromGroundStation()
{
    // DATA16 has no MAVLink target field. The ground station may therefore
    // send the same START_CONFIG packet once per participant. Coalesce those
    // packets before crossing into the GUI/player thread.
    if (_groundStationStartQueued.exchange(true)) return;

    _taskQueue.enqueue([this]() {
        _groundStationStartQueued.store(false);
        if (!_canPlay) return;

        // A scheduled start always begins from the start of the loaded show,
        // independent of any earlier manual preview state.
        stop();
        play();
    });
}

void DroneShowPlayerController::_loadShow(bool isLoad)
{
    if (isLoad) {
        _canPlay = true;
        _playbackEngine->_loadShowSpecification(DroneShowController::instance()->showSpecification());

        // 生成无人机
        const auto controller = DroneShowController::instance();
        const auto show = controller->showSpecification();
        if (!show) return;

        for (auto drone : *(show->drones())) {
            auto model = new SphereModelController(drone->settings()->name,drone,this);
            _drones->append(model);
        }

        emit dronesChanged();
        emit canPlayChanged(_canPlay);
    }

    //TODO 修改源文件判断 ShowSpecification
}

void DroneShowPlayerController::_1HzLoop()
{
    if ( _drones->count() > 0 ) {
        _pushVehiclePosition();
    }
}

void DroneShowPlayerController::_500HzLoop()
{
    _dequeueTask();
}

void DroneShowPlayerController::_updatePosition()
{
    //TEst 
    qint64 pos = _canPlay ? _playbackEngine->currentPosition().get() : 0;
    //zDebug(DroneShowPlayerControllerLog) << "pos:" << pos;

    // 更新 无人机位置
    for (int i = 0; i < _drones->count(); i++) {
        auto drone = qobject_cast<SphereModelController*>(_drones->get(i));
        drone->updateTime(pos);
    }

    if (pos >= duration()) stop();

    emit positionChanged(pos);
}

void DroneShowPlayerController::_pushVehiclePosition()
{
    std::vector<Pair_IdVector3D_t> positions;
    positions.reserve(_drones->count());
    for (int i = 0; i < _drones->count(); i++) {
        auto drone = qobject_cast<SphereModelController*>(_drones->get(i));
        positions.push_back(
            {
                drone->id(),
                /// 单位从 cm 到 m
                QVector3D(drone->x() * 0.01,drone->z()* 0.01,drone->y()* 0.01)
            });
    }

    zsmApp()->addTask([=]() {
        MockVehicleManager::instance()->updateMockVehiclePositionTask(positions);
    });
}

void DroneShowPlayerController::_dequeueTask()
{
    std::function<void()> task;

    while ( _taskQueue.try_dequeue(task) )
    {
        task();
    }
}

void DroneShowPlayerController::_updateTakeoffVehicle(int vehicleId, double altitudeRelative)
{
    //TODO 以后改 QmlObjectListModel 为 QmlObjectMapModel
    for (int i = 0 ;i < _drones->count(); i++) {
        auto drone = qobject_cast<SphereModelController*>(_drones->get(i));

        if (drone->id() == vehicleId ) {
            drone->setPosition(QVector3D(drone->x() * 0.01, drone->z() * 0.01,altitudeRelative));
        }
    }
}

void DroneShowPlayerController::_runPlayback()
{
    _playbackEngine->play();
    _positionUpdateTimer.start();

    //TODO 可能要通过其他的方式
    _playbackState = PlayingState;
    emit playbackStateChanged(_playbackState);
}
