/** @file
 *	@brief MAVLink comm protocol generated from storm32.xml
 *	@see http://mavlink.org
 */

#pragma once

#include <array>
#include <cstdint>
#include <sstream>

#ifndef MAVLINK_STX
#define MAVLINK_STX 253
#endif

#include "../message.hpp"

namespace mavlink {
namespace storm32 {

/**
 * Array of msg_entry needed for @p mavlink_parse_char() (through @p mavlink_get_msg_entry())
 */
constexpr std::array<mavlink_msg_entry_t, 313> MESSAGE_ENTRIES {{ {0, 50, 9, 9, 0, 0, 0}, {1, 124, 31, 43, 0, 0, 0}, {2, 137, 12, 12, 0, 0, 0}, {4, 235, 15, 15, 3, 12, 14}, {5, 162, 29, 29, 1, 0, 0}, {6, 47, 4, 4, 0, 0, 0}, {7, 119, 32, 32, 0, 0, 0}, {8, 117, 36, 36, 0, 0, 0}, {11, 139, 7, 7, 1, 4, 0}, {20, 196, 21, 21, 3, 0, 4}, {21, 60, 3, 3, 3, 0, 2}, {22, 220, 25, 25, 0, 0, 0}, {23, 32, 24, 24, 3, 4, 6}, {24, 24, 30, 52, 0, 0, 0}, {25, 23, 101, 101, 0, 0, 0}, {26, 170, 22, 24, 0, 0, 0}, {27, 144, 26, 29, 0, 0, 0}, {28, 67, 16, 16, 0, 0, 0}, {29, 115, 14, 16, 0, 0, 0}, {30, 39, 28, 28, 0, 0, 0}, {31, 246, 32, 48, 0, 0, 0}, {32, 185, 28, 28, 0, 0, 0}, {33, 104, 28, 28, 0, 0, 0}, {34, 237, 22, 22, 0, 0, 0}, {35, 244, 22, 22, 0, 0, 0}, {36, 222, 21, 37, 0, 0, 0}, {37, 83, 7, 8, 3, 0, 6}, {38, 249, 7, 8, 3, 0, 6}, {39, 100, 38, 39, 3, 28, 34}, {40, 127, 5, 6, 3, 0, 4}, {41, 55, 5, 5, 3, 0, 4}, {42, 28, 2, 18, 0, 0, 0}, {43, 250, 3, 4, 3, 0, 2}, {44, 18, 5, 10, 3, 0, 4}, {45, 226, 3, 4, 3, 0, 2}, {46, 11, 2, 2, 0, 0, 0}, {47, 108, 4, 9, 3, 0, 2}, {48, 41, 14, 22, 1, 12, 0}, {49, 39, 12, 20, 0, 0, 0}, {50, 219, 38, 38, 3, 16, 20}, {51, 38, 5, 6, 3, 0, 4}, {54, 249, 28, 28, 3, 24, 26}, {55, 3, 25, 25, 0, 0, 0}, {61, 167, 72, 72, 0, 0, 0}, {62, 183, 26, 26, 0, 0, 0}, {63, 119, 181, 181, 0, 0, 0}, {64, 191, 225, 225, 0, 0, 0}, {65, 118, 42, 42, 0, 0, 0}, {66, 122, 7, 7, 3, 0, 4}, {67, 21, 4, 4, 0, 0, 0}, {69, 177, 12, 31, 1, 0, 0}, {70, 199, 19, 39, 3, 0, 18}, {73, 218, 38, 39, 3, 28, 34}, {74, 20, 20, 20, 0, 0, 0}, {75, 19, 36, 36, 3, 28, 32}, {76, 206, 34, 34, 3, 28, 32}, {77, 143, 3, 11, 3, 8, 10}, {80, 21, 5, 5, 3, 0, 4}, {81, 106, 22, 22, 0, 0, 0}, {82, 143, 40, 52, 3, 36, 38}, {83, 22, 37, 37, 0, 0, 0}, {84, 145, 54, 54, 3, 48, 52}, {85, 140, 51, 51, 0, 0, 0}, {86, 175, 54, 54, 3, 48, 52}, {87, 150, 51, 51, 0, 0, 0}, {89, 231, 28, 28, 0, 0, 0}, {90, 183, 56, 56, 0, 0, 0}, {91, 63, 42, 42, 0, 0, 0}, {92, 54, 33, 33, 0, 0, 0}, {93, 47, 81, 81, 0, 0, 0}, {100, 175, 26, 34, 0, 0, 0}, {101, 102, 32, 117, 0, 0, 0}, {102, 158, 32, 117, 0, 0, 0}, {103, 208, 20, 57, 0, 0, 0}, {104, 56, 32, 116, 0, 0, 0}, {105, 93, 62, 63, 0, 0, 0}, {106, 138, 44, 44, 0, 0, 0}, {107, 108, 64, 65, 0, 0, 0}, {108, 32, 84, 92, 0, 0, 0}, {109, 185, 9, 9, 0, 0, 0}, {110, 134, 255, 255, 3, 0, 3}, {111, 34, 16, 19, 3, 16, 18}, {112, 174, 12, 12, 0, 0, 0}, {113, 124, 36, 39, 0, 0, 0}, {114, 237, 44, 44, 0, 0, 0}, {115, 4, 64, 64, 0, 0, 0}, {116, 76, 22, 24, 0, 0, 0}, {117, 37, 7, 7, 3, 0, 6}, {118, 56, 14, 14, 0, 0, 0}, {119, 92, 13, 13, 3, 8, 12}, {120, 134, 97, 97, 0, 0, 0}, {121, 51, 3, 3, 3, 0, 2}, {122, 1, 3, 3, 3, 0, 2}, {123, 232, 114, 114, 3, 0, 2}, {124, 87, 35, 57, 0, 0, 0}, {125, 203, 6, 6, 0, 0, 0}, {126, 220, 79, 82, 3, 79, 81}, {127, 25, 35, 35, 0, 0, 0}, {128, 226, 35, 35, 0, 0, 0}, {129, 46, 22, 24, 0, 0, 0}, {130, 29, 13, 13, 0, 0, 0}, {131, 223, 255, 255, 0, 0, 0}, {132, 85, 14, 39, 0, 0, 0}, {133, 6, 18, 18, 0, 0, 0}, {134, 229, 43, 43, 0, 0, 0}, {135, 203, 8, 8, 0, 0, 0}, {136, 1, 22, 22, 0, 0, 0}, {137, 195, 14, 16, 0, 0, 0}, {138, 109, 36, 120, 0, 0, 0}, {139, 59, 44, 44, 3, 40, 43}, {140, 181, 41, 41, 0, 0, 0}, {141, 47, 32, 32, 0, 0, 0}, {142, 72, 243, 243, 0, 0, 0}, {143, 131, 14, 16, 0, 0, 0}, {144, 127, 93, 93, 0, 0, 0}, {146, 103, 100, 100, 0, 0, 0}, {147, 154, 36, 54, 0, 0, 0}, {148, 178, 60, 78, 0, 0, 0}, {149, 200, 30, 60, 0, 0, 0}, {150, 134, 42, 42, 0, 0, 0}, {151, 120, 9, 9, 3, 0, 8}, {152, 208, 4, 8, 0, 0, 0}, {153, 188, 12, 12, 0, 0, 0}, {154, 122, 16, 16, 3, 4, 8}, {155, 206, 14, 14, 3, 4, 6}, {156, 167, 7, 7, 3, 0, 2}, {157, 137, 16, 16, 3, 12, 14}, {158, 203, 15, 16, 3, 12, 14}, {160, 192, 13, 13, 3, 8, 10}, {161, 232, 4, 4, 3, 0, 2}, {162, 189, 8, 9, 0, 0, 0}, {163, 127, 28, 28, 0, 0, 0}, {164, 154, 44, 44, 0, 0, 0}, {165, 21, 3, 3, 0, 0, 0}, {166, 21, 9, 9, 0, 0, 0}, {167, 144, 22, 22, 0, 0, 0}, {168, 1, 12, 12, 0, 0, 0}, {169, 234, 18, 18, 0, 0, 0}, {170, 73, 34, 34, 0, 0, 0}, {171, 181, 66, 66, 0, 0, 0}, {172, 22, 98, 98, 0, 0, 0}, {173, 83, 8, 8, 0, 0, 0}, {174, 167, 48, 48, 0, 0, 0}, {175, 248, 20, 20, 3, 8, 16}, {176, 47, 4, 4, 3, 0, 2}, {177, 240, 20, 20, 0, 0, 0}, {178, 47, 24, 24, 0, 0, 0}, {179, 24, 30, 30, 1, 24, 0}, {180, 28, 46, 48, 1, 40, 0}, {181, 174, 4, 4, 0, 0, 0}, {182, 229, 40, 40, 0, 0, 0}, {183, 202, 3, 3, 3, 0, 2}, {184, 60, 207, 207, 3, 4, 6}, {185, 140, 8, 8, 3, 4, 6}, {186, 238, 30, 30, 3, 0, 2}, {191, 92, 27, 27, 0, 0, 0}, {192, 36, 44, 54, 0, 0, 0}, {193, 71, 22, 26, 0, 0, 0}, {194, 98, 25, 33, 0, 0, 0}, {195, 120, 37, 37, 0, 0, 0}, {200, 13, 43, 43, 3, 40, 42}, {201, 8, 15, 15, 3, 12, 14}, {214, 117, 9, 9, 3, 0, 8}, {215, 101, 3, 3, 0, 0, 0}, {216, 124, 4, 4, 3, 0, 2}, {217, 202, 6, 6, 0, 0, 0}, {218, 106, 8, 8, 3, 0, 2}, {219, 162, 2, 2, 0, 0, 0}, {225, 208, 65, 73, 0, 0, 0}, {226, 207, 8, 8, 0, 0, 0}, {230, 163, 42, 42, 0, 0, 0}, {231, 105, 40, 40, 0, 0, 0}, {232, 151, 63, 65, 0, 0, 0}, {233, 35, 182, 182, 0, 0, 0}, {234, 150, 40, 40, 0, 0, 0}, {235, 179, 42, 42, 0, 0, 0}, {241, 90, 32, 32, 0, 0, 0}, {242, 104, 52, 60, 0, 0, 0}, {243, 5, 54, 62, 1, 52, 0}, {244, 95, 6, 6, 0, 0, 0}, {245, 130, 2, 2, 0, 0, 0}, {246, 184, 38, 38, 0, 0, 0}, {247, 81, 19, 19, 0, 0, 0}, {248, 254, 255, 255, 3, 0, 5}, {249, 204, 36, 36, 0, 0, 0}, {250, 49, 30, 30, 0, 0, 0}, {251, 170, 18, 18, 0, 0, 0}, {252, 44, 18, 18, 0, 0, 0}, {253, 83, 51, 54, 0, 0, 0}, {254, 46, 9, 9, 0, 0, 0}, {256, 114, 43, 43, 3, 8, 10}, {257, 131, 9, 9, 0, 0, 0}, {258, 53, 33, 233, 3, 0, 2}, {259, 92, 235, 237, 0, 0, 0}, {260, 146, 5, 14, 0, 0, 0}, {261, 179, 27, 61, 0, 0, 0}, {262, 12, 18, 23, 0, 0, 0}, {263, 133, 255, 255, 0, 0, 0}, {264, 49, 28, 32, 0, 0, 0}, {265, 26, 16, 20, 0, 0, 0}, {266, 208, 256, 256, 3, 0, 4}, {267, 233, 256, 256, 3, 0, 4}, {268, 176, 5, 5, 3, 0, 4}, {269, 109, 213, 215, 0, 0, 0}, {270, 59, 19, 20, 0, 0, 0}, {271, 22, 52, 53, 0, 0, 0}, {275, 126, 31, 32, 0, 0, 0}, {276, 18, 49, 50, 0, 0, 0}, {277, 62, 30, 30, 0, 0, 0}, {280, 70, 33, 33, 0, 0, 0}, {281, 51, 14, 14, 0, 0, 0}, {282, 36, 36, 36, 3, 32, 34}, {283, 74, 144, 145, 0, 0, 0}, {284, 101, 33, 33, 3, 28, 32}, {285, 28, 41, 50, 3, 36, 40}, {286, 212, 54, 58, 3, 48, 52}, {287, 73, 24, 24, 3, 20, 22}, {288, 89, 24, 24, 3, 20, 22}, {290, 251, 46, 46, 0, 0, 0}, {291, 10, 57, 57, 0, 0, 0}, {299, 19, 96, 98, 0, 0, 0}, {300, 217, 22, 22, 0, 0, 0}, {301, 243, 58, 58, 0, 0, 0}, {310, 28, 17, 17, 0, 0, 0}, {311, 95, 116, 116, 0, 0, 0}, {320, 209, 21, 21, 3, 0, 4}, {321, 211, 3, 3, 3, 0, 2}, {322, 243, 149, 149, 0, 0, 0}, {323, 106, 148, 148, 3, 0, 2}, {324, 132, 146, 146, 0, 0, 0}, {330, 23, 158, 167, 0, 0, 0}, {331, 91, 230, 233, 0, 0, 0}, {332, 236, 239, 239, 0, 0, 0}, {333, 231, 109, 109, 0, 0, 0}, {334, 72, 10, 10, 0, 0, 0}, {335, 225, 24, 24, 0, 0, 0}, {336, 245, 84, 84, 0, 0, 0}, {339, 199, 5, 5, 0, 0, 0}, {340, 99, 70, 70, 0, 0, 0}, {350, 232, 20, 252, 0, 0, 0}, {360, 11, 25, 25, 0, 0, 0}, {370, 26, 140, 140, 0, 0, 0}, {373, 117, 42, 42, 0, 0, 0}, {375, 251, 140, 140, 0, 0, 0}, {380, 232, 20, 20, 0, 0, 0}, {385, 24, 134, 134, 3, 0, 4}, {386, 149, 17, 17, 3, 4, 6}, {387, 115, 73, 73, 3, 4, 6}, {388, 145, 38, 38, 3, 0, 34}, {390, 156, 238, 238, 0, 0, 0}, {395, 0, 212, 212, 0, 0, 0}, {396, 50, 160, 160, 0, 0, 0}, {397, 182, 108, 108, 0, 0, 0}, {400, 182, 255, 255, 3, 4, 6}, {401, 108, 7, 7, 3, 4, 6}, {410, 86, 54, 54, 0, 0, 0}, {411, 106, 3, 3, 0, 0, 0}, {412, 201, 7, 7, 3, 0, 6}, {413, 18, 8, 8, 3, 0, 6}, {440, 66, 35, 35, 0, 0, 0}, {9000, 113, 137, 137, 0, 0, 0}, {9005, 117, 34, 34, 0, 0, 0}, {10001, 209, 20, 20, 0, 0, 0}, {10002, 186, 41, 41, 0, 0, 0}, {10003, 4, 1, 1, 0, 0, 0}, {11000, 193, 52, 53, 3, 4, 6}, {11001, 15, 135, 136, 0, 0, 0}, {11002, 16, 180, 181, 3, 4, 6}, {11003, 64, 5, 5, 0, 0, 0}, {11010, 46, 49, 49, 0, 0, 0}, {11011, 106, 44, 44, 0, 0, 0}, {11020, 205, 16, 16, 0, 0, 0}, {11030, 144, 44, 44, 0, 0, 0}, {11031, 133, 44, 44, 0, 0, 0}, {11032, 85, 44, 44, 0, 0, 0}, {11033, 63, 38, 38, 3, 16, 18}, {11034, 79, 5, 5, 0, 0, 0}, {11035, 82, 9, 9, 3, 4, 6}, {11036, 177, 34, 34, 0, 0, 0}, {11037, 130, 28, 28, 0, 0, 0}, {11038, 47, 38, 38, 0, 0, 0}, {11039, 142, 9, 9, 0, 0, 0}, {12900, 142, 45, 45, 3, 0, 2}, {12901, 67, 60, 60, 3, 24, 32}, {12902, 167, 54, 54, 3, 4, 6}, {12903, 184, 47, 47, 3, 0, 2}, {12904, 6, 55, 55, 3, 24, 30}, {12905, 164, 44, 44, 3, 0, 2}, {12915, 127, 250, 250, 3, 0, 2}, {12918, 139, 51, 51, 0, 0, 0}, {12919, 248, 19, 19, 3, 16, 18}, {12920, 20, 5, 5, 0, 0, 0}, {42000, 227, 1, 1, 0, 0, 0}, {42001, 239, 46, 46, 0, 0, 0}, {50001, 246, 32, 32, 0, 0, 0}, {50002, 181, 246, 246, 0, 0, 0}, {50003, 62, 19, 19, 0, 0, 0}, {50004, 197, 11, 11, 3, 8, 10}, {50005, 152, 7, 7, 3, 4, 6}, {52000, 13, 100, 100, 0, 0, 0}, {52001, 239, 1, 1, 0, 0, 0}, {52002, 24, 1, 1, 0, 0, 0}, {52003, 166, 26, 26, 0, 0, 0}, {52004, 39, 1, 1, 0, 0, 0}, {52005, 145, 1, 1, 0, 0, 0}, {60010, 208, 33, 33, 0, 0, 0}, {60011, 183, 7, 7, 0, 0, 0}, {60012, 99, 37, 37, 3, 28, 34}, {60013, 92, 25, 25, 3, 16, 22}, {60014, 94, 9, 9, 3, 4, 6}, {60020, 202, 4, 4, 0, 0, 0}, {60040, 156, 245, 245, 0, 0, 0}, {60041, 191, 255, 255, 0, 0, 0} }};

//! MAVLINK VERSION
constexpr auto MAVLINK_VERSION = 1;


// ENUM DEFINITIONS


/** @brief  */
enum class MAV_STORM32_TUNNEL_PAYLOAD_TYPE
{
    STORM32_CH1_IN=200, /* Registered for STorM32 gimbal controller. For communication with gimbal or camera. | */
    STORM32_CH1_OUT=201, /* Registered for STorM32 gimbal controller. For communication with gimbal or camera. | */
    STORM32_CH2_IN=202, /* Registered for STorM32 gimbal controller. For communication with gimbal. | */
    STORM32_CH2_OUT=203, /* Registered for STorM32 gimbal controller. For communication with gimbal. | */
    STORM32_CH3_IN=204, /* Registered for STorM32 gimbal controller. For communication with camera. | */
    STORM32_CH3_OUT=205, /* Registered for STorM32 gimbal controller. For communication with camera. | */
};

//! MAV_STORM32_TUNNEL_PAYLOAD_TYPE ENUM_END
constexpr auto MAV_STORM32_TUNNEL_PAYLOAD_TYPE_ENUM_END = 206;

/** @brief STorM32 gimbal prearm check flags. */
enum class MAV_STORM32_GIMBAL_PREARM_FLAGS
{
    IS_NORMAL=1, /* STorM32 gimbal is in normal state. | */
    IMUS_WORKING=2, /* The IMUs are healthy and working normally. | */
    MOTORS_WORKING=4, /* The motors are active and working normally. | */
    ENCODERS_WORKING=8, /* The encoders are healthy and working normally. | */
    VOLTAGE_OK=16, /* A battery voltage is applied and is in range. | */
    VIRTUALCHANNELS_RECEIVING=32, /* Virtual input channels are receiving data. | */
    MAVLINK_RECEIVING=64, /* Mavlink messages are being received. | */
    STORM32LINK_QFIX=128, /* The STorM32Link data indicates QFix. | */
    STORM32LINK_WORKING=256, /* The STorM32Link is working. | */
    CAMERA_CONNECTED=512, /* The camera has been found and is connected. | */
    AUX0_LOW=1024, /* The signal on the AUX0 input pin is low. | */
    AUX1_LOW=2048, /* The signal on the AUX1 input pin is low. | */
    NTLOGGER_WORKING=4096, /* The NTLogger is working normally. | */
};

//! MAV_STORM32_GIMBAL_PREARM_FLAGS ENUM_END
constexpr auto MAV_STORM32_GIMBAL_PREARM_FLAGS_ENUM_END = 4097;

/** @brief STorM32 camera prearm check flags. */
enum class MAV_STORM32_CAMERA_PREARM_FLAGS
{
    CONNECTED=1, /* The camera has been found and is connected. | */
};

//! MAV_STORM32_CAMERA_PREARM_FLAGS ENUM_END
constexpr auto MAV_STORM32_CAMERA_PREARM_FLAGS_ENUM_END = 2;

/** @brief Gimbal manager capability flags. */
enum class MAV_STORM32_GIMBAL_MANAGER_CAP_FLAGS : uint32_t
{
    HAS_PROFILES=1, /* The gimbal manager supports several profiles. | */
};

//! MAV_STORM32_GIMBAL_MANAGER_CAP_FLAGS ENUM_END
constexpr auto MAV_STORM32_GIMBAL_MANAGER_CAP_FLAGS_ENUM_END = 2;

/** @brief Flags for gimbal manager operation. Used for setting and reporting, unless specified otherwise. If a setting has been accepted by the gimbal manager is reported in the STORM32_GIMBAL_MANAGER_STATUS message. */
enum class MAV_STORM32_GIMBAL_MANAGER_FLAGS : uint16_t
{
    NONE=0, /* 0 = ignore. | */
    RC_ACTIVE=1, /* Request to set RC input to active, or report RC input is active. Implies RC mixed. RC exclusive is achieved by setting all clients to inactive. | */
    CLIENT_ONBOARD_ACTIVE=2, /* Request to set onboard/companion computer client to active, or report this client is active. | */
    CLIENT_AUTOPILOT_ACTIVE=4, /* Request to set autopliot client to active, or report this client is active. | */
    CLIENT_GCS_ACTIVE=8, /* Request to set GCS client to active, or report this client is active. | */
    CLIENT_CAMERA_ACTIVE=16, /* Request to set camera client to active, or report this client is active. | */
    CLIENT_GCS2_ACTIVE=32, /* Request to set GCS2 client to active, or report this client is active. | */
    CLIENT_CAMERA2_ACTIVE=64, /* Request to set camera2 client to active, or report this client is active. | */
    CLIENT_CUSTOM_ACTIVE=128, /* Request to set custom client to active, or report this client is active. | */
    CLIENT_CUSTOM2_ACTIVE=256, /* Request to set custom2 client to active, or report this client is active. | */
    SET_SUPERVISON=512, /* Request supervision. This flag is only for setting, it is not reported. | */
    SET_RELEASE=1024, /* Release supervision. This flag is only for setting, it is not reported. | */
};

//! MAV_STORM32_GIMBAL_MANAGER_FLAGS ENUM_END
constexpr auto MAV_STORM32_GIMBAL_MANAGER_FLAGS_ENUM_END = 1025;

/** @brief Gimbal manager client ID. In a prioritizing profile, the priorities are determined by the implementation; they could e.g. be custom1 > onboard > GCS > autopilot/camera > GCS2 > custom2. */
enum class MAV_STORM32_GIMBAL_MANAGER_CLIENT : uint8_t
{
    NONE=0, /* For convenience. | */
    ONBOARD=1, /* This is the onboard/companion computer client. | */
    AUTOPILOT=2, /* This is the autopilot client. | */
    GCS=3, /* This is the GCS client. | */
    CAMERA=4, /* This is the camera client. | */
    GCS2=5, /* This is the GCS2 client. | */
    CAMERA2=6, /* This is the camera2 client. | */
    CUSTOM=7, /* This is the custom client. | */
    CUSTOM2=8, /* This is the custom2 client. | */
};

//! MAV_STORM32_GIMBAL_MANAGER_CLIENT ENUM_END
constexpr auto MAV_STORM32_GIMBAL_MANAGER_CLIENT_ENUM_END = 9;

/** @brief Gimbal manager profiles. Only standard profiles are defined. Any implementation can define its own profile(s) in addition, and should use enum values > 16. */
enum class MAV_STORM32_GIMBAL_MANAGER_PROFILE : uint8_t
{
    DEFAULT=0, /* Default profile. Implementation specific. | */
    CUSTOM=1, /* Not supported/deprecated. | */
    COOPERATIVE=2, /* Profile with cooperative behavior. | */
    EXCLUSIVE=3, /* Profile with exclusive behavior. | */
    PRIORITY_COOPERATIVE=4, /* Profile with priority and cooperative behavior for equal priority. | */
    PRIORITY_EXCLUSIVE=5, /* Profile with priority and exclusive behavior for equal priority. | */
};

//! MAV_STORM32_GIMBAL_MANAGER_PROFILE ENUM_END
constexpr auto MAV_STORM32_GIMBAL_MANAGER_PROFILE_ENUM_END = 6;

/** @brief Enumeration of possible shot modes. */
enum class MAV_QSHOT_MODE : uint16_t
{
    UNDEFINED=0, /* Undefined shot mode. Can be used to determine if qshots should be used or not. | */
    DEFAULT=1, /* Start normal gimbal operation. Is usually used to return back from a shot. | */
    GIMBAL_RETRACT=2, /* Load and keep safe gimbal position and stop stabilization. | */
    GIMBAL_NEUTRAL=3, /* Load neutral gimbal position and keep it while stabilizing. | */
    GIMBAL_MISSION=4, /* Start mission with gimbal control. | */
    GIMBAL_RC_CONTROL=5, /* Start RC gimbal control. | */
    POI_TARGETING=6, /* Start gimbal tracking the point specified by Lat, Lon, Alt. | */
    SYSID_TARGETING=7, /* Start gimbal tracking the system with specified system ID. | */
    CABLECAM_2POINT=8, /* Start 2-point cable cam quick shot. | */
    HOME_TARGETING=9, /* Start gimbal tracking the home location. | */
};

//! MAV_QSHOT_MODE ENUM_END
constexpr auto MAV_QSHOT_MODE_ENUM_END = 10;


} // namespace storm32
} // namespace mavlink

// MESSAGE DEFINITIONS
#include "./mavlink_msg_storm32_gimbal_manager_information.hpp"
#include "./mavlink_msg_storm32_gimbal_manager_status.hpp"
#include "./mavlink_msg_storm32_gimbal_manager_control.hpp"
#include "./mavlink_msg_storm32_gimbal_manager_control_pitchyaw.hpp"
#include "./mavlink_msg_storm32_gimbal_manager_correct_roll.hpp"
#include "./mavlink_msg_qshot_status.hpp"
#include "./mavlink_msg_frsky_passthrough_array.hpp"
#include "./mavlink_msg_param_value_array.hpp"

// base include
#include "../ardupilotmega/ardupilotmega.hpp"
