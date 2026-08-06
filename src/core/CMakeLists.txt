add_library(serein_scope_core SHARED
    include/serein/scope/BufferedDecoder.h
    include/serein/scope/CoreExport.h
    include/serein/scope/IDataEngine.h
    include/serein/scope/Packet.h
    include/serein/scope/PluginInterface.h
    include/serein/scope/SerialTransport.h
    src/BufferedDecoder.cpp
    src/Interfaces.cpp
    src/SerialTransport.cpp
)

target_compile_definitions(serein_scope_core PRIVATE SEREIN_SCOPE_CORE_LIBRARY)
target_include_directories(serein_scope_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(serein_scope_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)

if(WIN32)
    target_compile_definitions(serein_scope_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(serein_scope_core PRIVATE setupapi)
endif()

set_target_properties(serein_scope_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
