qt_add_executable(engine_tests
    EngineTests.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/serial_scope/DelimitedEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/tn160/Tn160Engine.cpp
)
target_include_directories(engine_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugins/serial_scope
    ${CMAKE_SOURCE_DIR}/src/plugins/tn160
)
target_link_libraries(engine_tests PRIVATE serein_scope_core Qt6::Core Qt6::Test)
add_test(NAME engine_tests COMMAND engine_tests)

qt_add_executable(plugin_loading_tests
    PluginLoadingTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ComponentManager.h
    ${CMAKE_SOURCE_DIR}/src/app/ComponentManager.cpp
)
target_include_directories(plugin_loading_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(plugin_loading_tests PRIVATE serein_scope_core Qt6::Core Qt6::Widgets Qt6::Test)
add_dependencies(plugin_loading_tests serein_scope_serial_scope serein_scope_tn160)
add_test(NAME plugin_loading_tests
    COMMAND plugin_loading_tests
)
set_tests_properties(plugin_loading_tests PROPERTIES
    ENVIRONMENT "SEREIN_TEST_COMPONENT_DIR=$<TARGET_FILE_DIR:serein_scope_tn160>;QT_QPA_PLATFORM=offscreen"
)

qt_add_executable(workbench_layout_tests
    WorkbenchLayoutTests.cpp
    ${CMAKE_SOURCE_DIR}/src/app/ComponentManager.h
    ${CMAKE_SOURCE_DIR}/src/app/ComponentManager.cpp
    ${CMAKE_SOURCE_DIR}/src/app/MainWindow.h
    ${CMAKE_SOURCE_DIR}/src/app/MainWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PluginCard.h
    ${CMAKE_SOURCE_DIR}/src/app/PluginCard.cpp
)
target_include_directories(workbench_layout_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/app)
target_link_libraries(workbench_layout_tests PRIVATE
    serein_scope_core Qt6::Core Qt6::Gui Qt6::Svg Qt6::Widgets Qt6::Test
)
if(WIN32)
    target_link_libraries(workbench_layout_tests PRIVATE dwmapi)
endif()
set_source_files_properties(../src/app/grid.svg PROPERTIES QT_RESOURCE_ALIAS grid.svg)
set_source_files_properties(../src/app/bars.svg PROPERTIES QT_RESOURCE_ALIAS bars.svg)
set_source_files_properties(../src/app/layers.svg PROPERTIES QT_RESOURCE_ALIAS layers.svg)
set_source_files_properties(../src/app/wallet.svg PROPERTIES QT_RESOURCE_ALIAS wallet.svg)
qt_add_resources(workbench_layout_tests "app_test_resources"
    PREFIX "/serein"
    FILES
        ../src/app/grid.svg
        ../src/app/bars.svg
        ../src/app/layers.svg
        ../src/app/wallet.svg
)
add_test(NAME workbench_layout_tests COMMAND workbench_layout_tests)
set_tests_properties(workbench_layout_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test_config;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/test_data"
)

qt_add_executable(workspace_canvas_tests
    WorkspaceCanvasTests.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/tn160/WorkspaceCanvas.h
    ${CMAKE_SOURCE_DIR}/src/plugins/tn160/WorkspaceCanvas.cpp
)
target_include_directories(workspace_canvas_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugins/tn160
)
target_link_libraries(workspace_canvas_tests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
)
add_test(NAME workspace_canvas_tests COMMAND workspace_canvas_tests)
set_tests_properties(workspace_canvas_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test_config;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/test_data"
)
