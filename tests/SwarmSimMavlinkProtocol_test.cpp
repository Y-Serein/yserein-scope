#include "src/plugins/yserein-swarm-sim/Backend/Mavlink/MavlinkLib.h"
#include "src/plugins/yserein-swarm-sim/Backend/Mavlink/StandardMavlinkCodec.h"
#include "src/plugins/yserein-swarm-sim/Backend/Mavlink/StandardMAVLib/StandardMavlinkAdapter.h"

#include <QtTest/QTest>

#include <algorithm>
#include <iterator>
#include <vector>

class SwarmSimMavlinkProtocolTest final : public QObject {
    Q_OBJECT
private slots:
    void periodicTelemetryUsesStandardWire();
    void commandLongTargetRoundTrip();
    void batteryAndShowStatusRoundTrip();
    void overlongPayloadAndUnknownMessageRejected();
};

void SwarmSimMavlinkProtocolTest::periodicTelemetryUsesStandardWire()
{
    for (uint16_t id : {uint16_t(1), uint16_t(250)}) {
        mavlink_message_t heartbeat{};
        mavlink_msg_heartbeat_pack_chan(id, MAV_COMP_ID_AUTOPILOT1, MAVLINK_COMM_0,
            &heartbeat, MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_ARDUPILOTMEGA,
            MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, 3, MAV_STATE_STANDBY);

        mavlink_message_t position{};
        mavlink_msg_global_position_int_pack_chan(id, MAV_COMP_ID_AUTOPILOT1, MAVLINK_COMM_0,
            &position, 1000, 229786653, 1134589746, 1000, 1000, 0, 0, 0, UINT16_MAX);

        for (const mavlink_message_t& logical : {heartbeat, position}) {
            std::vector<uint8_t> bytes;
            QVERIFY(zsm::encodeStandard8Packet(logical, zsm::WireVersion::Mavlink2, bytes));
            QVERIFY(!bytes.empty());
            QCOMPARE(bytes.front(), uint8_t(MAVLINK_STX));
            zds::back::mavlink8::Parser parser;
            zds::back::mavlink8::Packet packet{};
            bool parsed = false;
            for (uint8_t byte : bytes) {
                parsed |= parser.feed(byte, packet) == zds::back::mavlink8::ParseResult::Ok;
            }
            QVERIFY(parsed);
            QCOMPARE(packet.systemId, static_cast<uint8_t>(id));
            QCOMPARE(packet.messageId, logical.msgid);
            mavlink_message_t decoded{};
            QVERIFY(zds::back::mavlink8::decodeToPrivate(packet, decoded));
            if (logical.msgid == MAVLINK_MSG_ID_GLOBAL_POSITION_INT) {
                mavlink_global_position_int_t originalPosition{}, decodedPosition{};
                mavlink_msg_global_position_int_decode(&position, &originalPosition);
                mavlink_msg_global_position_int_decode(&decoded, &decodedPosition);
                QCOMPARE(decodedPosition.lat, originalPosition.lat);
                QCOMPARE(decodedPosition.lon, originalPosition.lon);
            }
        }
    }
}

void SwarmSimMavlinkProtocolTest::commandLongTargetRoundTrip()
{
    mavlink_message_t command{};
    mavlink_msg_command_long_pack_chan(32767, 190, MAVLINK_COMM_0, &command,
        42, MAV_COMP_ID_AUTOPILOT1, MAV_CMD_COMPONENT_ARM_DISARM, 0,
        1.0f, 0, 0, 0, 0, 0, 0);
    std::vector<uint8_t> bytes;
    QVERIFY(zsm::encodeStandard8Packet(command, zsm::WireVersion::Mavlink2, bytes));
    zsm::Standard8Decoder decoder;
    mavlink_message_t decoded{};
    bool parsed = false;
    for (uint8_t byte : bytes) parsed |= decoder.feed(byte, decoded);
    QVERIFY(parsed);
    QCOMPARE(decoded.sysid, uint16_t(32767));
    mavlink_command_long_t result{};
    mavlink_msg_command_long_decode(&decoded, &result);
    QCOMPARE(result.target_system, uint16_t(42));
    QCOMPARE(result.command, uint16_t(MAV_CMD_COMPONENT_ARM_DISARM));
}

void SwarmSimMavlinkProtocolTest::batteryAndShowStatusRoundTrip()
{
    uint16_t voltages[10];
    std::fill(std::begin(voltages), std::end(voltages), UINT16_MAX);
    voltages[0] = voltages[1] = voltages[2] = 4150;
    uint16_t voltagesExt[4]{};

    for (uint16_t id : {uint16_t(1), uint16_t(250)}) {
        mavlink_message_t battery{};
        mavlink_msg_battery_status_pack_chan(id, MAV_COMP_ID_AUTOPILOT1, MAVLINK_COMM_0,
            &battery, 1, MAV_BATTERY_FUNCTION_ALL, MAV_BATTERY_TYPE_LIPO,
            2100, voltages, 600, 100, -1, 95, -1,
            MAV_BATTERY_CHARGE_STATE_OK, voltagesExt, MAV_BATTERY_MODE_UNKNOWN, 0);

        std::vector<uint8_t> bytes;
        QVERIFY(zsm::encodeStandard8Packet(battery, zsm::WireVersion::Mavlink2, bytes));
        zsm::Standard8Decoder decoder;
        mavlink_message_t decoded{};
        bool parsed = false;
        for (uint8_t byte : bytes) parsed |= decoder.feed(byte, decoded);
        QVERIFY(parsed);
        QCOMPARE(decoded.sysid, id);
        mavlink_battery_status_t result{};
        mavlink_msg_battery_status_decode(&decoded, &result);
        QCOMPARE(result.battery_remaining, int8_t(95));
        QCOMPARE(result.voltages[0], uint16_t(4150));
        QCOMPARE(result.charge_state, uint8_t(MAV_BATTERY_CHARGE_STATE_OK));
    }

    uint8_t statusData[16]{};
    statusData[6] = 0x40;
    mavlink_message_t status{};
    mavlink_msg_data16_pack_chan(20000, MAV_COMP_ID_AUTOPILOT1, MAVLINK_COMM_0,
        &status, 0x5B, 14, statusData);
    uint8_t wire[MAVLINK_MAX_PACKET_LEN]{};
    const auto wireLength = mavlink_msg_to_send_buffer(wire, &status);
    mavlink_message_t parsedStatus{};
    mavlink_status_t parserStatus{};
    bool parsed = false;
    for (uint16_t i = 0; i < wireLength; ++i) {
        parsed |= mavlink_parse_char(MAVLINK_COMM_0, wire[i], &parsedStatus, &parserStatus) == MAVLINK_FRAMING_OK;
    }
    QVERIFY(parsed);
    QCOMPARE(parsedStatus.sysid, uint16_t(20000));
    mavlink_data16_t result{};
    mavlink_msg_data16_decode(&parsedStatus, &result);
    QCOMPARE(result.type, uint8_t(0x5B));
    QCOMPARE(result.len, uint8_t(14));
    QCOMPARE(result.data[6], uint8_t(0x40));
    QCOMPARE(result.data[15], uint8_t(0));
}

void SwarmSimMavlinkProtocolTest::overlongPayloadAndUnknownMessageRejected()
{
    zds::back::mavlink8::Packet packet{};
    packet.systemId = 1;
    packet.messageId = MAVLINK_MSG_ID_COMMAND_LONG;
    packet.payloadLength = 34; // COMMAND_LONG standard layout is 33 bytes.
    mavlink_message_t decoded{};
    QVERIFY(!zds::back::mavlink8::decodeToPrivate(packet, decoded));
    packet.messageId = 0xFFFF;
    packet.payloadLength = 0;
    QVERIFY(!zds::back::mavlink8::decodeToPrivate(packet, decoded));
}

QTEST_GUILESS_MAIN(SwarmSimMavlinkProtocolTest)
#include "SwarmSimMavlinkProtocol_test.moc"
