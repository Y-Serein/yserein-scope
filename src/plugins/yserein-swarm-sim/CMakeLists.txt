# ============================================================================
# YSerein Swarm Simulator scope component
# ============================================================================
# The standalone YSerein Swarm Simulator (multisim) renderer is embedded into
# Scope as a "Device component" (IDevicePlugin / QWidget workspace).  The
# original simulator sources (Backend / Common / Frontend / UI) are compiled
# directly into a single MODULE so Scope's host architecture stays unchanged.
#
# This component is optional and gated by SEREIN_BUILD_YSEREIN_SWARM_SIM in
# src/plugins/CMakeLists.txt so the base Scope build is not broken when the
# simulator sources or extra Qt modules are absent.

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Core5Compat
    CorePrivate
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
    QuickWidgets
    Quick3D
    Positioning
    Network
    Concurrent
)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0002 NEW)
qt_policy(SET QTP0003 NEW)
qt_policy(SET QTP0004 NEW)

set(SEREIN_YSEREIN_SWARM_SIM_SOURCES
    # ---- Plugin entry / workspace --------------------------------------------
    SwarmSimPlugin.h
    SwarmSimPlugin.cpp
    SwarmSimWorkspace.h
    SwarmSimWorkspace.cpp
    plugin.json
    zsm.qrc

    # ---- Backend -------------------------------------------------------------
    Backend/ZSMBackend.cpp
    Backend/ZSMBackend.h
    Backend/Comms/LinkManager.cpp
    Backend/Comms/LinkManager.h
    Backend/Comms/MavlinkDistributor.cpp
    Backend/Comms/MavlinkDistributor.h
    Backend/Comms/UDPWin/NetUtils.cpp
    Backend/Comms/UDPWin/NetUtils.h
    Backend/Comms/UDPWin/NetWorkUtilities.cpp
    Backend/Comms/UDPWin/NetWorkUtilities.h
    Backend/Comms/UDPWin/UDPQtClient.h
    Backend/Comms/UDPWin/UDPWinLink.cpp
    Backend/Comms/UDPWin/UDPWinLink.h
    Backend/Comms/UDPWin/UDPWinReceiver.cpp
    Backend/Comms/UDPWin/UDPWinReceiver.h
    Backend/Comms/UDPWin/UDPWinSender.cpp
    Backend/Comms/UDPWin/UDPWinSender.h
    Backend/Comms/UDPWin/UDPWinSenderWorker.cpp
    Backend/Comms/UDPWin/UDPWinSenderWorker.h
    Backend/Mavlink/MAVLinkFTP.cpp
    Backend/Mavlink/MAVLinkFTP.h
    Backend/Mavlink/MavlinkLib.cpp
    Backend/Mavlink/MavlinkLib.h
    Backend/Mavlink/MavlinkProtocol.cpp
    Backend/Mavlink/MavlinkProtocol.h
    Backend/Mavlink/StandardMavlinkCodec.cpp
    Backend/Mavlink/StandardMavlinkCodec.h
    Backend/Mavlink/StandardMAVLib/StandardMavlinkAdapter.cpp
    Backend/Mavlink/StandardMAVLib/StandardMavlinkAdapter.h
    Backend/Mavlink/StandardMAVLib/StandardMavlinkWire.cpp
    Backend/Mavlink/StandardMAVLib/StandardMavlinkWire.h
    Backend/MockVehicle/MockVehicleManager.cpp
    Backend/MockVehicle/MockVehicleManager.h
    Backend/MockVehicle/Vehicle/MockVehicle.cpp
    Backend/MockVehicle/Vehicle/MockVehicle.h
    Backend/MockVehicle/Vehicle/MockVehicleFTP.cpp
    Backend/MockVehicle/Vehicle/MockVehicleFTP.h
    Backend/MockVehicle/Vehicle/MockVehiclePool.cpp
    Backend/MockVehicle/Vehicle/MockVehiclePool.h
    Backend/MockVehicle/Vehicle/MockVehicleWorker.cpp
    Backend/MockVehicle/Vehicle/MockVehicleWorker.h

    # ---- Common --------------------------------------------------------------
    Common/Geo/ZSMGeo.cpp
    Common/Geo/ZSMGeo.h
    Common/Time/ZSMTimeController.cpp
    Common/Time/ZSMTimeController.h
    Common/Time/ZSMTime.h
    Common/Time/qtaggedtime_p.h
    Common/Types/VehicleStruct.h
    Common/Types/BatteryStruct.h
    Common/Types/ZSMTypes.h
    Common/Types/GPSStruct.h
    Common/Types/ZSMStatic.h
    Common/QueueNonBlocking.h
    Common/ThreadSafeQueue.h
    Common/ZSMJsonHelper.cpp
    Common/ZSMJsonHelper.h
    Common/ZSMLoggingCategory.cpp
    Common/ZSMLoggingCategory.h
    Common/ZSMScopedTimer.cpp
    Common/ZSMScopedTimer.h
    Common/ZSMZipHelper.cpp
    Common/ZSMZipHelper.h

    # ---- Frontend ------------------------------------------------------------
    Frontend/ZSMApplication.cpp
    Frontend/ZSMApplication.h
    Frontend/DroneShow/DroneShowController.cpp
    Frontend/DroneShow/DroneShowController.h
    Frontend/DroneShow/DroneSpecification.cpp
    Frontend/DroneShow/DroneSpecification.h
    Frontend/DroneShow/ShowSpecification.cpp
    Frontend/DroneShow/ShowSpecification.h
    Frontend/DroneShow/ShowTypes.h
    Frontend/DroneShow/DroneTypes.h
    Frontend/Map3D/OSMTest/OSMManager.cpp
    Frontend/Map3D/OSMTest/OSMManager.h
    Frontend/Map3D/OSMTest/OSMRequest.cpp
    Frontend/Map3D/OSMTest/OSMRequest.h
    Frontend/MockVehicle/MockVehicleController.cpp
    Frontend/MockVehicle/MockVehicleController.h
    Frontend/MockVehicle/MockVehiclePropertys.cpp
    Frontend/MockVehicle/MockVehiclePropertys.h
    Frontend/QmlController/QmlChartLineModel.cpp
    Frontend/QmlController/QmlChartLineModel.h
    Frontend/QmlController/QmlObjectListModel.cc
    Frontend/QmlController/QmlObjectListModel.h
    Frontend/QmlController/ZSMQmlGlobal.cpp
    Frontend/QmlController/ZSMQmlGlobal.h
    Frontend/Show3D/DroneShowInstanceTable.cpp
    Frontend/Show3D/DroneShowInstanceTable.h
    Frontend/Show3D/DroneShowPlayerController.cpp
    Frontend/Show3D/DroneShowPlayerController.h
    Frontend/Show3D/DroneShowPlayerEngine.cpp
    Frontend/Show3D/DroneShowPlayerEngine.h
    Frontend/Show3D/SphereModelController.cpp
    Frontend/Show3D/SphereModelController.h
)

# ---- Embedded simulator C++ -------------------------------------------------
add_library(serein_scope_swarm_sim MODULE
    ${SEREIN_YSEREIN_SWARM_SIM_SOURCES}
)

target_include_directories(serein_scope_swarm_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Comms
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Comms/UDPWin
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/MAVLib
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/MAVLib/all
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/MAVLib/common
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/MAVLib/development
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/MAVLib/ardupilotmega
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/MAVLib/minimal
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/StandardMAVLib
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/StandardMAVLib/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/StandardMAVLib/generated/ardupilotmega
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/Mavlink/StandardMAVLib/generated/common
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/MockVehicle
    ${CMAKE_CURRENT_SOURCE_DIR}/Backend/MockVehicle/Vehicle
    ${CMAKE_CURRENT_SOURCE_DIR}/Common
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/Geo
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/Time
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/Types
    ${CMAKE_CURRENT_SOURCE_DIR}/Frontend
    ${CMAKE_CURRENT_SOURCE_DIR}/Frontend/DroneShow
    ${CMAKE_CURRENT_SOURCE_DIR}/Frontend/Map3D/OSMTest
    ${CMAKE_CURRENT_SOURCE_DIR}/Frontend/MockVehicle
    ${CMAKE_CURRENT_SOURCE_DIR}/Frontend/QmlController
    ${CMAKE_CURRENT_SOURCE_DIR}/Frontend/Show3D
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libzds/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/concurrentqueue
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/readerwriterqueue
)

# ---- QML modules ------------------------------------------------------------
# The simulator UI is split into several static QML modules (one per URI).
# Each module lives in its own source directory so qt_add_qml_module computes
# the correct resource URLs, e.g. UI/Main/App.qml is published as
# ":/qt/qml/YSerein.SwarmSimulator/Main/App.qml" (see UI/*/CMakeLists.txt).
#
# Static modules are linked into the final MODULE with whole-archive to keep the
# QML types, resource initialisers and registration objects; no separate plugin
# DLLs must be shipped next to the .sscomponent.

qt_add_library(sim_ui_root STATIC)
qt_add_qml_module(sim_ui_root
    URI YSerein.SwarmSimulator
    VERSION 1.0
    QML_FILES
        UI/Main.qml
)

add_subdirectory(UI)

# ---- libzds -----------------------------------------------------------------
set(LIBZDS_BUILD_TESTS OFF)
add_subdirectory(third_party/libzds)

# ---- Link -------------------------------------------------------------------
target_link_libraries(serein_scope_swarm_sim PRIVATE
    serein_scope_core
    Qt6::Core
    Qt6::Core5Compat
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickWidgets
    Qt6::Quick3D
    Qt6::Positioning
    Qt6::Network
    Qt6::Concurrent
)

foreach(lib
        sim_ui_rootplugin
        sim_ui_mainplugin
        sim_ui_configplugin
        sim_ui_zcontrolsplugin
        sim_ui_middleplugin
        sim_ui_debugplugin
        sim_ui_droneplugin
        sim_ui_settingsplugin
        sim_ui_headplugin
        sim_ui_footplugin
        sim_ui_contextplugin
        sim_ui_inspectorplugin
        sim_ui_outputplugin
        sim_ui_resourcesplugin)
    target_link_libraries(serein_scope_swarm_sim PRIVATE
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,${lib}>"
    )
endforeach()

target_link_libraries(serein_scope_swarm_sim PRIVATE libzds)

if(WIN32)
    # The UDP sources use Windows Registered I/O (RIO, see <MSWsock.h>);
    # MinGW keeps those types behind _WIN32_WINNT >= 0x0602 while the Windows
    # SDK/MSVC default already exposes them, so pin the modern value.
    target_compile_definitions(serein_scope_swarm_sim PRIVATE _WIN32_WINNT=0x0A00)
endif()

if(WIN32 AND MINGW)
    # MSVC honours the "pragma comment(lib,...)" hints in the UDP sources but
    # MinGW-w64 does not, so pull in the socket library explicitly.
    target_link_libraries(serein_scope_swarm_sim PRIVATE ws2_32)
endif()

set_target_properties(serein_scope_swarm_sim PROPERTIES
    OUTPUT_NAME "SereinScope.YSereinSwarmSimulator"
    PREFIX ""
    SUFFIX ".sscomponent"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/component_packages"
)
