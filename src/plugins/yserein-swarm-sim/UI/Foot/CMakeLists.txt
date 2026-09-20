qt_add_library(sim_ui_foot STATIC)
qt_add_qml_module(sim_ui_foot
    URI YSerein.SwarmSimulator.Foot
    VERSION 1.0
    QML_FILES
        FootContent.qml
)
