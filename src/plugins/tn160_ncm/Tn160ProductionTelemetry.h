#pragma once

#include <QtCore/QtTypes>

#include <cmath>

namespace serein::scope::tn160ncm {

inline bool validProductionMonitorTelemetry(const qint64 timestampUs,
                                            const quint16 ntcAdu,
                                            const quint16 vtempAdu,
                                            const float meanDiff) noexcept
{
    return timestampUs > 0 && ntcAdu > 0 && ntcAdu < 4095
        && vtempAdu > 0 && vtempAdu <= 0x3fff
        && std::isfinite(meanDiff) && meanDiff >= 0.0f;
}

} // namespace serein::scope::tn160ncm
