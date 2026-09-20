// MESSAGE GIMBAL_TORQUE_CMD_REPORT support class

#pragma once

namespace mavlink {
namespace ardupilotmega {
namespace msg {

/**
 * @brief GIMBAL_TORQUE_CMD_REPORT message
 *
 * 100 Hz gimbal torque command telemetry.
 */
struct GIMBAL_TORQUE_CMD_REPORT : mavlink::Message {
    static constexpr msgid_t MSG_ID = 214;
    static constexpr size_t LENGTH = 9;
    static constexpr size_t MIN_LENGTH = 9;
    static constexpr uint8_t CRC_EXTRA = 117;
    static constexpr auto NAME = "GIMBAL_TORQUE_CMD_REPORT";


    uint16_t target_system; /*<  System ID. */
    uint8_t target_component; /*<  Component ID. */
    int16_t rl_torque_cmd; /*<  Roll Torque Command. */
    int16_t el_torque_cmd; /*<  Elevation Torque Command. */
    int16_t az_torque_cmd; /*<  Azimuth Torque Command. */


    inline std::string get_name(void) const override
    {
            return NAME;
    }

    inline Info get_message_info(void) const override
    {
            return { MSG_ID, LENGTH, MIN_LENGTH, CRC_EXTRA };
    }

    inline std::string to_yaml(void) const override
    {
        std::stringstream ss;

        ss << NAME << ":" << std::endl;
        ss << "  target_system: " << target_system << std::endl;
        ss << "  target_component: " << +target_component << std::endl;
        ss << "  rl_torque_cmd: " << rl_torque_cmd << std::endl;
        ss << "  el_torque_cmd: " << el_torque_cmd << std::endl;
        ss << "  az_torque_cmd: " << az_torque_cmd << std::endl;

        return ss.str();
    }

    inline void serialize(mavlink::MsgMap &map) const override
    {
        map.reset(MSG_ID, LENGTH);

        map << target_system;                 // offset: 0
        map << rl_torque_cmd;                 // offset: 2
        map << el_torque_cmd;                 // offset: 4
        map << az_torque_cmd;                 // offset: 6
        map << target_component;              // offset: 8
    }

    inline void deserialize(mavlink::MsgMap &map) override
    {
        map >> target_system;                 // offset: 0
        map >> rl_torque_cmd;                 // offset: 2
        map >> el_torque_cmd;                 // offset: 4
        map >> az_torque_cmd;                 // offset: 6
        map >> target_component;              // offset: 8
    }
};

} // namespace msg
} // namespace ardupilotmega
} // namespace mavlink
