#pragma once

#include <cstdint>
#include <memory>
#include <vector>

// Forward declaration only: the full type comes from the shared
// <mavlink_types.h> pulled in by whichever MavlinkLib.h the consuming TU
// includes. Never include a dialect wrapper here, or the private 16-bit and
// standard 8-bit copies of mavlink channel/macro declarations will collide.
struct __mavlink_message;

namespace zsm {

/// Standard MAVLink wire version to emit.
enum class WireVersion : uint8_t {
    Mavlink1 = 1,
    Mavlink2 = 2,
};

/// Encodes a private logical message as standard 8-bit MAVLink wire bytes.
/// Returns false when the message cannot be translated to the standard dialect
/// (e.g. an unknown or private-only message ID).
bool encodeStandard8Packet(
    const struct __mavlink_message& message,
    WireVersion version,
    std::vector<uint8_t>& bytes);

/// Stateful decoder for an inbound stream of standard MAVLink 1/2 bytes.
/// Feed bytes one at a time; each call returns true when a complete and
/// successfully translated message is available in `out`.
class Standard8Decoder {
public:
    Standard8Decoder();
    ~Standard8Decoder();
    Standard8Decoder(Standard8Decoder&&) noexcept;
    Standard8Decoder& operator=(Standard8Decoder&&) noexcept;
    Standard8Decoder(const Standard8Decoder&) = delete;
    Standard8Decoder& operator=(const Standard8Decoder&) = delete;

    bool feed(uint8_t byte, struct __mavlink_message& out);
    void reset();
    bool isIdle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace zsm
