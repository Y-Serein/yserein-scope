#pragma once

#include <cstdint>
#include <QString>

namespace zsm {

/// Wire protocol domain used by the simulator when talking to a ground station.
/// Private16 is the legacy 16-bit dialect; Standard8 selects the standard
/// MAVLink 1/2 dialect with 8-bit system IDs.
enum class ProtocolDomain : uint8_t {
    Private16,
    Standard8,
};

/// Returns the active protocol domain. Defaults to Private16 unless configured
/// by the persisted Mavlink/ProtocolDomain setting.
ProtocolDomain currentProtocolDomain();

/// Reads or updates the domain used on the next Scope start. Changing it does
/// not change an already running simulator session.
ProtocolDomain configuredProtocolDomain();
void configureProtocolDomainForNextStart(ProtocolDomain domain);

/// Sets the active protocol domain and persists it to QSettings.
void setProtocolDomain(ProtocolDomain domain);

/// Parses a user-facing value ("private16" / "standard8") into a domain.
/// Returns true on success.
bool parseProtocolDomain(const QString& value, ProtocolDomain& out);

} // namespace zsm

namespace zds::back::mavlink {

/// Dialect family selected on the wire.
enum class ProtocolFamily : uint8_t {
    Unknown,
    Private16,
    Standard8,
};

/// MAVLink 1/2 wire version emitted by the standard 8-bit codec.
enum class StandardWireVersion : uint8_t {
    Unknown = 0,
    Mavlink1 = 1,
    Mavlink2 = 2,
};

} // namespace zds::back::mavlink
