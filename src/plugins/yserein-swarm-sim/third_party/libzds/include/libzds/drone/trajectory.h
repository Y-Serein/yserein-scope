//
// Created by Hoppz on 25-12-9.
//

#ifndef LIBZDS_DRONE_TRAJECTORY_H
#define LIBZDS_DRONE_TRAJECTORY_H

#include <vector>

#include "libzds/utils/type.h"
#include "libzds/drone/trajectory_segment.h"

namespace libzds{

class Status;

struct TrajectoryInit
{
    float startTime{0};
    float endTime{0};

    std::vector<Vector3D> points;
};

class Trajectory {
public:
    Trajectory() = default;
    ~Trajectory() = default;

    Status init(float landTime, std::vector<TrajectoryInit>& segment);

    /// ------------------- getter

    Vector3D takeoffAt() const {return _takeoffAt;}
    Vector3D landAt() const {return _landAt;}
    float landTime() const {return _landTime;}

    Vector4D maxVel() const {return _maxVel;}
    Vector4D minVel() const {return _minVel;}

    Vector4D maxAcc() const {return _maxAcc;}
    Vector4D minAcc() const {return _minAcc;}

    Status getPositionAt(float time, Vector4D& pos);
    Status getVelocityAt(float time, Vector4D& vel);
    Status getAccelAt(float time, Vector4D& acc);

private:
    /**
     * 使用二分查找, 获取当前时间对应的 segment index
     * @param time 单位 s
     * @return -1: not found, >=0: segment index
     */
    int _getSegmentIndex(float time);

    /**
     * 这种方法通过直接计算每一个轨迹段的极值点来获取速度，加速度的最大值
     * 但是目前的算法还不够完善，精度非常不准确
     */
    Status _initMaxMin();


    // only use this function in init
    Status _initTakeOffLand();

    bool _init{false};
    std::vector<TrajectorySegment> _segment;

    Vector4D _maxVel, _minVel;
    Vector4D _maxAcc, _minAcc;

    Vector3D _landAt;
    Vector3D _takeoffAt;

    float _landTime{0.f};           ///< trajectory.json  中的降落时间
};


}

#endif //LIBZDS_DRONE_TRAJECTORY_H
