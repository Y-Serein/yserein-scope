#pragma once 

#include <QtCore/QObject>

#include "ZSMTimeController.h"
#include "DroneShowPlayerController.h"

class ShowSpecification;

class DroneShowPlayerEngine : public QObject
{
	Q_OBJECT
	friend class DroneShowPlayerController;

public:
	using ZSMTimeController = ZSMTime::ZSMTimeController;
	using TrackPosition = ZSMTime::TrackPosition;
	using TrackDuration = ZSMTime::TrackDuration;
	using PlaybackState = DroneShowPlayerController::PlaybackState;

	//TODO need SphereModelController list point or create in this scope
	//TODO if we want to create in this scope we need the list of DroneSpecification
	//TODO we can get from DroneShowController::instance()
	explicit DroneShowPlayerEngine(QObject* parent = nullptr);
	~DroneShowPlayerEngine() override;

	void setState(PlaybackState state);

	void play() { setState(DroneShowPlayerController::PlayingState); }
	void pause() { setState(DroneShowPlayerController::PausedState); }
	void stop() { setState(DroneShowPlayerController::StoppedState); }

	void seek(ZSMTime::TrackPosition pos);
	
	float playbackRate() const;
	void setPlaybackRate(float rate);

	TrackPosition currentPosition(bool topPos = true) const;
	TrackDuration duration() const;

signals:
	void durationChanged();

private slots:
	//void update

private:
	void _loadShowSpecification(ShowSpecification* show);

	void _finalizeTime(TrackPosition pos);

	void _updatePausedState();

	/// @brief	限制 pos 的值再合理的范围
	/// @param pos 播放时间
	/// @return	限制 pos 在 [0, duration]
	TrackPosition _boundPosition(TrackPosition pos) const;

private:

	PlaybackState _state{DroneShowPlayerController::StoppedState};
	ZSMTimeController _timeController;

	ShowSpecification* _show{nullptr};
	qint64 _duration{ 0LL };
};