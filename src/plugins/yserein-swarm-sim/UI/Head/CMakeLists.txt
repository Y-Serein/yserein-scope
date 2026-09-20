qt_add_library(sim_ui_head STATIC)
qt_add_qml_module(sim_ui_head
    URI YSerein.SwarmSimulator.Head
    VERSION 1.0
    QML_FILES
        HeadContent.qml
)
