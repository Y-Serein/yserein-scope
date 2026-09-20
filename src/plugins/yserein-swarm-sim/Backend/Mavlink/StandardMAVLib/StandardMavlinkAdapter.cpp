#include "StandardMavlinkAdapter.h"

#include <algorithm>
#include <cstring>

namespace zds::back::mavlink8 {

namespace {

constexpr uint16_t kStandardGcsSystemId = 255;
constexpr uint16_t kPrivateGcsSystemId = 32767;

uint16_t privateTargetSystem(uint8_t standardSystemId)
{
    return standardSystemId == kStandardGcsSystemId
        ? kPrivateGcsSystemId
        : standardSystemId;
}

void writeU16(uint8_t* data, uint16_t value)
{
    data[0] = static_cast<uint8_t>(value & 0xFF);
    data[1] = static_cast<uint8_t>(value >> 8);
}

void copyRange(uint8_t* destination, size_t destinationOffset,
               const uint8_t* source, size_t sourceOffset, size_t length)
{
    std::memcpy(destination + destinationOffset, source + sourceOffset, length);
}

bool translatePayloadToPrivate(uint32_t messageId,
                               const uint8_t* source,
                               uint8_t sourceLength,
                               uint8_t* destination,
                               uint16_t& destinationLength)
{
    if (sourceLength == 0) return false;
    // The generated standard dialect uses the canonical MAVLink field order.
    // The private dialect widened target_system to uint16_t, which changes
    // wire offsets for these messages.
    switch (messageId) {
    case MAVLINK_MSG_ID_COMMAND_ACK:
        if (sourceLength == 0) return false;
        copyRange(destination, 0, source, 0, 8);
        writeU16(destination + 8, privateTargetSystem(source[8]));
        destination[10] = source[9];
        destinationLength = 11;
        return true;
    case MAVLINK_MSG_ID_COMMAND_LONG:
        copyRange(destination, 0, source, 0, 28);
        writeU16(destination + 28, privateTargetSystem(source[30]));
        copyRange(destination, 30, source, 28, 2);
        copyRange(destination, 32, source, 31, 2);
        destinationLength = 34;
        return true;
    case MAVLINK_MSG_ID_COMMAND_INT:
        copyRange(destination, 0, source, 0, 28);
        writeU16(destination + 28, privateTargetSystem(source[30]));
        copyRange(destination, 30, source, 28, 2);
        copyRange(destination, 32, source, 31, 4);
        destinationLength = 36;
        return true;
    case MAVLINK_MSG_ID_FILE_TRANSFER_PROTOCOL:
        writeU16(destination, privateTargetSystem(source[1]));
        destination[2] = source[0]; // target_network
        destination[3] = source[2]; // target_component
        copyRange(destination, 4, source, 3, 251);
        destinationLength = 255;
        return true;
    case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
        writeU16(destination, privateTargetSystem(source[0]));
        destination[2] = source[1];
        destinationLength = 3;
        return true;
    case MAVLINK_MSG_ID_PARAM_REQUEST_READ:
        writeU16(destination, privateTargetSystem(source[2]));
        copyRange(destination, 2, source, 0, 2);
        destination[4] = source[3];
        copyRange(destination, 5, source, 4, 16);
        destinationLength = 21;
        return true;
    case MAVLINK_MSG_ID_PARAM_SET:
        copyRange(destination, 0, source, 0, 4);
        writeU16(destination + 4, privateTargetSystem(source[4]));
        destination[6] = source[5];
        copyRange(destination, 7, source, 6, 16);
        destination[23] = source[22];
        destinationLength = 24;
        return true;
    case MAVLINK_MSG_ID_REQUEST_DATA_STREAM:
        writeU16(destination, privateTargetSystem(source[2]));
        copyRange(destination, 2, source, 0, 2);
        copyRange(destination, 4, source, 3, 3);
        destinationLength = 7;
        return true;
    case MAVLINK_MSG_ID_LOG_REQUEST_DATA:
        copyRange(destination, 0, source, 0, 8);
        writeU16(destination + 8, privateTargetSystem(source[10]));
        copyRange(destination, 10, source, 8, 2);
        destination[12] = source[11];
        destinationLength = 13;
        return true;
    case MAVLINK_MSG_ID_LOG_REQUEST_END:
        writeU16(destination, privateTargetSystem(source[0]));
        destination[2] = source[1];
        destinationLength = 3;
        return true;
    case MAVLINK_MSG_ID_LOG_REQUEST_LIST:
        writeU16(destination, privateTargetSystem(source[4]));
        copyRange(destination, 2, source, 0, 4);
        destination[6] = source[5];
        destinationLength = 7;
        return true;
    case MAVLINK_MSG_ID_SET_MODE:
        copyRange(destination, 0, source, 0, 4);
        writeU16(destination + 4, privateTargetSystem(source[4]));
        destination[6] = source[5];
        destinationLength = 7;
        return true;
    case MAVLINK_MSG_ID_LED_CONTROL:
        writeU16(destination, privateTargetSystem(source[0]));
        copyRange(destination, 2, source, 1, 28);
        destinationLength = 30;
        return true;
    default: {
        // For unchanged messages, the private decoder can consume the
        // standard payload directly. Its length table is intentionally not
        // used here because MAVLink2 may omit trailing extension bytes.
        const auto* entry = mavlink_get_msg_entry(messageId);
        if (!entry) return false;
        std::memcpy(destination, source, sourceLength);
        destinationLength = sourceLength;
        return true;
    }
    }
}

bool translatePayloadToStandard(uint32_t messageId,
                                const uint8_t* source,
                                uint16_t sourceLength,
                                Packet& packet)
{
    if (sourceLength == 0) return false;
    std::fill(packet.payload.begin(), packet.payload.end(), 0);
    switch (messageId) {
    case MAVLINK_MSG_ID_COMMAND_ACK:
        copyRange(packet.payload.data(), 0, source, 0, 8);
        packet.payload[8] = source[8];
        packet.payload[9] = source[10];
        packet.payloadLength = 10;
        return true;
    case MAVLINK_MSG_ID_COMMAND_LONG:
        copyRange(packet.payload.data(), 0, source, 0, 28);
        copyRange(packet.payload.data(), 28, source, 30, 2);
        packet.payload[30] = source[28];
        packet.payload[31] = source[32];
        packet.payload[32] = source[33];
        packet.payloadLength = 33;
        return true;
    case MAVLINK_MSG_ID_COMMAND_INT:
        copyRange(packet.payload.data(), 0, source, 0, 28);
        copyRange(packet.payload.data(), 28, source, 30, 2);
        packet.payload[30] = source[28];
        packet.payload[31] = source[32];
        copyRange(packet.payload.data(), 32, source, 33, 3);
        packet.payloadLength = 35;
        return true;
    case MAVLINK_MSG_ID_FILE_TRANSFER_PROTOCOL:
        packet.payload[0] = source[2];
        packet.payload[1] = source[0];
        packet.payload[2] = source[3];
        copyRange(packet.payload.data(), 3, source, 4, 251);
        packet.payloadLength = 254;
        return true;
    case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
        packet.payload[0] = source[0];
        packet.payload[1] = source[2];
        packet.payloadLength = 2;
        return true;
    case MAVLINK_MSG_ID_PARAM_REQUEST_READ:
        copyRange(packet.payload.data(), 0, source, 2, 2);
        packet.payload[2] = source[0];
        packet.payload[3] = source[4];
        copyRange(packet.payload.data(), 4, source, 5, 16);
        packet.payloadLength = 20;
        return true;
    case MAVLINK_MSG_ID_PARAM_SET:
        copyRange(packet.payload.data(), 0, source, 0, 4);
        packet.payload[4] = source[4];
        packet.payload[5] = source[6];
        copyRange(packet.payload.data(), 6, source, 7, 16);
        packet.payload[22] = source[23];
        packet.payloadLength = 23;
        return true;
    case MAVLINK_MSG_ID_REQUEST_DATA_STREAM:
        copyRange(packet.payload.data(), 0, source, 2, 2);
        packet.payload[2] = source[0];
        packet.payload[3] = source[4];
        copyRange(packet.payload.data(), 4, source, 5, 2);
        packet.payloadLength = 6;
        return true;
    case MAVLINK_MSG_ID_LOG_REQUEST_DATA:
        copyRange(packet.payload.data(), 0, source, 0, 8);
        copyRange(packet.payload.data(), 8, source, 10, 2);
        packet.payload[10] = source[8];
        packet.payload[11] = source[12];
        packet.payloadLength = 12;
        return true;
    case MAVLINK_MSG_ID_LOG_REQUEST_END:
        packet.payload[0] = source[0];
        packet.payload[1] = source[2];
        packet.payloadLength = 2;
        return true;
    case MAVLINK_MSG_ID_LOG_REQUEST_LIST:
        copyRange(packet.payload.data(), 0, source, 2, 4);
        packet.payload[4] = source[0];
        packet.payload[5] = source[6];
        packet.payloadLength = 6;
        return true;
    case MAVLINK_MSG_ID_SET_MODE:
        copyRange(packet.payload.data(), 0, source, 0, 4);
        packet.payload[4] = source[4];
        packet.payload[5] = source[6];
        packet.payloadLength = 6;
        return true;
    case MAVLINK_MSG_ID_LED_CONTROL:
        packet.payload[0] = source[0];
        copyRange(packet.payload.data(), 1, source, 2, 28);
        packet.payloadLength = 29;
        return true;
    default: {
        const auto* entry = mavlink_get_msg_entry(messageId);
        if (!entry || sourceLength > entry->max_msg_len || sourceLength > 255) return false;
        std::memcpy(packet.payload.data(), source, sourceLength);
        packet.payloadLength = static_cast<uint8_t>(sourceLength);
        return true;
    }
    }
}

} // namespace

bool decodeToPrivate(const Packet& packet, ::mavlink_message_t& message)
{
    if (packet.systemId == 0) return false;

    message = ::mavlink_message_t{};
    message.magic = packet.version == WireVersion::Mavlink1
        ? MAVLINK_STX_MAVLINK1
        : MAVLINK_STX;
    message.incompat_flags = packet.incompatFlags;
    message.compat_flags = packet.compatFlags;
    message.seq = packet.sequence;
    message.sysid = packet.systemId;
    message.compid = packet.componentId;
    message.msgid = packet.messageId;
    uint16_t privateLength = 0;
    if (!translatePayloadToPrivate(
            packet.messageId,
            packet.payload.data(),
            packet.payloadLength,
            reinterpret_cast<uint8_t*>(&message.payload64[0]),
            privateLength)) {
        return false;
    }
    message.len = privateLength;
    return true;
}

bool encodeFromPrivate(const ::mavlink_message_t& message,
                       mavlink::StandardWireVersion version,
                       std::vector<uint8_t>& bytes)
{
    if (message.sysid > 255 || message.sysid == 0) {
        if (message.sysid != 32767) return false;
    }

    Packet packet{};
    packet.version = version == mavlink::StandardWireVersion::Mavlink1
        ? WireVersion::Mavlink1
        : WireVersion::Mavlink2;
    packet.sequence = message.seq;
    packet.systemId = message.sysid == 32767
        ? kStandardGcsSystemId
        : static_cast<uint8_t>(message.sysid);
    packet.componentId = message.compid;
    packet.messageId = message.msgid;
    if (!translatePayloadToStandard(
            message.msgid,
            reinterpret_cast<const uint8_t*>(&message.payload64[0]),
            message.len,
            packet)) {
        return false;
    }
    return encode(packet, packet.version, bytes);
}

} // namespace zds::back::mavlink8
