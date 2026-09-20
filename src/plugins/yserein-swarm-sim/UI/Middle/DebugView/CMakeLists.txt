qt_add_library(sim_ui_debug STATIC)
qt_add_qml_module(sim_ui_debug
    URI YSerein.SwarmSimulator.Middle.Debug
    VERSION 1.0
    QML_FILES
        DebugView.qml
)
