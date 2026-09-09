add_library(serein_scope_yserein_hmi MODULE
    plugin.json
    YsereinHmiPlugin.h
    YsereinHmiPlugin.cpp
    YsereinHmiWorkspace.h
    YsereinHmiWorkspace.cpp
    NumericDisplayControl.h
    NumericDisplayControl.cpp
    HmiProjectModel.h
    HmiProjectModel.cpp
    DesignerCanvas.h
    DesignerCanvas.cpp
)

target_link_libraries(serein_scope_yserein_hmi
    PRIVATE serein_scope_core Qt6::Core Qt6::Gui Qt6::Widgets
)

set_target_properties(serein_scope_yserein_hmi PROPERTIES
    OUTPUT_NAME "SereinScope.YsereinHmi"
    PREFIX ""
    SUFFIX ".sscomponent"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
)
