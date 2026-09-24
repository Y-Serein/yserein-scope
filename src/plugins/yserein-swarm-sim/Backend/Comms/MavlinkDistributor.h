//
// Created by Hoppz on 25-6-27.
//
#pragma once

#include <QObject>
#include <QByteArray>
#include <unordered_map>
#include <unordered_set>

#include "readerwriterqueue.h"
#include "MavlinkLib.h"
#include "MavlinkProtocol.h"
#include "StandardMavlinkCodec.h"

class MockVehicleWorker;

class MavlinkDistributor : public QObject {
    Q_OBJECT

public:
    explicit MavlinkDistributor(QObject *parent = nullptr);
    ~MavlinkDistributor() override;

    static MavlinkDistributor* instance();
    void init();

    /// @brief 清空对 MockVehicleWorker 的弱引用集合。
    /// 在 shutdown 车辆池之前调用，避免残留指向已删除 worker 的悬空指针。
    void clear();

    void enqueueMsg(QByteArray& data);

private slots:
    void _handleVehicleAdd(MockVehicleWorker* worker, uint16_t vehicleId);

    void _10HzLoop();


private:
    void _dequeueTask();

    void _transmitToVehicleWorker(const mavlink_message_t& msg);

    /// from APM/libraries/GCS_MAVLink/MAVLink_routing.cpp/MAVLink_routing::get_targets
    void _getTarget(const mavlink_message_t& msg, uint16_t& sysid, uint16_t& compid);


private:

    moodycamel::ReaderWriterQueue<QByteArray> _msgQueue;
    std::unordered_map<uint16_t, MockVehicleWorker*> _vehicleWorkerMap;
    std::unordered_set<MockVehicleWorker*> _vehicleWorkerSet;
    zsm::Standard8Decoder m_standardDecoder;
    mavlink_status_t m_status{};
};


