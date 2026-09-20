/** @file
 *    @brief MAVLink comm protocol testsuite generated from all.xml
 *    @see https://mavlink.io/en/
 */
#pragma once
#ifndef ALL_TESTSUITE_H
#define ALL_TESTSUITE_H

#include "all.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAVLINK_TEST_ALL
#define MAVLINK_TEST_ALL
static void mavlink_test_ardupilotmega(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_ASLUAV(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_common(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_development(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_icarous(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_minimal(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_python_array_test(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_standard(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_test(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_ualberta(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_uAvionix(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_storm32(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_AVSSUAS(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_cubepilot(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_csAirLink(uint16_t, uint8_t, mavlink_message_t *last_msg);
static void mavlink_test_all(uint16_t, uint8_t, mavlink_message_t *last_msg);

static void mavlink_test_all(uint16_t system_id, uint8_t component_id, mavlink_message_t *last_msg)
{
    mavlink_test_ardupilotmega(system_id, component_id, last_msg);
    mavlink_test_ASLUAV(system_id, component_id, last_msg);
    mavlink_test_common(system_id, component_id, last_msg);
    mavlink_test_development(system_id, component_id, last_msg);
    mavlink_test_icarous(system_id, component_id, last_msg);
    mavlink_test_minimal(system_id, component_id, last_msg);
    mavlink_test_python_array_test(system_id, component_id, last_msg);
    mavlink_test_standard(system_id, component_id, last_msg);
    mavlink_test_test(system_id, component_id, last_msg);
    mavlink_test_ualberta(system_id, component_id, last_msg);
    mavlink_test_uAvionix(system_id, component_id, last_msg);
    mavlink_test_storm32(system_id, component_id, last_msg);
    mavlink_test_AVSSUAS(system_id, component_id, last_msg);
    mavlink_test_cubepilot(system_id, component_id, last_msg);
    mavlink_test_csAirLink(system_id, component_id, last_msg);
    mavlink_test_all(system_id, component_id, last_msg);
}
#endif



#include "../ardupilotmega/testsuite.h"
#include "../ASLUAV/testsuite.h"
#include "../common/testsuite.h"
#include "../development/testsuite.h"
#include "../icarous/testsuite.h"
#include "../minimal/testsuite.h"
#include "../python_array_test/testsuite.h"
#include "../standard/testsuite.h"
#include "../test/testsuite.h"
#include "../ualberta/testsuite.h"
#include "../uAvionix/testsuite.h"
#include "../storm32/testsuite.h"
#include "../AVSSUAS/testsuite.h"
#include "../cubepilot/testsuite.h"
#include "../csAirLink/testsuite.h"



static void mavlink_test_all(uint16_t system_id, uint8_t component_id, mavlink_message_t *last_msg)
{

}

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // ALL_TESTSUITE_H
