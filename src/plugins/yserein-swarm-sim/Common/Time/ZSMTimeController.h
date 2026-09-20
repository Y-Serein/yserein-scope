#pragma once

/// frome QMediaPlayer ffmpeg-> qffmepgtimercontroler.h

#include <chrono>
#include <optional>

#include "ZSMTime.h"

QT_BEGIN_NAMESPACE

namespace ZSMTime {

class ZSMTimeController
{
public:
	/// 标识当前的真实时间点，多少时，多少分，多少秒
	using TimePoint = SteadyClock::time_point;
	using PlaybackRate = float;

	ZSMTimeController();

	PlaybackRate playbackRate() const;
	void setPlaybackRate(PlaybackRate playbackRate);

	void sync(TrackPosition trackPos = TrackPosition(0));
	void sync(TimePoint tp, TrackPosition pos);
	
	/// @brief 
	/// @param tp 
	/// @param pos 
	/// @param fixingTime	过度的时间窗口（多少时间完成过度）
	void syncSoft(TimePoint tp, TrackPosition pos,
		SteadyClock::duration fixingTime = std::chrono::seconds(4));

	TrackPosition currentPosition(SteadyClock::duration offset = SteadyClock::duration{ 0 }) const;
	
	void setPaused(bool paused);
	
	/// @brief	从真实时间计算对应的播放时间
	/// @param tp			真实时间点
	/// @param ignorePause	
	/// @return 对应的播放时间
	TrackPosition positionFromTime(TimePoint tp, bool ignorePause = false) const;
	
	/// @brief	从播放时间计算真实时间
	/// @param pos			播放时间点
	/// @param ignorePause 
	/// @return 对应的真实时间点
	TimePoint timeFromPosition(TrackPosition pos, bool ignorePause = false) const;

private:
	/// @brief	任何微小的播放偏移都会造成画面抖动、跳帧、声音错位
	///			软同步，用于“平滑过渡轨道位置”的一种机制，防止位置跳跃，保持视觉或逻辑上的连续性。
	struct SoftSyncData 
	{
		TimePoint srcTimePoint;				///< 起始真实时间
		TrackPosition srcPosition = 0;		///< 起始播放时间
		TimePoint dstTimePoint;				///< 希望在这个时间点完成同步
		TrackDuration srcPosOffset = 0;		///< 当前播放时间与 “正常播放” 时间的差距
		TrackPosition dstPosition = 0;		///< 希望在这个时间点完成同步

		PlaybackRate internalRate = 1;		///< 为了实现平滑过度，要加速多少
	};	///< 在 srcTp 开始，按某个 internalRate，从 srcPos 平滑过渡到 dstTp 对应的播放位置

	/// @brief	记录 srcTp 和 srcPos 过度到 dstTp 的数据
	/// @param srcTp		起始时间
	/// @param srcPos	起始位置
	/// @param dstTp		期望在这个时间点到达目标位置
	/// @return 
	SoftSyncData makeSoftSyncData(const TimePoint& srcTp, const TrackPosition& srcPos,
								  const TimePoint& dstTp) const;

	TrackPosition positionFromTimeInternal(const TimePoint& tp) const;

	TimePoint timeFromPositionInternal(const TrackPosition& pos) const;

	/// @brief	根据当前的真实时间点、播放速度，计算现在的播放时间点
	///			用于在播放过程中，有其他的操作时，先同步时间到最新的播放点，再做其他的操作
	void scrollTimeTillNow();

	/// @brief 播放时间段转换为真实时间段
	/// @param trackDuration	播放时间段长度
	/// @param rate			播放速率
	/// @return 真实时间段长度			
	static SteadyClock::duration toClockDuration(TrackDuration trackDuration,
		PlaybackRate rate = 1.f);

	/// @brief 真实时间段转换为播放时间段
	/// @param clockDuration	真实时间段长度 
	/// @param rate			播放速率
	/// @return 播放时间段长度
	static TrackDuration toTrackDuration(SteadyClock::duration clockDuration, PlaybackRate rate);

private:
	bool _paused = true;
	PlaybackRate _playbackRate = 1;
	TrackPosition _position = 0;
	TimePoint _timePoint;
	std::optional<SoftSyncData> _softSyncData;
};

} // namespace ZSMTIME

QT_END_NAMESPACE

