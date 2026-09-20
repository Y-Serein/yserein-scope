find_package(Qt6 REQUIRED COMPONENTS Core)

qt_add_library(MAVLib INTERFACE)

target_include_directories(MAVLib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/all
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)


message("-- [mavlink] " ${CMAKE_CURRENT_SOURCE_DIR})
