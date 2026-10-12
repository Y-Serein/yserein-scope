cmake_minimum_required(VERSION 3.24)

project(SereinScopeComponentTemplate VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# Serein Scope 组件 SDK 模板
#
# 目标：用宿主 ABI 2 (com.serein.scope.IDevicePlugin/2.0) 构建一个最小的
#       自包含组件。产物是一个 Qt 插件库：
#           <build>/component_packages/SereinScope.Template.sscomponent
#
# 用法（Windows / MinGW 示例）：
#   cmake -S . -B build -G Ninja ^
#         -DSEREIN_SCOPE_SDK_PREFIX=C:/path/to/component-sdk ^
#         -DCMAKE_PREFIX_PATH=C:/DOWNLOAD/Qt/6.11.1/mingw_64
#   cmake --build build
#
# 说明：组件只依赖宿主核心库与 Qt Core/Gui/Widgets。原来的
# SEREIN_SCOPE_ROOT + Core DLL/导入库参数仍可用于旧构建流程。
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)

# ---------------------------------------------------------------------------
# 定位已安装的 Core SDK；旧源码树入口仅作兼容回退。
# ---------------------------------------------------------------------------
set(SEREIN_SCOPE_SDK_PREFIX "" CACHE PATH "已安装的 Serein Scope Core SDK 目录")
set(SEREIN_SCOPE_ROOT "" CACHE PATH "yserein-scope 仓库根目录")
set(SEREIN_SCOPE_CORE_DLL "" CACHE FILEPATH "宿主核心库共享文件 (.dll/.so) 路径")
set(SEREIN_SCOPE_CORE_IMPLIB "" CACHE FILEPATH "宿主核心库导入库 (.dll.a/.lib) 路径")

if(SEREIN_SCOPE_SDK_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${SEREIN_SCOPE_SDK_PREFIX}")
    find_package(SereinScopeCore CONFIG REQUIRED)
elseif(NOT SEREIN_SCOPE_ROOT AND NOT DEFINED ENV{SEREIN_SCOPE_ROOT})
    find_package(SereinScopeCore CONFIG QUIET)
endif()

if(SereinScopeCore_FOUND)
    set(_serein_scope_core_target SereinScopeCore::serein_scope_core)
    if(NOT SEREIN_SCOPE_PLUGIN_ABI EQUAL 2)
        message(FATAL_ERROR "SDK 插件 ABI 不是此模板支持的 ABI 2")
    endif()
    message(STATUS "[template] 使用已安装的 Core SDK ${SEREIN_SCOPE_CORE_VERSION}")
else()
    message(WARNING "未找到已安装的 Core SDK，使用旧源码树入口；建议改用 SEREIN_SCOPE_SDK_PREFIX")

    if(NOT SEREIN_SCOPE_ROOT AND DEFINED ENV{SEREIN_SCOPE_ROOT})
        set(SEREIN_SCOPE_ROOT "$ENV{SEREIN_SCOPE_ROOT}")
    endif()
    if(NOT SEREIN_SCOPE_ROOT)
        message(FATAL_ERROR
            "请提供 -DSEREIN_SCOPE_SDK_PREFIX=/path/to/component-sdk；"
            "旧流程可用 SEREIN_SCOPE_ROOT")
    endif()

    set(_core_inc "${SEREIN_SCOPE_ROOT}/src/core/include")
    if(NOT EXISTS "${_core_inc}/serein/scope/PluginInterface.h")
        message(FATAL_ERROR
            "SEREIN_SCOPE_ROOT 无效：缺少 ${_core_inc}/serein/scope/PluginInterface.h")
    endif()

    # 旧入口从 <root>/build*/ 搜索 Core。多份产物时仍按原行为
    # 取第一个；正式组件构建应使用上面的确定性 SDK 包。
    if(NOT SEREIN_SCOPE_CORE_DLL OR (WIN32 AND NOT SEREIN_SCOPE_CORE_IMPLIB))
        if(NOT SEREIN_SCOPE_CORE_DLL)
            file(GLOB_RECURSE _core_dll_candidates LIST_DIRECTORIES false
                "${SEREIN_SCOPE_ROOT}/build*/libserein_scope_core.dll"
                "${SEREIN_SCOPE_ROOT}/build*/libserein_scope_core.so")
            if(_core_dll_candidates)
                list(GET _core_dll_candidates 0 SEREIN_SCOPE_CORE_DLL)
            endif()
        endif()
        if(WIN32 AND NOT SEREIN_SCOPE_CORE_IMPLIB)
            file(GLOB_RECURSE _core_implib_candidates LIST_DIRECTORIES false
                "${SEREIN_SCOPE_ROOT}/build*/libserein_scope_core.dll.a"
                "${SEREIN_SCOPE_ROOT}/build*/serein_scope_core.lib")
            if(_core_implib_candidates)
                list(GET _core_implib_candidates 0 SEREIN_SCOPE_CORE_IMPLIB)
            endif()
        endif()
    endif()

    if(WIN32 AND NOT SEREIN_SCOPE_CORE_IMPLIB)
        message(FATAL_ERROR
            "Windows 下未找到 serein_scope_core 导入库 (.dll.a/.lib)，"
            "请显式传 -DSEREIN_SCOPE_CORE_IMPLIB=...")
    endif()
    if(NOT SEREIN_SCOPE_CORE_DLL)
        message(FATAL_ERROR
            "未找到 serein_scope_core 共享库 (.dll/.so)，"
            "请显式传 -DSEREIN_SCOPE_CORE_DLL=...")
    endif()

    add_library(serein_scope_core SHARED IMPORTED)
    set_target_properties(serein_scope_core PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_core_inc}"
        IMPORTED_LOCATION "${SEREIN_SCOPE_CORE_DLL}"
    )
    if(SEREIN_SCOPE_CORE_IMPLIB)
        set_target_properties(serein_scope_core PROPERTIES
            IMPORTED_IMPLIB "${SEREIN_SCOPE_CORE_IMPLIB}")
    endif()
    set(_serein_scope_core_target serein_scope_core)
endif()

# ---------------------------------------------------------------------------
# 组件库
# ---------------------------------------------------------------------------
set(COMPONENT_OUTPUT_DIR "${CMAKE_BINARY_DIR}/component_packages")

add_library(template_component MODULE
    src/plugin.json
    src/TemplatePlugin.h
    src/TemplatePlugin.cpp
    src/TemplateWorkspace.h
    src/TemplateWorkspace.cpp
)

target_link_libraries(template_component PRIVATE
    ${_serein_scope_core_target}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

set_target_properties(template_component PROPERTIES
    OUTPUT_NAME "SereinScope.Template"
    PREFIX ""
    SUFFIX ".sscomponent"
    RUNTIME_OUTPUT_DIRECTORY "${COMPONENT_OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${COMPONENT_OUTPUT_DIR}"
)

message(STATUS "[template] 组件输出: ${COMPONENT_OUTPUT_DIR}/SereinScope.Template.sscomponent")
