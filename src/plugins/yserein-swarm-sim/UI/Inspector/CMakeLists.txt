qt_add_library(sim_ui_inspector STATIC)
qt_add_qml_module(sim_ui_inspector
    URI YSerein.SwarmSimulator.Inspector
    VERSION 1.0
    QML_FILES
        InspectorPage.qml
)
