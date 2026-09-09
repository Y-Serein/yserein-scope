add_library(serein_scope_serial_scope MODULE
    plugin.json
    DelimitedEngine.h
    DelimitedEngine.cpp
    SerialScopePlugin.h
    SerialScopePlugin.cpp
    SerialScopeWorkspace.h
    SerialScopeWorkspace.cpp
    WaveformWidget.h
    WaveformWidget.cpp
)

target_link_libraries(serein_scope_serial_scope
    PRIVATE serein_scope_core Qt6::Core Qt6::Gui Qt6::Widgets
)
set_target_properties(serein_scope_serial_scope PROPERTIES
    OUTPUT_NAME "SereinScope.SerialScope"
    PREFIX ""
    SUFFIX ".sscomponent"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
)
