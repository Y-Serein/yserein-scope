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
add_library(SereinScopeCore::serein_scope_core ALIAS serein_scope_core)

# Core SDK compatibility is versioned independently of the host executable.
set(SEREIN_SCOPE_CORE_VERSION 1.0.1)

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
    VERSION ${SEREIN_SCOPE_CORE_VERSION}
    SOVERSION 0
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# The native SDK is built with the host toolchain and installed separately from
# the runnable application.  Components consume the exported target instead of
# searching arbitrary build trees for a Core DLL/import library pair.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_serein_scope_sdk_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/SereinScopeCore")
configure_package_config_file(
    cmake/SereinScopeCoreConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/SereinScopeCoreConfig.cmake"
    INSTALL_DESTINATION "${_serein_scope_sdk_cmake_dir}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SereinScopeCoreConfigVersion.cmake"
    VERSION "${SEREIN_SCOPE_CORE_VERSION}"
    COMPATIBILITY ExactVersion
)
configure_file(cmake/SDK_INFO.json.in
    "${CMAKE_CURRENT_BINARY_DIR}/SDK_INFO.json" @ONLY)

install(TARGETS serein_scope_core EXPORT SereinScopeCoreTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT SDK
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT SDK
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT SDK
)
install(DIRECTORY include/serein
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT SDK
    FILES_MATCHING PATTERN "*.h"
)
install(EXPORT SereinScopeCoreTargets
    FILE SereinScopeCoreTargets.cmake
    NAMESPACE SereinScopeCore::
    DESTINATION "${_serein_scope_sdk_cmake_dir}"
    COMPONENT SDK
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SereinScopeCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SereinScopeCoreConfigVersion.cmake"
    DESTINATION "${_serein_scope_sdk_cmake_dir}"
    COMPONENT SDK
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SDK_INFO.json"
    DESTINATION "."
    COMPONENT SDK
)
install(FILES "${PROJECT_SOURCE_DIR}/docs/plugin_development.md"
    DESTINATION "."
    RENAME README.md
    COMPONENT SDK
)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION "."
    COMPONENT SDK
)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/tools/component_sdk/"
    DESTINATION "examples/component_sdk"
    COMPONENT SDK
    FILES_MATCHING
    PATTERN "*.txt" PATTERN "*.md" PATTERN "*.h"
    PATTERN "*.cpp" PATTERN "*.json"
)
