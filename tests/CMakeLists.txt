find_package(Qt6 6.4 REQUIRED COMPONENTS Test CorePrivate)

foreach(version IN ITEMS v1 v2 v3)
    string(TOUPPER "${version}" VERSION_UPPER)
    add_library(scope_test_plugin_${version} MODULE
        fixtures/TestPlugin${VERSION_UPPER}.cpp
    )
    target_link_libraries(scope_test_plugin_${version} PRIVATE
        serein_scope_core Qt6::Core Qt6::Widgets
    )
    set_target_properties(scope_test_plugin_${version} PROPERTIES
        PREFIX "" SUFFIX ".sscomponent"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-fixtures"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-fixtures"
    )
endforeach()

qt_add_executable(plugin_loading_tests
    PluginLoading_test.cpp
    ../src/app/ComponentManager.cpp
    ../src/app/ComponentManager.h
)
target_include_directories(plugin_loading_tests PRIVATE ../src/app)
target_link_libraries(plugin_loading_tests PRIVATE
    serein_scope_core Qt6::Core Qt6::CorePrivate Qt6::Test Qt6::Widgets
)
target_compile_definitions(plugin_loading_tests PRIVATE
    TEST_PLUGIN_V1_PATH="$<TARGET_FILE:scope_test_plugin_v1>"
    TEST_PLUGIN_V2_PATH="$<TARGET_FILE:scope_test_plugin_v2>"
    TEST_PLUGIN_V3_PATH="$<TARGET_FILE:scope_test_plugin_v3>"
)
add_dependencies(plugin_loading_tests scope_test_plugin_v1 scope_test_plugin_v2
                 scope_test_plugin_v3)
add_test(NAME plugin_loading_tests COMMAND plugin_loading_tests)

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

qt_add_executable(t384_oem_k_panel_tests
    T384OemKPanel_test.cpp
    ../src/plugins/t384/T384OemKPanel.cpp
    ../src/plugins/t384/T384OemKPanel.h
)
target_include_directories(t384_oem_k_panel_tests PRIVATE ../src/plugins/t384)
target_link_libraries(t384_oem_k_panel_tests PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::Test)
add_test(NAME t384_oem_k_panel_tests COMMAND t384_oem_k_panel_tests)
set_tests_properties(t384_oem_k_panel_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
