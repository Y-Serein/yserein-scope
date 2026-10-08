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
