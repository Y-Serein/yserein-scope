#include "DroneShowPlayerEngine.h"
#include "ZSMLoggingCategory.h"
#include "DroneShowController.h"
#include "ShowSpecification.h"
#include <QMetaType>

ZSM_LOGGING_CATEGORY(DroneShowPlayerEngineLog,"Frontend.Show3D.DroneShowPlayerEngine")

DroneShowPlayerEngine::DroneShowPlayerEngine(QObject* parent)
	: QObject(parent)
{
	zInfo(DroneShowPlayerEngineLog) << "create";

	//TODO registerQmlTpyes
	qRegisterMetaType<ZSMTime::TrackDuration>("TrackDuration");
	qRegisterMetaType<ZSMTime::TrackPosition>("TrackPosition");
}

DroneShowPlayerEngine::~DroneShowPlayerEngine()
{
	zInfo(DroneShowPlayerEngineLog) << "destory";

	//TODO ZSMTimeController extend QObject to manager create and destroy
}

void DroneShowPlayerEngine::setState(DroneShowPlayerController::PlaybackState state)
{
	if (state == _state) return;
	// T exchange(T& obj,U&& new_value);
	const auto prevState = std::exchange(_state, state);

	if (_state == DroneShowPlayerController::StoppedState) {
		_finalizeTime(TrackPosition(0));
	}

	//TODO maybe need to remove
	if (prevState == DroneShowPlayerController::StoppedState || _state == DroneShowPlayerController::StoppedState) {
		_timeController.setPaused(true);
	}

	_updatePausedState();
}

void DroneShowPlayerEngine::seek(ZSMTime::TrackPosition pos)
{
	pos = _boundPosition(pos);

	//_timeController.setPaused(true);
	setState(DroneShowPlayerController::PausedState);
	_timeController.sync(pos);
}

float DroneShowPlayerEngine::playbackRate() const
{
	return _timeController.playbackRate();
}

void DroneShowPlayerEngine::setPlaybackRate(float rate)
{
	if (rate == playbackRate()) return;

	_timeController.setPlaybackRate(rate);
}

ZSMTime::TrackPosition DroneShowPlayerEngine::currentPosition(bool topPos) const
{
	return _boundPosition(TrackPosition(_timeController.currentPosition()));
}

ZSMTime::TrackDuration DroneShowPlayerEngine::duration() const
{
	return TrackDuration(_duration);
}

void DroneShowPlayerEngine::_loadShowSpecification(ShowSpecification* show)
{
    if (!show) {
        stop();
        _timeController.sync(TrackPosition(0));
        _show = nullptr;
        _duration = 0;
        emit durationChanged();
        return;
    }
	
	_show = show;
	_duration = static_cast<qint64>(_show->showSettings()->cues()->items.back().time * 1000);
	emit durationChanged();
}

void DroneShowPlayerEngine::_finalizeTime(TrackPosition pos)
{
	Q_ASSERT(pos >= TrackPosition(0) && pos <= duration().asTimePoint());

	_timeController.setPaused(true);
	_timeController.sync(pos);

}

//TODO maybe need to remove this function
void DroneShowPlayerEngine::_updatePausedState()
{
	const auto paused = _state != DroneShowPlayerController::PlayingState;

	_timeController.setPaused(paused);
}

ZSMTime::TrackPosition DroneShowPlayerEngine::_boundPosition(TrackPosition pos) const
{
	pos = qMax(pos, TrackPosition(0));
	return duration() > TrackDuration(0) ? qMin(pos, duration().asTimePoint()) : pos;
}
