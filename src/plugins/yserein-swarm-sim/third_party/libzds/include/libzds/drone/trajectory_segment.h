//
// Created by Hoppz on 25-12-8.
//

#ifndef LIBZDS_TRAJECTORY_H
#define LIBZDS_TRAJECTORY_H

#include <cstdint>
#include <vector>

#include "libzds/utils/status.h"
#include "libzds/utils/type.h"

namespace libzds{

class TrajectorySegment
{
public:

    TrajectorySegment() = default;
    ~TrajectorySegment() = default;

    enum class SegmentType : uint8_t
    {
        Linear = 0,
        Bezier = 1,
        Poly7D = 2
    };

    /**
     * 初始化轨迹片段
     * @param startTime 轨迹片段的起始时间, 单位 s
     * @param endTime 轨迹片段的结束时间, 单位 s
     * @param points 轨迹包含的点, 单位 m
     * @param useMathWayInitMaxMin 是否使用数学方法来初始化极值
     * @return
     */
    Status init(float startTime, float endTime,
        const std::vector<Vector3D>& points, bool useMathWayInitMaxMin = false);

    /// ------------------- getter

    SegmentType type() const {return _type;}
    float startTimeSec() const {return _startTimeSec;}
    float endTimeSec() const {return _endTimeSec;}
    Vector3D startPoint() const {return _startPoint;}
    Vector3D endPoint() const {return _endPoint;}
    float durationSec() const {return _endTimeSec - _startTimeSec;}

    Status getVelMax(Vector4D& velMax) const;
    Status getVelMin(Vector4D& velMin) const;

    Status getAccMax(Vector4D& accMax) const;
    Status getAccMin(Vector4D& accMin) const;

    Status getPositionAt(float time, Vector4D& pos) const;
    Status getVelocityAt(float time, Vector4D& vel) const;
    Status getAccelAt(float time, Vector4D& acc) const;

private:

    /**
     * 通过计算多项式极值的办法获取轨迹片段的极值
     * 当前的算法计算出来的还不够稳定
     */
    Status _initMaxMinMathWay();

    /**
     * 通过采样方法来获取极值
     * @param sampleTime_ms 采样时间间隔, 单位 ms
     */
    Status _initMaxMinSampleWay(uint32_t sampleTime_ms = 100);

    bool _init{false};
    SegmentType _type{SegmentType::Linear};

    float _startTimeSec{0};
    float _endTimeSec{0};

    Vector3D _startPoint;
    Vector3D _endPoint;

    Poly4D _posPoly;      ///< 位置贝塞尔多项式
    Poly4D _velPoly;      ///< 速度贝斯尔多项式
    Poly4D _accPoly;      ///< 加速度贝塞尔多项式

    Vector4D _velMax, _velMin;
    Vector4D _accMax, _accMin;
};  // class TrajectorySegment

} // namespace libzds

#endif //LIBZDS_TRAJECTORY_H
