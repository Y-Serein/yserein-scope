//
// Created by Hoppz on 25-12-9.
//

#ifndef LIBZDS_DRONE_LIGHT_PROGRAM_H
#define LIBZDS_DRONE_LIGHT_PROGRAM_H

#include <libzds/drone/light_segment.h>
#include <libzds/utils/status.h>

#include <vector>

namespace libzds{

class LightProgram{
public:
    LightProgram() = default;
    ~LightProgram() = default;

    Status init(const char* u8data, size_t size);

    Status getColorAt(uint32_t time, Color& color);

private:

    int _getSegmentIndex(uint32_t time);

    bool _init{false};

    std::vector<LightSegment> _segment;
};

}   // namespace libzds

#endif //LIBZDS_DRONE_LIGHT_PROGRAM_H
