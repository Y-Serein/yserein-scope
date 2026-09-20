//
// Created by Hoppz on 25-12-8.
//
#include "libzds/drone/trajectory_segment.h"

#include <iostream>

#include "../utils/math.h"
#include <cassert>

namespace libzds
{

// void trajectory_segment_poly_stretch(Poly& p, float duration)
// {
//     if (duration <= 0.0f) return;
//
//     float inv = 1.0f / duration;
//     float scale = 1.0f;
//
//     for (uint8_t i = 1; i < p.numCoeffs; ++i) {
//         scale *= inv;
//         p.coeffs[i] *= scale;
//     }
// }

Status trajectory_segment_make_bezier_poly(Poly& p, float duration,
                                           const std::vector<float>& xs,
                                           uint8_t numPoints)
{
    if (duration <= 0.f) {
        return Status::InvalidArgument("duration is zero");
    }
    if (numPoints < 2 || numPoints > MAX_POLY_COEFFS || xs.size() < numPoints) {
        return Status::InvalidArgument("invalid Bezier control point count");
    }

    const int n = numPoints - 1;  // Bezier 阶数
    p.clear();
    p.numCoeffs = n + 1;

    // 为了构造 B(t) = B(u=t/d)，需要展开成 t 的多项式
    // B(t) = Σ C(n,i) * P_i * (t/d)^i * (1 - t/d)^(n-i)

    // 我们先按贝塞尔基函数系数构造在 u 上的多项式
    std::vector<float> bezierCoefs(n + 1, 0.0f);

    for (int i = 0; i <= n; i++) {
        float Pi = xs[i];
        for (int k = 0; k <= n - i; k++) {
            float comb = LIBZDS_FACES[n] /
                         (LIBZDS_FACES[i] * LIBZDS_FACES[n-i]);

            float coef = comb *
                (float)std::pow(-1.0f, k) *
                (float)( LIBZDS_FACES[n-i] /
                         (LIBZDS_FACES[k] * LIBZDS_FACES[n-i-k]) );

            bezierCoefs[i + k] += Pi * coef;
        }
    }

    // bezierCoefs 存的是 B(u) 的系数，现在需要做变量替换 u = t/duration
    float inv = 1.0f / duration;
    float invPow = 1.0f;

    for (int k = 0; k <= n; k++) {
        p.coeffs[k] = bezierCoefs[k] * invPow;
        invPow *= inv;  // t/d 的幂
    }

    return Status::OK();
}

Status trajectory_segment_points_to_poly(Poly4D& result, float duration, const std::vector<Vector3D>& points)
{
    const uint8_t n = static_cast<uint8_t>(points.size());

    std::vector<float> xs(n), ys(n), zs(n);
    for ( uint8_t i = 0; i < n; ++i ) {
        xs[i] = points[i].x;
        ys[i] = points[i].y;
        zs[i] = points[i].z;
    }

    Status status = trajectory_segment_make_bezier_poly(result.x, duration, xs, n);
    if ( !status.ok() ) return status;
    status = trajectory_segment_make_bezier_poly(result.y, duration, ys, n);
    if ( !status.ok() ) return status;
    status = trajectory_segment_make_bezier_poly(result.z, duration, zs, n);
    if ( !status.ok() ) return status;

    return Status::OK();
}

Status TrajectorySegment::init(float startTime, float endTime
    , const std::vector<Vector3D> &points, bool useMathWayInitMaxMin)
{
    if ( points.size() < 2 ) {
        return Status::InvalidArgument("TrajectorySegment", "not enough points");
    }
    if ( points.size() > MAX_POLY_COEFFS) {
        return Status::NotSupported("TrajectorySegment", "too much points");
    }
    if (endTime < startTime) {
        return Status::InvalidArgument("TrajectorySegment", "bad time range");
    }

    if ( _init ) {
        return Status::NotSupported("TrajectorySegment", "already init");
    }

    _startTimeSec = startTime;
    _endTimeSec = endTime;
    _startPoint = *points.begin();
    _endPoint = *points.rbegin();

    _type = (points.size() == 2) ? SegmentType::Linear : SegmentType::Bezier;

    Status status = trajectory_segment_points_to_poly(_posPoly, durationSec(),  points);
    if ( !status.ok() ) return status;

    // init max min velocity accel
    _velPoly = _posPoly.derivative();
    _accPoly = _velPoly.derivative();

    if ( useMathWayInitMaxMin ) {
        status =_initMaxMinMathWay();
        if ( !status.ok() ) return status;
    } else {
        status =_initMaxMinSampleWay();
        if ( !status.ok() ) return status;
    }

    _init = true;

    return Status::OK();
}

Status TrajectorySegment::getPositionAt(float time, Vector4D &pos) const
{
    if (!_init) {
        return Status::NotSupported("not init trajectory segment");
    }

    float t = time - _startTimeSec;
    //TODO 边界处理
    if (t < 0.0f || t > durationSec()) {
        assert(false);
        return Status::InvalidArgument("time out of range");
    }

    pos = _posPoly.eval(t);
    return Status::OK();
}

Status TrajectorySegment::getVelocityAt(float time, Vector4D &vel) const
{
    if (!_init) {
        return Status::NotSupported("not init trajectory segment");
    }

    float t = time - _startTimeSec;
    //TODO 边界处理, 同步修改 _initMaxMinSampleWay getAccelAt getVelocityAt
    if (t < 0.0f || t > durationSec()) {
        assert(false);
        return Status::InvalidArgument("time out of range");
    }

    vel = _velPoly.eval(t);

    //std::cout << t << "vel: " << vel.x << vel.y << vel.z << std::endl;
    return Status::OK();
}

Status TrajectorySegment::getAccelAt(float time, Vector4D &acc) const
{
    if (!_init) {
        return Status::NotSupported("not init trajectory segment");
    }

    float t = time - _startTimeSec;
    //TODO 边界处理, 同步修改 _initMaxMinSampleWay getAccelAt getVelocityAt
    if (t < 0.0f || t > durationSec()) {
        assert(false);
        return Status::InvalidArgument("time out of range");
    }

    acc = _accPoly.eval(t);
    return Status::OK();
}

Status TrajectorySegment::getVelMax(Vector4D &velMax) const
{
    if (!_init) {
        return Status::NotSupported("not init trajectory segment");
    }

    velMax = _velMax;
    return Status::OK();
}

Status TrajectorySegment::getVelMin(Vector4D &velMin) const
{
    if (!_init) {
        return Status::NotSupported("not init trajectory segment");
    }

    velMin = _velMin;
    return Status::OK();
}

Status TrajectorySegment::getAccMax(Vector4D &accMax) const
{
    if (!_init) {
        return Status::NotSupported("not init trajectory segment");
    }

    accMax = _accMax;
    return Status::OK();
}

Status TrajectorySegment::getAccMin(Vector4D &accMin) const
{
    if (!_init) {
        return Status::NotSupported("not init trajectory segment");
    }

    accMin = _accMin;
    return Status::OK();
}

Status TrajectorySegment::_initMaxMinMathWay()
{
    Status status = _velPoly.polyMaxMin(durationSec(),_velMax, _velMin);
    if ( !status.ok() ) return status;
    status = _accPoly.polyMaxMin(durationSec(),_accMax, _accMin);
    if ( !status.ok() ) return status;

    return Status::OK();
}

Status TrajectorySegment::_initMaxMinSampleWay(uint32_t sampleTime_ms)
{
    if ( sampleTime_ms <= 0 ) return Status::InvalidArgument("TrajectorySegment", "sample time <= 0");

    float time = 0;
    float sample = static_cast<float>(sampleTime_ms) / 1000.f;

    // init vel
    Vector4D te_maxVel, te_minVel;
    bool firstFlag = true;

    float duration = durationSec();

    while ( time <= duration) {
        float t = time;

        //TODO 边界处理, 同步修改 _initMaxMinSampleWay getAccelAt getVelocityAt
        if (t < 0.0f || t > durationSec()) {
            assert(false);
            return Status::InvalidArgument("time out of range");
        }

        Vector4D te_vel = _velPoly.eval(t);

        if ( firstFlag ) {
            te_maxVel = te_vel;
            te_minVel = te_vel;
            firstFlag = false;
        } else {
            te_maxVel = Vector4D::maxAll(te_maxVel, te_vel);
            te_minVel = Vector4D::minAll(te_minVel, te_vel);
        }

        time += sample;
    }

    //TODO check the result is reasonable
    _velMax = te_maxVel;
    _velMin = te_minVel;

    // init acc
    time = 0;
    Vector4D te_maxAcc, te_minAcc;
    firstFlag = true;

    while ( time < duration) {
        float t = time;

        //TODO 边界处理, 同步修改 _initMaxMinSampleWay getAccelAt getVelocityAt
        if (t < 0.0f || t > durationSec()) {
            assert(false);
            return Status::InvalidArgument("time out of range");
        }
        Vector4D te_acc = _accPoly.eval(t);

        if ( firstFlag ) {
            te_maxAcc = te_acc;
            te_minAcc = te_acc;
            firstFlag = false;
        } else {
            te_maxAcc = Vector4D::maxAll(te_maxAcc, te_acc);
            te_minAcc = Vector4D::minAll(te_minAcc, te_acc);
        }

        time += sample;
    }

    //TODO check the result is reasonable
    _accMax = te_maxAcc;
    _accMin = te_minAcc;

    return Status::OK();
}

} // namespace libzds



