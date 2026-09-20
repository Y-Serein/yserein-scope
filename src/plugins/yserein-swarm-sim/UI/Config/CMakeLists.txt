set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(Styles.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(sim_ui_config STATIC)
qt_add_qml_module(sim_ui_config
    URI YSerein.SwarmSimulator.Config
    VERSION 1.0
    QML_FILES
        Theme.qml
        Styles.qml
)
