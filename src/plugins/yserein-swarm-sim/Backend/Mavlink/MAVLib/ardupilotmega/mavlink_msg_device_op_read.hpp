// MESSAGE DEVICE_OP_READ support class

#pragma once

namespace mavlink {
namespace ardupilotmega {
namespace msg {

/**
 * @brief DEVICE_OP_READ message
 *
 * Read registers for a device.
 */
struct DEVICE_OP_READ : mavlink::Message {
    static constexpr msgid_t MSG_ID = 11000;
    static constexpr size_t LENGTH = 53;
    static constexpr size_t MIN_LENGTH = 52;
    static constexpr uint8_t CRC_EXTRA = 193;
    static constexpr auto NAME = "DEVICE_OP_READ";


    uint16_t target_system; /*<  System ID. */
    uint8_t target_component; /*<  Component ID. */
    uint32_t request_id; /*<  Request ID - copied to reply. */
    uint8_t bustype; /*<  The bus type. */
    uint8_t bus; /*<  Bus number. */
    uint8_t address; /*<  Bus address. */
    std::array<char, 40> busname; /*<  Name of device on bus (for SPI). */
    uint8_t regstart; /*<  First register to read. */
    uint8_t count; /*<  Count of registers to read. */
    uint8_t bank; /*<  Bank number. */


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
        ss << "  request_id: " << request_id << std::endl;
        ss << "  bustype: " << +bustype << std::endl;
        ss << "  bus: " << +bus << std::endl;
        ss << "  address: " << +address << std::endl;
        ss << "  busname: \"" << to_string(busname) << "\"" << std::endl;
        ss << "  regstart: " << +regstart << std::endl;
        ss << "  count: " << +count << std::endl;
        ss << "  bank: " << +bank << std::endl;

        return ss.str();
    }

    inline void serialize(mavlink::MsgMap &map) const override
    {
        map.reset(MSG_ID, LENGTH);

        map << request_id;                    // offset: 0
        map << target_system;                 // offset: 4
        map << target_component;              // offset: 6
        map << bustype;                       // offset: 7
        map << bus;                           // offset: 8
        map << address;                       // offset: 9
        map << busname;                       // offset: 10
        map << regstart;                      // offset: 50
        map << count;                         // offset: 51
        map << bank;                          // offset: 52
    }

    inline void deserialize(mavlink::MsgMap &map) override
    {
        map >> request_id;                    // offset: 0
        map >> target_system;                 // offset: 4
        map >> target_component;              // offset: 6
        map >> bustype;                       // offset: 7
        map >> bus;                           // offset: 8
        map >> address;                       // offset: 9
        map >> busname;                       // offset: 10
        map >> regstart;                      // offset: 50
        map >> count;                         // offset: 51
        map >> bank;                          // offset: 52
    }
};

} // namespace msg
} // namespace ardupilotmega
} // namespace mavlink
