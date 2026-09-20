#include "StandardMavlinkWire.h"

#include <algorithm>
#include <cstring>

// This translation unit is deliberately the only place which includes the
// standard generated C headers. Do not include them from project headers.
#define MAVLINK_NO_SIGNATURE_CHECK
#define MAVLINK_NO_SIGN_PACKET
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include "generated/ardupilotmega/mavlink.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace zds::back::mavlink8 {

namespace {

void copyPacket(const mavlink_message_t& source, Packet& destination)
{
    destination.version = source.magic == MAVLINK_STX_MAVLINK1
        ? WireVersion::Mavlink1
        : WireVersion::Mavlink2;
    destination.sequence = source.seq;
    destination.systemId = source.sysid;
    destination.componentId = source.compid;
    destination.messageId = source.msgid;
    destination.incompatFlags = source.incompat_flags;
    destination.compatFlags = source.compat_flags;
    destination.payloadLength = source.len;
    std::fill(destination.payload.begin(), destination.payload.end(), 0);
    std::memcpy(destination.payload.data(),
                &source.payload64[0],
                std::min<size_t>(source.len, destination.payload.size()));
    destination.hasSignature = source.magic != MAVLINK_STX_MAVLINK1
        && (source.incompat_flags & MAVLINK_IFLAG_SIGNED) != 0;
    std::fill(destination.signature.begin(), destination.signature.end(), 0);
    if (destination.hasSignature) {
        std::memcpy(destination.signature.data(), source.signature,
                    destination.signature.size());
    }
}

} // namespace

struct Parser::Impl {
    mavlink_message_t receiveMessage{};
    mavlink_status_t status{};
};

Parser::Parser()
    : m_impl(std::make_unique<Impl>())
{
}

Parser::~Parser() = default;
Parser::Parser(Parser&&) noexcept = default;
Parser& Parser::operator=(Parser&&) noexcept = default;

ParseResult Parser::feed(uint8_t byte, Packet& packet)
{
    mavlink_message_t parsed{};
    mavlink_status_t parsedStatus{};
    const uint8_t result = mavlink_frame_char_buffer(
        &m_impl->receiveMessage, &m_impl->status, byte, &parsed, &parsedStatus);

    switch (result) {
    case MAVLINK_FRAMING_OK:
        copyPacket(parsed, packet);
        return ParseResult::Ok;
    case MAVLINK_FRAMING_BAD_CRC:
        return ParseResult::BadCrc;
    case MAVLINK_FRAMING_BAD_SIGNATURE:
        return ParseResult::BadSignature;
    default:
        return ParseResult::Incomplete;
    }
}

void Parser::reset()
{
    m_impl->receiveMessage = mavlink_message_t{};
    m_impl->status = mavlink_status_t{};
    m_impl->status.parse_state = MAVLINK_PARSE_STATE_IDLE;
}

bool Parser::isIdle() const
{
    return m_impl->status.parse_state == MAVLINK_PARSE_STATE_IDLE;
}

bool encode(const Packet& packet, WireVersion version, std::vector<uint8_t>& bytes)
{
    if (packet.systemId == 0
        || (packet.messageId > 255 && version == WireVersion::Mavlink1)) {
        return false;
    }

    mavlink_message_t message{};
    message.msgid = packet.messageId;
    message.incompat_flags = packet.incompatFlags;
    message.compat_flags = packet.compatFlags;
    message.seq = packet.sequence;
    message.len = packet.payloadLength;
    std::memcpy(&message.payload64[0], packet.payload.data(), packet.payloadLength);

    const mavlink_msg_entry_t* entry = mavlink_get_msg_entry(message.msgid);
    if (!entry || packet.payloadLength > entry->max_msg_len) {
        return false;
    }

    mavlink_status_t status{};
    status.parse_state = MAVLINK_PARSE_STATE_IDLE;
    status.current_tx_seq = packet.sequence;
    if (version == WireVersion::Mavlink1) {
        status.flags |= MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
    }

    const uint16_t length = mavlink_finalize_message_buffer(
        &message,
        packet.systemId,
        packet.componentId,
        &status,
        entry->min_msg_len,
        packet.payloadLength,
        entry->crc_extra);
    if (length == 0) {
        return false;
    }

    std::array<uint8_t, MAVLINK_MAX_PACKET_LEN> buffer{};
    const uint16_t serializedLength = mavlink_msg_to_send_buffer(buffer.data(), &message);
    if (serializedLength == 0) {
        return false;
    }
    bytes.assign(buffer.begin(), buffer.begin() + serializedLength);
    return true;
}

} // namespace zds::back::mavlink8
