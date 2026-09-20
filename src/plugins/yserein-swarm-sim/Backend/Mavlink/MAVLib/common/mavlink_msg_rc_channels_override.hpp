// MESSAGE RC_CHANNELS_OVERRIDE support class

#pragma once

namespace mavlink {
namespace common {
namespace msg {

/**
 * @brief RC_CHANNELS_OVERRIDE message
 *
 * The RAW values of the RC channels sent to the MAV to override info received from the RC radio. The standard PPM modulation is as follows: 1000 microseconds: 0%, 2000 microseconds: 100%. Individual receivers/transmitters might violate this specification.  Note carefully the semantic differences between the first 8 channels and the subsequent channels
 */
struct RC_CHANNELS_OVERRIDE : mavlink::Message {
    static constexpr msgid_t MSG_ID = 70;
    static constexpr size_t LENGTH = 39;
    static constexpr size_t MIN_LENGTH = 19;
    static constexpr uint8_t CRC_EXTRA = 199;
    static constexpr auto NAME = "RC_CHANNELS_OVERRIDE";


    uint16_t target_system; /*<  System ID */
    uint8_t target_component; /*<  Component ID */
    uint16_t chan1_raw; /*< [us] RC channel 1 value. A value of UINT16_MAX means to ignore this field. A value of 0 means to release this channel back to the RC radio. */
    uint16_t chan2_raw; /*< [us] RC channel 2 value. A value of UINT16_MAX means to ignore this field. A value of 0 means to release this channel back to the RC radio. */
    uint16_t chan3_raw; /*< [us] RC channel 3 value. A value of UINT16_MAX means to ignore this field. A value of 0 means to release this channel back to the RC radio. */
    uint16_t chan4_raw; /*< [us] RC channel 4 value. A value of UINT16_MAX means to ignore this field. A value of 0 means to release this channel back to the RC radio. */
    uint16_t chan5_raw; /*< [us] RC channel 5 value. A value of UINT16_MAX means to ignore this field. A value of 0 means to release this channel back to the RC radio. */
    uint16_t chan6_raw; /*< [us] RC channel 6 value. A value of UINT16_MAX means to ignore this field. A value of 0 means to release this channel back to the RC radio. */
    uint16_t chan7_raw; /*< [us] RC channel 7 value. A value of UINT16_MAX means to ignore this field. A value of 0 means to release this channel back to the RC radio. */
    uint16_t chan8_raw; /*< [us] RC channel 8 value. A value of UINT16_MAX means to ignore this field. A value of 0 means to release this channel back to the RC radio. */
    uint16_t chan9_raw; /*< [us] RC channel 9 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */
    uint16_t chan10_raw; /*< [us] RC channel 10 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */
    uint16_t chan11_raw; /*< [us] RC channel 11 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */
    uint16_t chan12_raw; /*< [us] RC channel 12 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */
    uint16_t chan13_raw; /*< [us] RC channel 13 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */
    uint16_t chan14_raw; /*< [us] RC channel 14 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */
    uint16_t chan15_raw; /*< [us] RC channel 15 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */
    uint16_t chan16_raw; /*< [us] RC channel 16 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */
    uint16_t chan17_raw; /*< [us] RC channel 17 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */
    uint16_t chan18_raw; /*< [us] RC channel 18 value. A value of 0 or UINT16_MAX means to ignore this field. A value of UINT16_MAX-1 means to release this channel back to the RC radio. */


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
        ss << "  chan1_raw: " << chan1_raw << std::endl;
        ss << "  chan2_raw: " << chan2_raw << std::endl;
        ss << "  chan3_raw: " << chan3_raw << std::endl;
        ss << "  chan4_raw: " << chan4_raw << std::endl;
        ss << "  chan5_raw: " << chan5_raw << std::endl;
        ss << "  chan6_raw: " << chan6_raw << std::endl;
        ss << "  chan7_raw: " << chan7_raw << std::endl;
        ss << "  chan8_raw: " << chan8_raw << std::endl;
        ss << "  chan9_raw: " << chan9_raw << std::endl;
        ss << "  chan10_raw: " << chan10_raw << std::endl;
        ss << "  chan11_raw: " << chan11_raw << std::endl;
        ss << "  chan12_raw: " << chan12_raw << std::endl;
        ss << "  chan13_raw: " << chan13_raw << std::endl;
        ss << "  chan14_raw: " << chan14_raw << std::endl;
        ss << "  chan15_raw: " << chan15_raw << std::endl;
        ss << "  chan16_raw: " << chan16_raw << std::endl;
        ss << "  chan17_raw: " << chan17_raw << std::endl;
        ss << "  chan18_raw: " << chan18_raw << std::endl;

        return ss.str();
    }

    inline void serialize(mavlink::MsgMap &map) const override
    {
        map.reset(MSG_ID, LENGTH);

        map << target_system;                 // offset: 0
        map << chan1_raw;                     // offset: 2
        map << chan2_raw;                     // offset: 4
        map << chan3_raw;                     // offset: 6
        map << chan4_raw;                     // offset: 8
        map << chan5_raw;                     // offset: 10
        map << chan6_raw;                     // offset: 12
        map << chan7_raw;                     // offset: 14
        map << chan8_raw;                     // offset: 16
        map << target_component;              // offset: 18
        map << chan9_raw;                     // offset: 19
        map << chan10_raw;                    // offset: 21
        map << chan11_raw;                    // offset: 23
        map << chan12_raw;                    // offset: 25
        map << chan13_raw;                    // offset: 27
        map << chan14_raw;                    // offset: 29
        map << chan15_raw;                    // offset: 31
        map << chan16_raw;                    // offset: 33
        map << chan17_raw;                    // offset: 35
        map << chan18_raw;                    // offset: 37
    }

    inline void deserialize(mavlink::MsgMap &map) override
    {
        map >> target_system;                 // offset: 0
        map >> chan1_raw;                     // offset: 2
        map >> chan2_raw;                     // offset: 4
        map >> chan3_raw;                     // offset: 6
        map >> chan4_raw;                     // offset: 8
        map >> chan5_raw;                     // offset: 10
        map >> chan6_raw;                     // offset: 12
        map >> chan7_raw;                     // offset: 14
        map >> chan8_raw;                     // offset: 16
        map >> target_component;              // offset: 18
        map >> chan9_raw;                     // offset: 19
        map >> chan10_raw;                    // offset: 21
        map >> chan11_raw;                    // offset: 23
        map >> chan12_raw;                    // offset: 25
        map >> chan13_raw;                    // offset: 27
        map >> chan14_raw;                    // offset: 29
        map >> chan15_raw;                    // offset: 31
        map >> chan16_raw;                    // offset: 33
        map >> chan17_raw;                    // offset: 35
        map >> chan18_raw;                    // offset: 37
    }
};

} // namespace msg
} // namespace common
} // namespace mavlink
