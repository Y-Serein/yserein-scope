# Standard MAVLink 1/2 codec

`generated/` is a checked-in, isolated C MAVLink dialect snapshot used only by
`StandardMavlinkWire.cpp`. It is generated from the ArduPilot CMCopter SITL
tree at:

```text
/home/yserein/yserein-swarm/ardupilot/build/sitl/libraries/GCS_MAVLink/include/mavlink/v2.0
```

The snapshot is the standard `ardupilotmega` dialect and uses the normal
8-bit MAVLink system ID and payload layout. It accepts MAVLink 1 (`0xFE`) and
MAVLink 2 (`0xFD`) frames. The main application must not include generated
headers directly because the existing `Backend/Mavlink/MAVLib` dialect is a
private 16-bit wire format with incompatible struct layouts and CRC extras.

`StandardMavlinkAdapter.cpp` translates only at the codec boundary. Messages
whose private dialect widened `target_system` are mapped field-by-field; an
unknown message ID is rejected rather than sent with an unverified payload.

