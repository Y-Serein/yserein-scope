#include "ZSMTimeController.h"
#include <QDebug>

namespace ZSMTime {

ZSMTimeController::ZSMTimeController()
{
	sync();
}

ZSMTimeController::PlaybackRate ZSMTimeController::playbackRate() const
{
	return _playbackRate;
}

void ZSMTimeController::setPlaybackRate(PlaybackRate playbackRate)
{
	if (playbackRate == _playbackRate) return;

	Q_ASSERT(playbackRate > 0.0f);

	// 更新当前的播放时间
	scrollTimeTillNow();
	_playbackRate = playbackRate;

	// 如果当前还在软同步中，重新生成软同步数据
	if (_softSyncData) {
		_softSyncData = makeSoftSyncData(_timePoint, _position, _softSyncData->dstTimePoint);
	}
}

void ZSMTimeController::sync(TrackPosition trackPos)
{
	sync(SteadyClock::now(), trackPos);
}

void ZSMTimeController::sync(TimePoint tp, TrackPosition pos)
{
	_softSyncData.reset();
	_position = pos;
	_timePoint = tp;
}

void ZSMTimeController::syncSoft(TimePoint tp, TrackPosition pos, SteadyClock::duration fixingTime)
{
	const auto srcTime = SteadyClock::now();
	const auto srcPos = positionFromTime(srcTime, true);
	const auto dstTime = srcTime + fixingTime;

	_position = pos;
	_timePoint = tp;

	_softSyncData = makeSoftSyncData(srcTime, srcPos, dstTime);
}

TrackPosition ZSMTimeController::currentPosition(SteadyClock::duration offset) const
{
	return positionFromTime(SteadyClock::now() + offset);
}

void ZSMTimeController::setPaused(bool paused)
{
	if( _paused == paused ) return;

	scrollTimeTillNow();
	_paused = paused;
}

TrackPosition ZSMTimeController::positionFromTime(TimePoint tp, bool ignorePause) const
{
	// 暂停使用之前的 _timePoint, 没暂停使用新的
	tp = _paused && !ignorePause ? _timePoint : tp;

	if (_softSyncData && tp < _softSyncData->dstTimePoint) {
		// 软同步没有结束
		const PlaybackRate rate =
			// 软同步是否开始了？ 
			tp > _softSyncData->srcTimePoint ? _softSyncData->internalRate : _playbackRate;
		
		return _softSyncData->srcPosition + toTrackDuration(tp - _softSyncData->srcTimePoint, rate);
	}

	return positionFromTimeInternal(tp);
}

ZSMTimeController::TimePoint ZSMTimeController::timeFromPosition(TrackPosition pos, bool ignorePause) const
{
	auto position = _paused && !ignorePause ? _position : TrackPosition(pos);

	if (_softSyncData&& position < _softSyncData->dstPosition) {
		const auto rate = position > _softSyncData->srcPosition ? _softSyncData->internalRate
			: _playbackRate;
		return _softSyncData->srcTimePoint
			+ toClockDuration(position - _softSyncData->srcPosition, rate);
	}

	return timeFromPositionInternal(position);
}

ZSMTimeController::SoftSyncData ZSMTimeController::makeSoftSyncData(const TimePoint& srcTp,
																	  const TrackPosition& srcPos, 
																	  const TimePoint& dstTp) const
{
	SoftSyncData result;
	result.srcTimePoint = srcTp;
	result.srcPosition = srcPos;
	result.dstTimePoint = dstTp;
	result.srcPosOffset = srcPos - positionFromTime(srcTp);
	result.dstPosition = positionFromTimeInternal(dstTp);
	result.internalRate =
			static_cast<PlaybackRate>(toClockDuration(result.dstPosition - srcPos).count())
			/ (dstTp - srcTp).count();

	return result;
}

TrackPosition ZSMTimeController::positionFromTimeInternal(const TimePoint& tp) const
{
	// 当前的位置 + 将要播放的位置
	return _position + toTrackDuration(tp - _timePoint, _playbackRate);
}

ZSMTimeController::TimePoint ZSMTimeController::timeFromPositionInternal(const TrackPosition& pos) const
{
	return _timePoint + toClockDuration(pos - _position, _playbackRate);
}

void ZSMTimeController::scrollTimeTillNow()
{
	// 获取当前的真实时间
	const auto now = SteadyClock::now();

	if (!_paused) {
		// 没有暂停，根据 now 计算新的 position
		_position = positionFromTimeInternal(now);

		// 如果启用了软同步，且当前时间已经超过目标时间
		if (_softSyncData && _softSyncData->dstTimePoint <= now)
			_softSyncData.reset();
	}
	else if (_softSyncData) {
		// 如果是暂停，且启用了软同步，仍要移动时间
		// 让软同步仍然能在 resume 后正常工作

		_softSyncData->dstTimePoint += now - _timePoint;
		_softSyncData->srcTimePoint += now - _timePoint;
	}

	_timePoint = now;
}

SteadyClock::duration ZSMTimeController::toClockDuration(TrackDuration trackDuration, PlaybackRate rate)
{
	return std::chrono::duration_cast<SteadyClock::duration>(
		std::chrono::milliseconds(trackDuration.get()) / rate);
}

TrackDuration ZSMTimeController::toTrackDuration(SteadyClock::duration clockDuration, PlaybackRate rate)
{
	return TrackDuration(
		std::chrono::duration_cast<std::chrono::milliseconds>(clockDuration * rate).count());
}

} // namespace ZSMTime