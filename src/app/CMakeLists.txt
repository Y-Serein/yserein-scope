qt_add_executable(SereinScope WIN32
    main.cpp
    ComponentManager.h
    ComponentManager.cpp
    MainWindow.h
    MainWindow.cpp
    PluginCard.h
    PluginCard.cpp
    Theme.h
    Theme.cpp
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
