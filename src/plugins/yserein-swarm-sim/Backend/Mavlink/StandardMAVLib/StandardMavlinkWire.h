#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace zds::back::mavlink8 {

enum class WireVersion : uint8_t {
    Mavlink1 = 1,
    Mavlink2 = 2,
};

struct Packet {
    WireVersion version{WireVersion::Mavlink2};
    uint8_t sequence{0};
    uint8_t systemId{0};
    uint8_t componentId{0};
    uint32_t messageId{0};
    uint8_t incompatFlags{0};
    uint8_t compatFlags{0};
    uint8_t payloadLength{0};
    std::array<uint8_t, 255> payload{};
    std::array<uint8_t, 13> signature{};
    bool hasSignature{false};
};

enum class ParseResult : uint8_t {
    Incomplete,
    Ok,
    BadCrc,
    BadSignature,
};

/**
 * Isolated wrapper around the standard ArduPilot-generated MAVLink dialect.
 * The generated C headers are intentionally not exposed to the main codebase;
 * this keeps their 8-bit wire types separate from the private 16-bit dialect.
 */
class Parser final {
public:
    Parser();
    ~Parser();

    Parser(Parser&&) noexcept;
    Parser& operator=(Parser&&) noexcept;

    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;

    ParseResult feed(uint8_t byte, Packet& packet);
    void reset();
    bool isIdle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Serializes a standard packet as MAVLink 1 or MAVLink 2.
bool encode(const Packet& packet, WireVersion version, std::vector<uint8_t>& bytes);

} // namespace zds::back::mavlink8
