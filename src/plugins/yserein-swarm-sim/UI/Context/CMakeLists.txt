qt_add_library(sim_ui_context STATIC)
qt_add_qml_module(sim_ui_context
    URI YSerein.SwarmSimulator.Context
    VERSION 1.0
    QML_FILES
        ContextPage.qml
)
