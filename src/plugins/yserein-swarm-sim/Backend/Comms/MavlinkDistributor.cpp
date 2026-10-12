//
// Created by Hoppz on 25-6-27.
//

#include "MavlinkDistributor.h"

#include <MockVehicleWorker.h>

#include "ZSMLoggingCategory.h"
#include "MockVehicleManager.h"
#include "ZSMBackend.h"

#include <qapplicationstatic.h>

Q_APPLICATION_STATIC(MavlinkDistributor,_mavlinkDistributroInstance)
ZSM_LOGGING_CATEGORY(MavlinkDistributorLog, "Backend.Comms.MavlinkDistributor")

MavlinkDistributor::MavlinkDistributor(QObject *parent)
    : QObject(parent)
{
    zInfo(MavlinkDistributorLog) << "create";
}

MavlinkDistributor::~MavlinkDistributor()
{
    zInfo(MavlinkDistributorLog) << "destory";
}

MavlinkDistributor * MavlinkDistributor::instance()
{
    return _mavlinkDistributroInstance;
}

void MavlinkDistributor::init()
{
    connect(MockVehicleManager::instance(), &MockVehicleManager::addMockVehicleSignal,this,&MavlinkDistributor::_handleVehicleAdd);
    connect(zsmApp(), &ZSMBackend::back10HzSignal,this, &MavlinkDistributor::_10HzLoop);
}

void MavlinkDistributor::clear()
{
    _vehicleWorkerMap.clear();
    _vehicleWorkerSet.clear();
    QByteArray data;
    while (_msgQueue.try_dequeue(data)) {}
    m_standardDecoder.reset();
    mavlink_reset_channel_status(ZSM_MAVLINK_COMM);
    m_status = {};
}

void MavlinkDistributor::enqueueMsg(QByteArray &data)
{
    _msgQueue.enqueue(data);
}

void MavlinkDistributor::_handleVehicleAdd(MockVehicleWorker *worker, uint16_t vehicleId)
{
    if ( !_vehicleWorkerMap.contains(vehicleId) ) {
        _vehicleWorkerMap[vehicleId] = worker;

        if (  !_vehicleWorkerSet.contains(worker) ) _vehicleWorkerSet.insert(worker) ;
    }
}

void MavlinkDistributor::_10HzLoop()
{
    _dequeueTask();
}

void MavlinkDistributor::_dequeueTask()
{
    QByteArray data;
    while ( _msgQueue.try_dequeue(data) ) {

        for (const uint8_t& byte : data) {

            if (zsm::currentProtocolDomain() == zsm::ProtocolDomain::Standard8) {
                mavlink_message_t logicalMessage{};
                if (m_standardDecoder.feed(byte, logicalMessage)) {
                    _transmitToVehicleWorker(logicalMessage);
                }
                continue;
            }

            mavlink_message_t message{};
            if (mavlink_parse_char(ZSM_MAVLINK_COMM, byte, &message, &m_status) != MAVLINK_FRAMING_OK) {
                continue;
            }

            _transmitToVehicleWorker(message);
        }

    }
}

void MavlinkDistributor::_transmitToVehicleWorker(const mavlink_message_t &msg)
{
    if ( msg.msgid ) zDebug(MavlinkDistributorLog) << "get Msg:" << msg.msgid;

    uint16_t target_system = 0;
    uint16_t target_component = 0;
    _getTarget(msg,target_system,target_component);

    const bool broadcast_system = target_system == 0;

    // A command addressed to an unknown vehicle must not reach every vehicle.
    if (!broadcast_system) {
        auto it = _vehicleWorkerMap.find(target_system);
        if (it != _vehicleWorkerMap.end()) {
            it->second->messageReceived(msg, target_system);
        }
        return;
    }

    // Only protocol broadcasts are forwarded to every worker.
    for (const auto& worker : _vehicleWorkerSet) {
        worker->messageReceived(msg);
    }




}

void MavlinkDistributor::_getTarget(const mavlink_message_t &msg, uint16_t &sysid, uint16_t &compid)
{
    const mavlink_msg_entry_t *msg_entry = mavlink_get_msg_entry(msg.msgid);
    if ( msg_entry == nullptr ) {
        return;
    }

    if (msg_entry->flags & MAV_MSG_ENTRY_FLAG_HAVE_TARGET_SYSTEM) {
        // hoppz modify: adjust our uin16_t sysId (only used as int16_t )  target_system
        // sysid = _MAV_RETURN_uint8_t(&msg,  msg_entry->target_system_ofs);
        sysid = _MAV_RETURN_uint16_t(&msg, msg_entry->target_system_ofs);
        // gcs().send_text(MAV_SEVERITY_DEBUG,"te_sysid: %d",te_sysid);
    }
    if (msg_entry->flags & MAV_MSG_ENTRY_FLAG_HAVE_TARGET_COMPONENT) {
        compid = _MAV_RETURN_uint8_t(&msg,  msg_entry->target_component_ofs);
    }
}
