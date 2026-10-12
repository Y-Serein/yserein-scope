add_executable(t384_calibration_protocol_tests
    T384CalibrationProtocol_test.cpp
    ../src/plugins/t384/T384CalibrationProtocol.cpp
)
target_include_directories(t384_calibration_protocol_tests PRIVATE
    ../src/plugins/t384
)
target_link_libraries(t384_calibration_protocol_tests PRIVATE Qt6::Core)
add_test(NAME t384_calibration_protocol_tests
         COMMAND t384_calibration_protocol_tests)

add_executable(thermal_workbench_ui_tests
    ThermalWorkbenchUi_test.cpp
)
target_link_libraries(thermal_workbench_ui_tests PRIVATE
    serein_scope_thermal_workbench Qt6::Core Qt6::Widgets
)
add_test(NAME thermal_workbench_ui_tests COMMAND thermal_workbench_ui_tests)
set_tests_properties(thermal_workbench_ui_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(SEREIN_BUILD_YSEREIN_SWARM_SIM)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    set(SWARM_SIM_DIR "${CMAKE_SOURCE_DIR}/src/plugins/yserein-swarm-sim")
    qt_add_executable(swarm_sim_mavlink_protocol_tests
        SwarmSimMavlinkProtocol_test.cpp
        ${SWARM_SIM_DIR}/Backend/Mavlink/StandardMavlinkCodec.cpp
        ${SWARM_SIM_DIR}/Backend/Mavlink/MavlinkLib.cpp
        ${SWARM_SIM_DIR}/Backend/Mavlink/StandardMAVLib/StandardMavlinkAdapter.cpp
        ${SWARM_SIM_DIR}/Backend/Mavlink/StandardMAVLib/StandardMavlinkWire.cpp
    )
    target_include_directories(swarm_sim_mavlink_protocol_tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${SWARM_SIM_DIR}
        ${SWARM_SIM_DIR}/Backend/Mavlink
        ${SWARM_SIM_DIR}/Backend/Mavlink/MAVLib
        ${SWARM_SIM_DIR}/Backend/Mavlink/MAVLib/all
        ${SWARM_SIM_DIR}/Backend/Mavlink/MAVLib/common
        ${SWARM_SIM_DIR}/Backend/Mavlink/MAVLib/ardupilotmega
        ${SWARM_SIM_DIR}/Backend/Mavlink/MAVLib/minimal
        ${SWARM_SIM_DIR}/Backend/Mavlink/StandardMAVLib
        ${SWARM_SIM_DIR}/Backend/Mavlink/StandardMAVLib/generated
    )
    target_link_libraries(swarm_sim_mavlink_protocol_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME swarm_sim_mavlink_protocol_tests COMMAND swarm_sim_mavlink_protocol_tests)

    qt_add_executable(swarm_sim_box_dance_tests
        SwarmSimBoxDance_test.cpp
    )
    target_link_libraries(swarm_sim_box_dance_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME swarm_sim_box_dance_tests COMMAND swarm_sim_box_dance_tests)
endif()
