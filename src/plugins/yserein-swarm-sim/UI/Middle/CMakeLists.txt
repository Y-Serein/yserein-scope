add_subdirectory(DebugView)
add_subdirectory(DroneView)
add_subdirectory(SettingsView)

qt_add_library(sim_ui_middle STATIC)
qt_add_qml_module(sim_ui_middle
    URI YSerein.SwarmSimulator.Middle
    VERSION 1.0
    QML_FILES
        MiddleContent.qml
        MiddleSidebar.qml
        MiddleSidebarButton.qml

        # Map3D
        Map3D/Map3DTest.qml
        Map3D/OSMCameraController.qml
)
