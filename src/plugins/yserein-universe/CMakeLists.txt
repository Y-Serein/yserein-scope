add_library(serein_scope_yserein_universe MODULE
    plugin.json
    YsereinUniversePlugin.h
    YsereinUniversePlugin.cpp
    YsereinUniverseWorkspace.h
    YsereinUniverseWorkspace.cpp
    UniversePipeline.h
    UniversePipeline.cpp
)

target_link_libraries(serein_scope_yserein_universe
    PRIVATE serein_scope_core Qt6::Core Qt6::Gui Qt6::Widgets
)

set_target_properties(serein_scope_yserein_universe PROPERTIES
    OUTPUT_NAME "SereinScope.YsereinUniverse"
    PREFIX ""
    SUFFIX ".sscomponent"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
)
