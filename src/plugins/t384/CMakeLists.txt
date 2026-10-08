add_library(serein_scope_t384 MODULE
    plugin.json
    T384Plugin.h
    T384Plugin.cpp
    T384Workspace.h
    T384Workspace.cpp
    T384Transport.h
    T384Transport.cpp
    T384Engine.h
    T384Engine.cpp
    T384CalibrationPage.h
    T384CalibrationPage.cpp
    T384CalibrationProtocol.h
    T384CalibrationProtocol.cpp
    T384CalibrationTransaction.h
    T384CalibrationTransaction.cpp
    T384ProductionWorkflow.h
    ThermalView.h
    ThermalView.cpp
    TrendWidget.h
    TrendWidget.cpp
)

target_link_libraries(serein_scope_t384
    PRIVATE serein_scope_core serein_scope_thermal_workbench
            Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)
set_target_properties(serein_scope_t384 PROPERTIES
    OUTPUT_NAME "SereinScope.T384"
    PREFIX ""
    SUFFIX ".sscomponent"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
)
