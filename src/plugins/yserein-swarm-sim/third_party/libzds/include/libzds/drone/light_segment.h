//
// Created by Hoppz on 25-12-9.
//

#ifndef LIBZDS_DRONE_LIGHT_SEGMENT_H
#define LIBZDS_DRONE_LIGHT_SEGMENT_H

#include <libzds/utils/status.h>
#include <libzds/utils/type.h>

namespace libzds{

/// 初始化之后无法改变
struct LightSegment {
    LightSegment(uint32_t st, uint32_t et, bool f, Color sc, Color ec, uint32_t dt)
        : startTime(st), endTime(et), isFade(f), startColor(sc), endColor(ec), duration(dt){}

    LightSegment(const LightSegment& rhs) = default;

    Color getColorAt(uint32_t time) const
    {
        if ( time < startTime || time > endTime ) {
            return {};
        }

        if ( isFade ) {
            float start = static_cast<float>(startTime);
            float end = static_cast<float>(endTime);
            float p = static_cast<float>(time);


            float ratio = duration > 0 ? (p - start) / (end - start) : 0;
            uint8_t r = startColor.r + ratio * static_cast<float>(endColor.r - startColor.r);
            uint8_t g = startColor.g + ratio * static_cast<float>(endColor.g - startColor.g);
            uint8_t b = startColor.b + ratio * static_cast<float>(endColor.b - startColor.b);
            uint8_t a = startColor.a + ratio * static_cast<float>(endColor.a - startColor.a);
            return {r, g, b, a};
        } else {
            return startColor;
        }
    }

    const Color startColor;
    const Color endColor;
    const bool isFade;
    const uint32_t startTime;     ///< start time
    const uint32_t endTime;
    const uint32_t duration;
};

}// namespace libzds

#endif //LIBZDS_DRONE_LIGHT_SEGMENT_H
