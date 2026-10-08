add_library(serein_scope_tn160_ncm MODULE
    ../Tn160ProductionStateGate.h
    Tn160NcmProtocolUtils.h
    plugin.json
    CalibrationPage.h
    CalibrationPage.cpp
    CalibrationTransfer.h
    CalibrationTransfer.cpp
    FirmwarePage.h
    FirmwarePage.cpp
    Tn160FirmwareProfileGenerator.h
    Tn160FirmwareProfileGenerator.cpp
    LivePage.h
    LivePage.cpp
    ProductionCalibrationPage.h
    ProductionCalibrationPage.cpp
    Tn160CalibrationValidation.h
    ProductionCalibrationHub.h
    ProductionCalibrationHub.cpp
    Tn160ProductionFrameFilter.h
    Tn160ProductionFrameFilter.cpp
    Tn160ProductionTelemetry.h
    Tn160ProductionWorkflow.h
    ThermalView.h
    ThermalView.cpp
    Tn160Engine.h
    Tn160Engine.cpp
    Tn160NcmTransport.h
    Tn160NcmTransport.cpp
    Tn160ProtocolEngines.h
    Tn160ProtocolEngines.cpp
    Tn160Plugin.h
    Tn160Plugin.cpp
    Tn160Workspace.h
    Tn160Workspace.cpp
    TrendWidget.h
    TrendWidget.cpp
)

target_link_libraries(serein_scope_tn160_ncm
    PRIVATE serein_scope_core serein_scope_thermal_workbench
            Qt6::Core Qt6::Gui Qt6::Widgets
)
set_target_properties(serein_scope_tn160_ncm PROPERTIES
    OUTPUT_NAME "SereinScope.TN160Ncm"
    PREFIX ""
    SUFFIX ".sscomponent"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
)
