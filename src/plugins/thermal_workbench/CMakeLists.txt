add_library(serein_scope_thermal_workbench STATIC
    ThermalWorkbenchUi.h
    ThermalWorkbenchUi.cpp
)

target_include_directories(serein_scope_thermal_workbench PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(serein_scope_thermal_workbench
    PUBLIC Qt6::Core Qt6::Widgets
)
set_target_properties(serein_scope_thermal_workbench PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
