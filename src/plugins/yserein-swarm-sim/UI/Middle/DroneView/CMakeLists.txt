qt_add_library(sim_ui_drone STATIC)
qt_add_qml_module(sim_ui_drone
    URI YSerein.SwarmSimulator.Middle.Drone
    VERSION 1.0
    QML_FILES
        DroneView.qml
        DroneForm/DroneFormView.qml
        DroneForm/DroneList.qml
        DroneDetail/DroneDetailView.qml
        World3DView/World3DViewTest.qml
        World3DView/World3DView.qml
        World3DView/World3DViewInstance.qml
)
