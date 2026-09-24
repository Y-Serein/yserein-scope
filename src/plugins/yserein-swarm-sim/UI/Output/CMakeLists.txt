qt_add_library(sim_ui_output STATIC)
qt_add_qml_module(sim_ui_output
    URI YSerein.SwarmSimulator.Output
    VERSION 1.0
    QML_FILES
        OutputPage.qml
)
