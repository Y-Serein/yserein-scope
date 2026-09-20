qt_add_library(sim_ui_main STATIC)
qt_add_qml_module(sim_ui_main
    URI YSerein.SwarmSimulator.Main
    VERSION 1.0
    QML_FILES
        App.qml
        HeadComponent.qml
        FootComponent.qml
        MiddleComponent.qml
)
