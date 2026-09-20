//
// Created by Hoppz on 25-12-9.
//

#include "libzds/drone/trajectory.h"
#include "libzds/drone/trajectory_segment.h"
#include "libzds/utils/status.h"

namespace libzds{

Status Trajectory::init(float landTime, std::vector<TrajectoryInit>& segment)
{
    if ( _init ) {
        return Status::NotSupported("already init");
    }

    if ( segment.empty() ) {
        return Status::InvalidArgument("segment is empty");
    }

    _segment.clear();
    _segment.reserve(segment.size());

    _landTime = landTime;

    for (const auto &segInit : segment) {
        _segment.push_back(TrajectorySegment());
        auto& seg = _segment.back();
        Status status = seg.init(segInit.startTime, segInit.endTime, segInit.points);
        if ( !status.ok() ) return status;
    }

    // init takeoff land
    Status s = _initTakeOffLand();
    if ( !s.ok() ) return s;

    // init max min
    s = _initMaxMin();
    if ( !s.ok() ) return s;

    _init = true;

    return Status::OK();
}

Status Trajectory::getPositionAt(float time, Vector4D &pos)
{
    if ( !_init ) {
        return Status::NotSupported("not init trajectory");
    }

    auto index = _getSegmentIndex(time);

    if (index == -1) {
        if ( time < _segment[0].endTimeSec() ) {
            Vector3D endP = _segment[0].endPoint();
            pos.x = endP.x;
            pos.y = endP.y;
            pos.z = endP.z;
            return Status::OK();
        } else if ( time > _segment.back().endTimeSec()) {
            Vector3D endP = _segment.back().endPoint();
            pos.x = endP.x;
            pos.y = endP.y;
            pos.z = endP.z;
            return Status::OK();
        } else {
            return Status::InvalidArgument("time out of range,"
                                " need to check why this happen");
        }
    }

    Vector4D te_pos;
    Status s = _segment[index].getPositionAt(time, te_pos);
    if ( !s.ok() ) return s;
    pos = te_pos;

    return Status::OK();
}

Status Trajectory::getVelocityAt(float time, Vector4D &vel)
{
    if ( !_init ) {
        return Status::NotSupported("not init trajectory");
    }

    auto index = _getSegmentIndex(time);
    if ( index == -1 ) return Status::InvalidArgument("time out of range");

    Vector4D te_vel;
    Status s = _segment[index].getVelocityAt(time, te_vel);
    if ( !s.ok() ) return s;
    vel = te_vel;

    return Status::OK();
}

Status Trajectory::getAccelAt(float time, Vector4D &acc)
{
    if ( !_init ) {
        return Status::NotSupported("not init trajectory");
    }

    auto index = _getSegmentIndex(time);
    if ( index == -1 ) return Status::InvalidArgument("time out of range");

    Vector4D te_acc;
    Status s = _segment[index].getAccelAt(time, te_acc);
    if ( !s.ok() ) return s;
    acc = te_acc;

    return Status::OK();
}

int Trajectory::_getSegmentIndex(float time)
{
    auto comp = [](const TrajectorySegment &seg, float val) {
        return seg.endTimeSec() < val;
    };

    auto it = std::lower_bound(_segment.begin(), _segment.end(), time, comp);

    if ( it == _segment.end() ) return -1;
    return std::distance(_segment.begin(), it);
}

Status Trajectory::_initMaxMin()
{
    // 遍历得到 max min vel, acc
    Vector4D maxVel, minVel, maxAcc, minAcc;

    if ( _segment.empty() ) {
        return Status::NotSupported("after init inner segment empty");
    }

    _segment[0].getVelMax(maxVel);
    _segment[0].getVelMin(minVel);
    _segment[0].getAccMax(maxAcc);
    _segment[0].getAccMin(minAcc);

    for (int i = 1; i < _segment.size(); ++i) {
        Vector4D te_maxVel, te_minVel, te_maxAcc, te_minAcc;

        _segment[i].getVelMax(te_maxVel);
        _segment[i].getVelMin(te_minVel);
        _segment[i].getAccMax(te_maxAcc);
        _segment[i].getAccMin(te_minAcc);

        maxVel = Vector4D::maxAll(maxVel, te_maxVel);
        minVel = Vector4D::minAll(minVel, te_minVel);
        maxAcc = Vector4D::maxAll(maxAcc, te_maxAcc);
        minAcc = Vector4D::minAll(minAcc, te_minAcc);
    }

    _maxVel = maxVel;
    _minVel = minVel;
    _maxAcc = maxAcc;
    _minAcc = minAcc;

    return Status::OK();
}

Status Trajectory::_initTakeOffLand()
{
    // the first segment's end is the takeoff point
    _takeoffAt = _segment[0].endPoint();

    // the last segment's end is the takeoff point
    _landAt = _segment.back().endPoint();

    return Status::OK();
}


}// libzds

