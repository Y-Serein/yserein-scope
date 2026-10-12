# Serein Scope 组件 SDK 模板

这是一个可供第三方在扩展 Scope 宿主时直接复用的**最小组件骨架**。
它实现了宿主 ABI 2 的插件接口（`com.serein.scope.IDevicePlugin/2.0`），
构建产物是一个 Qt 插件库：`SereinScope.Template.sscomponent`。

## 目标

- 让「从零写一个组件」只做 3 件事：
  1. 复制这个模板；
  2. 改 `plugin.json` 和 `metadata()` 里的 id / name；
  3. 在 `TemplateWorkspace` 里写界面。
- 只使用宿主已部署的 Core/Qt 库时，组件目录可只含 `.sscomponent`。
- 宿主已带 `Qt6Core`/`Qt6Gui`/`Qt6Widgets`，组件只依赖这些模块时不需要额外 DLL。

## 目录结构

```text
component_sdk/
├── CMakeLists.txt          # 顶层构建脚本
├── README.md
└── src/
    ├── plugin.json         # 插件元数据（编译期序列化进 .sscomponent）
    ├── TemplatePlugin.h    # IDevicePlugin 实现头
    ├── TemplatePlugin.cpp
    ├── TemplateWorkspace.h # 最小工作区界面
    └── TemplateWorkspace.cpp
```

## 前置条件

- 与目标宿主同一版本、同一平台和编译器生成的 Core SDK 包；无需仓库源码。
- 与 SDK 相同的 Qt 版本和编译器；配置时会检查，不匹配会直接失败。
- CMake 3.24+，Ninja。

## 构建

```bat
cmake -S . -B build -G Ninja ^
      -DSEREIN_SCOPE_SDK_PREFIX=C:/path/to/component-sdk ^
      -DCMAKE_PREFIX_PATH=C:/DOWNLOAD/Qt/6.11.1/mingw_64
cmake --build build
```

- `SEREIN_SCOPE_SDK_PREFIX` 指向解压后的 `component-sdk` 目录，内含
  `include/`、`lib/cmake/SereinScopeCore/`、`lib/`、`bin/` 和本模板；
  `SDK_INFO.json` 记录目标 Qt、编译器和 ABI，`SHA256SUMS.txt` 校验包内文件。
  根目录 `LICENSE` 是 Core 及示例源码的 MIT 许可文本。
- 模板通过 `find_package(SereinScopeCore CONFIG)` 使用导出的
  `SereinScopeCore::serein_scope_core`，不搜索其他构建树。
- 原 `SEREIN_SCOPE_ROOT`、`SEREIN_SCOPE_CORE_DLL` 和
  `SEREIN_SCOPE_CORE_IMPLIB` 入口仍可用，用于迁移旧构建；新组件应使用 SDK 包。
- SDK 包的 CMake 配置检查 Qt 精确版本、编译器及其版本、CPU 架构与目标平台。
  宿主在加载时还会核对组件 ID、版本和插件 ABI 2。Core 的 C++/Qt ABI
  仍要求组件与目标宿主使用同一次兼容的 Core 构建。

产物位置：

```text
build/component_packages/SereinScope.Template.sscomponent
```

## 部署：自包含目录形式

宿主从可执行文件旁的 `Components/` 发现组件（见 `ComponentManager`），
也可以在组件管理界面导入独立目录、`.sscomponent` 或自包含 ZIP：

```text
# 形式：<store>/<id>/<id>.sscomponent
Components\template-component\template-component.sscomponent
```

- 默认组件仓库：`<exe目录>/Components/<id>/`。是否可写取决于安装目录权限。
- 可用环境变量 `SEREIN_COMPONENT_DIR` 覆盖组件仓库路径。

宿主扫描子目录里的 `*.sscomponent` 并读取元数据 id；目录名建议与 id
一致，便于管理。模板构建出的文件名是 `SereinScope.Template.sscomponent`，
部署时可按上例重命名为 `template-component.sscomponent`。

## 改成一个新组件

1. 把目录里所有 `Template` 前缀改名，例如 `MyThing`。
2. 改 `plugin.json` 的 `id` / `name` / `description` / `version`。
3. 改 `TemplatePlugin::metadata()`，与 `plugin.json` 保持一致。
4. 在 `CMakeLists.txt` 里改 `OUTPUT_NAME`（保持 `SereinScope.` 前缀约定）。
5. 在 `TemplateWorkspace.cpp` 里写业务内容。中央组件使用 Core
   `WorkspaceCanvas` / `ComponentSurface`，通过 `IWorkspaceUiProvider` 声明
   宿主组件库和外围语义槽。可用 QML 时自行加入 `Qt6::Qml` 等。

## 只要必装组件很干净

- 只链接 `Core/Gui/Widgets` 的组件，文件夹里只需要 `.sscomponent` 一个文件。
  Qt 的 `Qt6Core.dll` 等会从宿主 exe 目录解析到。
- 若组件引入额外 Qt 模块（如 Quick3D、Positioning），用 Qt 自带 `windeployqt`
  把对应 DLL 放进该组件目录，并确保这些 DLL 与组件同名归于目录，宿主在加载前
  会通过 `SetDllDirectory` 把组件目录加入 DLL 搜索路径。

```bat
windeployqt --release --compiler-runtime --no-translations ^
            build\component_packages\SereinScope.MyThing.sscomponent
```

注意 `windeployqt` 默认把依赖部署到 `.sscomponent` 所在目录；部署后把整个目录
拷贝为 `Components\<id>\` 即可，不要只拷贝 `.sscomponent` 一个文件。

## 加入基础安装包（可选）

`tools/windows_user_action.ps1` 中的 `$componentRegistry` 把每个组件映射到一个
CMake 开关。若要成为默认安装项之一，把对应 key `/name` 加进 `$baseComponents`；
若它带额外 DLL，则按 `$heavyComponents` 的方式单独发布成一个自包含 pack。
