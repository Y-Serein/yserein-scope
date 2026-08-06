add_library(serein_scope_tn160 MODULE
    plugin.json
    CalibrationPage.h
    CalibrationPage.cpp
    CalibrationTransfer.h
    CalibrationTransfer.cpp
    FirmwarePage.h
    FirmwarePage.cpp
    LivePage.h
    LivePage.cpp
    ThermalView.h
    ThermalView.cpp
    Tn160Engine.h
    Tn160Engine.cpp
    Tn160PortSelection.h
    Tn160Plugin.h
    Tn160Plugin.cpp
    Tn160Workspace.h
    Tn160Workspace.cpp
    TrendWidget.h
    TrendWidget.cpp
    WorkspaceCanvas.h
    WorkspaceCanvas.cpp
)

target_link_libraries(serein_scope_tn160
    PRIVATE serein_scope_core Qt6::Core Qt6::Gui Qt6::Widgets
)
set_target_properties(serein_scope_tn160 PROPERTIES
    OUTPUT_NAME "SereinScope.TN160"
    PREFIX ""
    SUFFIX ".sscomponent"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
)
