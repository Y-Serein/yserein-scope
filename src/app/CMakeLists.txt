qt_add_executable(SereinScope WIN32
    main.cpp
    ComponentManager.h
    ComponentManager.cpp
    MainWindow.h
    MainWindow.cpp
    QuickWorkbenchController.h
    QuickWorkbenchController.cpp
    PluginCard.h
    PluginCard.cpp
    Theme.h
    Theme.cpp
)

set_source_files_properties(qml/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(SereinScope
    URI Serein.Scope
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/ActivityRail.qml
        qml/components/ComponentLibraryPanel.qml
        qml/components/ConnectionPanel.qml
        qml/components/IconButton.qml
        qml/components/InspectorPanel.qml
        qml/components/MaterialPanel.qml
        qml/components/OutputPanel.qml
        qml/components/ScopeHome.qml
        qml/components/SereinButton.qml
        qml/components/SereinComboBox.qml
        qml/components/SereinField.qml
        qml/components/TopBar.qml
        qml/components/WorkspaceHost.qml
)

qt_add_resources(SereinScope "app_resources"
    PREFIX "/serein"
    FILES
        chevron-down.svg
        grid.svg
        bars.svg
        layers.svg
        wallet.svg
        waveform.svg
        channels.svg
        terminal.svg
        thermal.svg
)

target_link_libraries(SereinScope PRIVATE
    serein_scope_core
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
    Qt6::Widgets
)

if(WIN32)
    target_link_libraries(SereinScope PRIVATE dwmapi)
endif()

set_target_properties(SereinScope PROPERTIES
    OUTPUT_NAME "SereinScope"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
