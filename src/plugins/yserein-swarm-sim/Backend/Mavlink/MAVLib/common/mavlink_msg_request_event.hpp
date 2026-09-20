// MESSAGE REQUEST_EVENT support class

#pragma once

namespace mavlink {
namespace common {
namespace msg {

/**
 * @brief REQUEST_EVENT message
 *
 * Request one or more events to be (re-)sent. If first_sequence==last_sequence, only a single event is requested. Note that first_sequence can be larger than last_sequence (because the sequence number can wrap). Each sequence will trigger an EVENT or EVENT_ERROR response.
 */
struct REQUEST_EVENT : mavlink::Message {
    static constexpr msgid_t MSG_ID = 412;
    static constexpr size_t LENGTH = 7;
    static constexpr size_t MIN_LENGTH = 7;
    static constexpr uint8_t CRC_EXTRA = 201;
    static constexpr auto NAME = "REQUEST_EVENT";


    uint16_t target_system; /*<  System ID */
    uint8_t target_component; /*<  Component ID */
    uint16_t first_sequence; /*<  First sequence number of the requested event. */
    uint16_t last_sequence; /*<  Last sequence number of the requested event. */


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
        ss << "  first_sequence: " << first_sequence << std::endl;
        ss << "  last_sequence: " << last_sequence << std::endl;

        return ss.str();
    }

    inline void serialize(mavlink::MsgMap &map) const override
    {
        map.reset(MSG_ID, LENGTH);

        map << target_system;                 // offset: 0
        map << first_sequence;                // offset: 2
        map << last_sequence;                 // offset: 4
        map << target_component;              // offset: 6
    }

    inline void deserialize(mavlink::MsgMap &map) override
    {
        map >> target_system;                 // offset: 0
        map >> first_sequence;                // offset: 2
        map >> last_sequence;                 // offset: 4
        map >> target_component;              // offset: 6
    }
};

} // namespace msg
} // namespace common
} // namespace mavlink
