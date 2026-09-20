qt_add_library(sim_ui_settings STATIC)
qt_add_qml_module(sim_ui_settings
    URI YSerein.SwarmSimulator.Middle.Settings
    VERSION 1.0
    QML_FILES
        SettingsView.qml
)
