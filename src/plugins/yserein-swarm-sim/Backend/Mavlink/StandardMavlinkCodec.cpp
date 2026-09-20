#include "StandardMavlinkCodec.h"

#include "Backend/Mavlink/MavlinkProtocol.h"
#include "Backend/Mavlink/StandardMAVLib/StandardMavlinkAdapter.h"
#include "Backend/Mavlink/StandardMAVLib/StandardMavlinkWire.h"

namespace zsm {

bool encodeStandard8Packet(
    const struct __mavlink_message& message,
    WireVersion version,
    std::vector<uint8_t>& bytes) {
    auto v = version == WireVersion::Mavlink1
        ? zds::back::mavlink::StandardWireVersion::Mavlink1
        : zds::back::mavlink::StandardWireVersion::Mavlink2;
    return zds::back::mavlink8::encodeFromPrivate(message, v, bytes);
}

class Standard8Decoder::Impl {
public:
    zds::back::mavlink8::Parser parser;
};

Standard8Decoder::Standard8Decoder() : m_impl(std::make_unique<Impl>()) {}
Standard8Decoder::~Standard8Decoder() = default;
Standard8Decoder::Standard8Decoder(Standard8Decoder&&) noexcept = default;
Standard8Decoder& Standard8Decoder::operator=(Standard8Decoder&&) noexcept = default;

bool Standard8Decoder::feed(uint8_t byte, struct __mavlink_message& out) {
    zds::back::mavlink8::Packet packet{};
    if (m_impl->parser.feed(byte, packet) == zds::back::mavlink8::ParseResult::Ok) {
        return zds::back::mavlink8::decodeToPrivate(packet, out);
    }
    return false;
}

void Standard8Decoder::reset() { m_impl->parser.reset(); }
bool Standard8Decoder::isIdle() const { return m_impl->parser.isIdle(); }

} // namespace zsm
