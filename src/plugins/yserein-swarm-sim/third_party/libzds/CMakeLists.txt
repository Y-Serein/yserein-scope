cmake_minimum_required(VERSION 3.24)

project(libzds VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(libzds STATIC
    src/utils/math.h
    src/utils/status.cpp
    src/drone/trajectory_segment.cpp
    src/drone/trajectory.cpp
    src/drone/light_program.cpp
)

target_include_directories(libzds
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
