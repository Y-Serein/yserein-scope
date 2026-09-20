#pragma once

#include <QtCore/QObject>
#include <QTimer>
#include <atomic>
#include <functional>

#include "QmlObjectListModel.h"
#include "concurrentqueue.h"

class DroneShowPlayerEngine;

class DroneShowPlayerController : public QObject
{
	Q_OBJECT

	//Q_PROPERTY(PlaybackState			playbackState	READ playbackState	setPlaybackState	NOTIFY playbackStateChanged)
	Q_PROPERTY(PlaybackState			playbackState	READ playbackState							NOTIFY playbackStateChanged)
	Q_PROPERTY(bool						playing			READ isPlaying								NOTIFY playingChanged)
	Q_PROPERTY(bool						canPlay			READ canPlay								NOTIFY canPlayChanged)
	Q_PROPERTY(qint64					duration		READ duration								NOTIFY durationChanged)
	Q_PROPERTY(qreal					playbackRate	READ playbackRate	WRITE setPlaybackRate	NOTIFY playbackRateChanged)
	Q_PROPERTY(qint64					position		READ position		WRITE setPosition		NOTIFY positionChanged)
	Q_PROPERTY(QmlObjectListModel*		drones			READ drones									NOTIFY dronesChanged)

public:
	explicit DroneShowPlayerController(QObject* parent = nullptr);
	~DroneShowPlayerController();

	static DroneShowPlayerController* instance();
	static void registerQmlTypes();

	void init();

	enum PlaybackState {
		StoppedState,
		PlayingState,
		PausedState
	};
	Q_ENUM(PlaybackState);
	
	/// getter =====================================================

	PlaybackState playbackState() const {return _playbackState;}
	bool isPlaying() const { return _isPlaying; }
	bool canPlay() const { return _canPlay; }
	qint64 duration() const;
	qreal playbackRate() const { return _playbackRate; }
	qint64 position() const;
	QmlObjectListModel* drones() const { return _drones; }

	/// setter =====================================================
	
	//void setPlaybackState(PlaybackState state);
	void setPlaybackRate(qreal rate);
	void setPosition(qint64 pos);

	/// function ===================================================

	Q_INVOKABLE void play();
	Q_INVOKABLE void pause();
	Q_INVOKABLE void stop();

	/**
	 * 由 MockVehiclePropertys 接收前端的消息后发送过来
	 */
	void updateVehiclePosition(int vehicleId, QVector3D pos);

	/// task function ----------------------------------------------

	void updateTakeoffVehicle(int vehicleId, double altitudeRelative);
	void requestStartFromGroundStation();

signals:
	void playbackStateChanged(PlaybackState state);
	void playingChanged();
	void canPlayChanged(bool canPlay);
	void durationChanged();
	void playbackRateChanged(qreal rate);
	void positionChanged(qint64 position);
	void dronesChanged();
	void instanceTableChanged();

private slots:
	//TODO change to handleShowChange
	void _loadShow(bool isLoad);
	
	/// 绑定全局 1Hz
	void _1HzLoop();
	void _500HzLoop();

private: 
	void _runPlayback();
	void _updatePosition();

	/// --------------------------------------------------- 1Hz Task

	/// 推送无人机实时位置给后端, 用于 gps 信息
	void _pushVehiclePosition();

	/// ------------------------------------------------- 500Hz Task
	void _dequeueTask();

	/// ----------------------------------- task from Vehicle Thread

	void _updateTakeoffVehicle(int vehicleId, double altitudeRelative);



private:
	
	/// property's member ==========================================
	
	bool _canPlay{ false };											///< 加载 show 之后才可以 play
	bool _isPlaying{false};
	PlaybackState _playbackState{ StoppedState };
	//TODO store in engine
	//qint64 _duration{ 0 };
	float _playbackRate{ 1.f };
	QmlObjectListModel* _drones;

	/// function member ============================================

	QTimer _positionUpdateTimer;
	DroneShowPlayerEngine* _playbackEngine{nullptr};
	moodycamel::ConcurrentQueue<std::function<void()>> _taskQueue;
	std::atomic_bool _groundStationStartQueued{false};
};
