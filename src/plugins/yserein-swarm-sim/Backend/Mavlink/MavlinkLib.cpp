/**
 * @file MavlinkLib.cpp
 * @author hoppz (hoppz520@gmail.com)
 * @brief 
 * @version 0.1
 * @date 2025-03-17
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "MAVLinkLib.h"
#include <QtCore/QDebug>

//! from QGCMAVLink
#ifdef MAVLINK_EXTERNAL_RX_STATUS
    mavlink_status_t m_mavlink_status[MAVLINK_COMM_NUM_BUFFERS];
#endif

#ifdef MAVLINK_GET_CHANNEL_STATUS
mavlink_status_t* mavlink_get_channel_status(uint8_t channel)
{
#ifndef MAVLINK_EXTERNAL_RX_STATUS
    static QList<mavlink_status_t> m_mavlink_status(MAVLINK_COMM_NUM_BUFFERS);
#endif
    // if (!QGCMAVLink::isValidChannel(channel)) {
    //     qCWarning(QGCMAVLinkLog) << Q_FUNC_INFO << "Invalid Channel Number:" << channel;
    //     return nullptr;
    // }

    if( ! (channel < MAVLINK_COMM_NUM_BUFFERS) ) {
        qWarning() << "[Warning]" << "[mavlink_get_channel_status]" << "Invalid Channel Number:" << channel;
        return nullptr;
    }

    return &m_mavlink_status[channel];
}
#endif
//! from QGCMAVLink