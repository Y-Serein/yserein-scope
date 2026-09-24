add_library(serein_scope_core SHARED
    include/serein/scope/AppPaths.h
    include/serein/scope/BufferedDecoder.h
    include/serein/scope/BackendIoTaskGroup.h
    include/serein/scope/BackendRuntime.h
    include/serein/scope/CoreExport.h
    include/serein/scope/DecoderRuntime.h
    include/serein/scope/HttpRequestClient.h
    include/serein/scope/IDataEngine.h
    include/serein/scope/Packet.h
    include/serein/scope/PluginInterface.h
    include/serein/scope/SerialTransport.h
    include/serein/scope/SerialDataSession.h
    include/serein/scope/WorkspaceCanvas.h
    include/serein/scope/WorkspaceComponents.h
    include/serein/scope/WorkspaceDocument.h
    include/serein/scope/ChannelRegistry.h
    include/serein/scope/WorkspaceComponentInspector.h
    src/AppPaths.cpp
    src/BufferedDecoder.cpp
    src/BackendIoTaskGroup.cpp
    src/BackendRuntime.cpp
    src/DecoderRuntime.cpp
    src/HttpRequestClient.cpp
    src/FrontendPacketMailbox.h
    src/Interfaces.cpp
    src/SerialTransport.cpp
    src/SerialTransmitQueue.h
    src/SerialDataSession.cpp
    src/WorkspaceCanvas.cpp
    src/WorkspaceComponents.cpp
    src/WorkspaceDocument.cpp
    src/ChannelRegistry.cpp
    src/WorkspaceComponentInspector.cpp
)

target_compile_definitions(serein_scope_core PRIVATE SEREIN_SCOPE_CORE_LIBRARY)
target_include_directories(serein_scope_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(serein_scope_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
target_link_libraries(serein_scope_core PRIVATE Qt6::Network)

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
