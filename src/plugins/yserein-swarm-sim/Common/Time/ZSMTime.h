#ifndef ZSMTIME_H
#define ZSMTIME_H

#include <chrono>
#include <qcompare.h>

#include "qtaggedtime_p.h"

QT_BEGIN_NAMESPACE

namespace ZSMTime
{

using namespace std::chrono_literals;
using SteadyClock = std::chrono::steady_clock;

struct TrackTimeTag;
// track position in milliSeconds , used as
// a general time position in the playback engine
using TrackPosition = QTaggedTimePoint<qint64, TrackTimeTag>;			///< 时间点		(单位：毫秒)
using TrackDuration = QTaggedDuration<qint64, TrackTimeTag>;			///< 时间长度		(单位：毫秒)




}// namespace ZSMTimer

QT_END_NAMESPACE

#endif