#include "StandardMavlinkAdapter.h"

#include "Backend/Mavlink/StandardMAVLib/generated/LayoutTable.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace zds::back::mavlink8 {

namespace {

constexpr uint16_t kStandardGcsSystemId = 255;
constexpr uint16_t kPrivateGcsSystemId = 32767;

// Sentinel for a dialect-only field that has no counterpart in the standard
// payload. Fields flagged with this source offset are zero-filled when mapping
// standard -> private and dropped when mapping private -> standard.
constexpr uint16_t kNoStandardSource = 0xFFFF;

const MessageLayout* findLayout(uint32_t msgId)
{
    const auto* begin = kMessageLayouts;
    const auto* end = kMessageLayouts + kMessageLayoutCount;
    while (begin < end) {
        const auto* mid = begin + (end - begin) / 2;
        if (mid->msgId == msgId) {
            return mid;
        }
        if (mid->msgId < msgId) {
            begin = mid + 1;
        } else {
            end = mid;
        }
    }
    return nullptr;
}

bool hasSameLayout(uint32_t msgId)
{
    for (uint16_t id : kSameLayoutMsgIds) {
        if (id == msgId) {
            return true;
        }
    }
    return false;
}

uint16_t readLittleEndian(const uint8_t* data, uint16_t size)
{
    uint16_t value = 0;
    for (uint16_t i = 0; i < size && i < 2; ++i) {
        value |= static_cast<uint16_t>(data[i]) << (8 * i);
    }
    return value;
}

void writeLittleEndian(uint8_t* data, uint16_t size, uint16_t value)
{
    for (uint16_t i = 0; i < size && i < 2; ++i) {
        data[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

// Maps a single field from a standard payload into a private payload. Dialect-only
// fields are pre-zeroed by the caller. Returns false on out-of-bounds access.
bool applyToPrivate(const LayoutStep& step,
                    const uint8_t* stdPayload,
                    uint16_t stdLen,
                    uint8_t* privPayload,
                    uint16_t privCap)
{
    if (step.stdOffset == kNoStandardSource) {
        return true;
    }
    if (step.stdOffset > stdLen || step.stdSize > stdLen - step.stdOffset) {
        return false;
    }
    if (step.privOffset > privCap || step.privSize > privCap - step.privOffset) {
        return false;
    }

    if (step.stdSize == step.privSize) {
        std::memcpy(privPayload + step.privOffset, stdPayload + step.stdOffset, step.stdSize);
        return true;
    }
    if (step.stdSize != 1 || step.privSize != 2) return false;
    uint16_t value = readLittleEndian(stdPayload + step.stdOffset, step.stdSize);
    if (step.isTargetSystem) {
        if (value == kStandardGcsSystemId) {
            value = kPrivateGcsSystemId;
        } else if (value > kStandardGcsSystemId) {
            // Not representable in the standard domain.
            return false;
        }
    }
    writeLittleEndian(privPayload + step.privOffset, step.privSize, value);
    return true;
}

// Inverse of applyToPrivate: maps a single field from a private payload back into
// a standard payload. Dialect-only fields are dropped. Returns false on
// out-of-bounds access.
bool applyToStandard(const LayoutStep& step,
                     const uint8_t* privPayload,
                     uint16_t privLen,
                     uint8_t* stdPayload,
                     uint16_t stdCap)
{
    if (step.stdOffset == kNoStandardSource) {
        return true;
    }
    if (step.privOffset > privLen || step.privSize > privLen - step.privOffset) {
        return false;
    }
    if (step.stdOffset > stdCap || step.stdSize > stdCap - step.stdOffset) {
        return false;
    }

    if (step.stdSize == step.privSize) {
        std::memcpy(stdPayload + step.stdOffset, privPayload + step.privOffset, step.stdSize);
        return true;
    }
    if (step.stdSize != 1 || step.privSize != 2) return false;
    uint16_t value = readLittleEndian(privPayload + step.privOffset, step.privSize);
    if (step.isTargetSystem) {
        if (value == kPrivateGcsSystemId) {
            value = kStandardGcsSystemId;
        } else if (value > kStandardGcsSystemId) {
            // A private id above 255 has no 8-bit standard representation.
            return false;
        }
    }
    if (value > 255) return false;
    writeLittleEndian(stdPayload + step.stdOffset, step.stdSize, value);
    return true;
}

} // namespace

bool decodeToPrivate(const Packet& packet, ::mavlink_message_t& message)
{
    if (packet.systemId == 0) {
        return false;
    }

    message = ::mavlink_message_t{};
    message.magic = packet.version == WireVersion::Mavlink1
        ? MAVLINK_STX_MAVLINK1
        : MAVLINK_STX;
    message.incompat_flags = packet.incompatFlags;
    message.compat_flags = packet.compatFlags;
    message.seq = packet.sequence;
    // Symmetric with encodeFromPrivate: the standard GCS id maps to the private
    // reserved id when crossing back into the private domain.
    message.sysid = packet.systemId == kStandardGcsSystemId
        ? kPrivateGcsSystemId
        : packet.systemId;
    message.compid = packet.componentId;
    message.msgid = packet.messageId;

    uint8_t* privPayload = reinterpret_cast<uint8_t*>(&message.payload64[0]);
    const uint16_t privCap = static_cast<uint16_t>(sizeof(message.payload64));
    uint16_t privateLength = 0;

    if (const auto* layout = findLayout(packet.messageId)) {
        if (packet.payloadLength > layout->stdLen) return false;
        // MAVLink2 may trim any trailing zero bytes. The packet payload is
        // zero-initialized by the parser, so map the complete logical layout.
        // Clear the whole private payload so dialect-only fields end up zeroed.
        std::fill(privPayload, privPayload + privCap, 0);
        for (uint16_t i = 0; i < layout->fieldCount; ++i) {
            if (!applyToPrivate(layout->steps[i],
                                packet.payload.data(),
                                layout->stdLen,
                                privPayload,
                                privCap)) {
                return false;
            }
        }
        privateLength = layout->privLen;
    } else if (hasSameLayout(packet.messageId)) {
        // Struct layouts are identical; copy the payload verbatim.
        if (packet.payloadLength > privCap) {
            return false;
        }
        std::memcpy(privPayload, packet.payload.data(), packet.payloadLength);
        privateLength = packet.payloadLength;
    } else {
        // Neither a known layout delta nor a confirmed identical layout. Reject
        // rather than shipping a payload that may be silently corrupted.
        return false;
    }

    message.len = privateLength;
    return true;
}

bool encodeFromPrivate(const ::mavlink_message_t& message,
                       mavlink::StandardWireVersion version,
                       std::vector<uint8_t>& bytes)
{
    // The private dialect reserves 32767 for the GCS. Any other id must fit the
    // standard 8-bit domain to be representable on the wire.
    const uint16_t systemId = message.sysid;
    if (systemId != kPrivateGcsSystemId
        && (systemId == 0 || systemId > kStandardGcsSystemId)) {
        return false;
    }

    Packet packet{};
    packet.version = version == mavlink::StandardWireVersion::Mavlink1
        ? WireVersion::Mavlink1
        : WireVersion::Mavlink2;
    packet.sequence = message.seq;
    packet.systemId = systemId == kPrivateGcsSystemId
        ? kStandardGcsSystemId
        : static_cast<uint8_t>(systemId);
    packet.componentId = message.compid;
    packet.messageId = message.msgid;

    const uint8_t* privPayload = reinterpret_cast<const uint8_t*>(&message.payload64[0]);
    const uint16_t privLen = static_cast<uint16_t>(message.len);
    const uint16_t stdCap = static_cast<uint16_t>(packet.payload.size());

    if (const auto* layout = findLayout(message.msgid)) {
        if (privLen > layout->privLen) return false;
        std::array<uint8_t, sizeof(message.payload64)> padded{};
        std::memcpy(padded.data(), privPayload, privLen);
        std::fill(packet.payload.begin(), packet.payload.end(), 0);
        for (uint16_t i = 0; i < layout->fieldCount; ++i) {
            if (!applyToStandard(layout->steps[i],
                                 padded.data(),
                                 layout->privLen,
                                 packet.payload.data(),
                                 stdCap)) {
                return false;
            }
        }
        packet.payloadLength = layout->stdLen;
    } else if (hasSameLayout(message.msgid)) {
        if (privLen > stdCap) {
            return false;
        }
        std::memcpy(packet.payload.data(), privPayload, privLen);
        packet.payloadLength = static_cast<uint8_t>(privLen);
    } else {
        return false;
    }

    return encode(packet, packet.version, bytes);
}

} // namespace zds::back::mavlink8
