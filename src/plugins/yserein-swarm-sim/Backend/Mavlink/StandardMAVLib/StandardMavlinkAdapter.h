#pragma once

#include <vector>

#include "Backend/Mavlink/MavlinkLib.h"
#include "Backend/Mavlink/MavlinkProtocol.h"
#include "Backend/Mavlink/StandardMAVLib/StandardMavlinkWire.h"

namespace zds::back::mavlink8 {

/**
 * Converts between the private logical message representation used by Vehicle
 * and the standard 8-bit MAVLink wire representation. The conversion is
 * field-layout aware for messages whose private dialect widened target_system;
 * messages with an unknown or private-only ID are rejected instead of being
 * sent with a corrupted payload.
 */
bool decodeToPrivate(const Packet& packet, ::mavlink_message_t& message);

bool encodeFromPrivate(
    const ::mavlink_message_t& message,
    mavlink::StandardWireVersion version,
    std::vector<uint8_t>& bytes);

inline mavlink::StandardWireVersion toStandardWireVersion(WireVersion version)
{
    return version == WireVersion::Mavlink1
        ? mavlink::StandardWireVersion::Mavlink1
        : mavlink::StandardWireVersion::Mavlink2;
}

} // namespace zds::back::mavlink8
