# Serein Scope

Serein Scope 是一个使用 **C++20 + Qt 6** 实现的可扩展桌面调试仪器，提供通用串口工作台和设备专项组件。

当前版本：`1.0.1`

## 已实现

- 原生插件宿主：独立 `.sscomponent` 插件包、用户插件仓库、ABI 版本检查和真实运行时加载/卸载。
- 插件管理：导入、加载、打开、关闭工作区、卸载 DLL、移除安装包是彼此独立的操作。
- VOFA式工作台：中央工作区保持纯净；左侧导航、右侧信息和底部输出均可独立打开、关闭、换边停靠或浮动为独立窗口。
- 布局记忆：首次启动与恢复默认布局时隐藏全部可选面板；用户调整后的窗口和 Dock 状态跨启动保存。
- `Serial Scope` 独立插件：
  - 原生串口扫描和异步收发；
  - 多通道文本引擎，例如 `1.2,3.4,5.6\n`；
  - 组件化实时波形、数据通道、串口工具区和收发终端，支持布局编辑、拖动、缩放、显隐和跨启动记忆；
  - Raw Text 引擎。
- `TN160 Thermal` 独立插件：
  - 安全批量 UF2 烧录；
  - 当前 19255 字节监控协议的重同步与 telemetry 合理性校验；
  - 160×120 热图、Center 5×5、板载 NTC、Center ADU、VTEMP、FPS；
  - 温度与 ADU 分轴趋势，避免不同量纲曲线重叠；
  - 原生 `0xCC` 设备标定和 `0xDD` Flash 数据读取状态机；
  - 背景、Gain、坏点和写入一致性汇总。
  - 实时诊断使用独立画布承载热图、关键测量和实时趋势三个组件，支持锁定、移动、八向缩放、隐藏、工作台内全屏、恢复默认模板和跨启动布局记忆。
- 串口独占：所有内置插件共享同一个 Core 租约表，避免两个工作区同时抢占相同端口。
- Windows 串口使用 Win32 API，Linux 使用 `termios2`，不依赖缺失的 Qt SerialPort 模块。

## Windows构建

当前脚本默认匹配本机工具链：

```text
C:\DOWNLOAD\Qt\6.11.1\mingw_64
C:\DOWNLOAD\Qt\Tools\mingw1310_64
C:\DOWNLOAD\Qt\Tools\CMake_64
C:\DOWNLOAD\Qt\Tools\Ninja
```

在 Windows PowerShell 运行唯一构建入口：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Serein_Y\Project\yserein-scope\tools\windows_user_action.ps1"
```

不要直接运行 `c++.exe`、`gcc.exe` 或测试 PE。构建脚本会先把 MinGW 和 Qt
运行时目录放到子进程 `PATH`，并固定 CMake 使用同一套编译器，避免
`libwinpthread-1.dll` 等运行时 DLL 缺失导致 Windows 弹窗。若候选程序仍在运行，
脚本只会向候选路径对应的进程发送一次正常关闭请求并等待，不会强制结束其他程序。

CMake 缓存和中间文件固定在 `build/windows-release/`。
构建和候选目录生命周期检查通过后，生成一套可直接运行的扁平交付：

```text
out/yserein-scope/
├── SereinScope.exe
├── libserein_scope_core.dll
├── Qt6*.dll / platforms/ / qml/ ...
├── Components/
│   ├── serial-scope/serial-scope.sscomponent
│   ├── tn160-thermal/tn160-thermal.sscomponent
│   ├── tn160-thermal-ncm/tn160-thermal-ncm.sscomponent
│   ├── t384/SereinScope.T384.sscomponent
│   ├── yserein-hmi/yserein-hmi.sscomponent
│   ├── yserein-swarm-sim/yserein-swarm-sim.sscomponent
│   └── yserein-universe/yserein-universe.sscomponent
└── SHA256SUMS.txt
```

`out/run-win/` 只保留当前已验收的运行基线，构建脚本不会覆盖它。

需要只编译一个组件时，在同一脚本后加 `-BuildOnly t384`（可替换为注册的组件
ID）；它只构建该 CMake 目标及依赖，不更新候选、组件 ZIP 或安装包。
`-Components t384` 则用于选择完整交付中包含的组件，仍执行全量编译。

同一脚本还会安装 Core SDK 到 `out/component-sdk/`，以独立模板构建检查
公开头文件、共享库、导入库和 CMake 包配置，随后生成 `out/component-sdk.zip`。
需要只产出 SDK 时加 `-SdkOnly`；它只构建 Core，校验解压后的 SDK 示例，
不更新候选、组件包或安装包。SDK 包内的 `SDK_INFO.json` 记录工具链与 ABI，
`SHA256SUMS.txt` 校验文件，ZIP 旁还有摘要文件。
外部组件从 SDK 包的 `examples/component_sdk/` 起步，使用
`SEREIN_SCOPE_SDK_PREFIX` 定位 SDK；详见 [SDK 模板说明](tools/component_sdk/README.md)。

## Linux开发构建

需要 Qt 6 Base 开发包，不需要 Qt SerialPort：

```bash
cmake -S . -B build/linux-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-debug --parallel
./build/linux-debug/bin/SereinScope
```

## 目录结构

```text
src/
├── core/                         # Packet、DataEngine、缓冲、原生串口、插件 ABI
├── app/                          # 插件仓库、生命周期、宿主、Dock和Serein主题
└── plugins/
    ├── serial_scope/             # 通用串口插件包
    ├── tn160/、tn160_ncm/        # TN160 组件
    ├── t384/、thermal_workbench/ # T256/T384/T640 与共用热成像工作台
    ├── yserein-hmi/              # 离线 HMI 设计器
    ├── yserein-swarm-sim/        # 编队模拟器
    └── yserein-universe/         # 轨道可视化流程
tools/
├── component_sdk/               # 第三方组件模板
└── windows_user_action.ps1      # Windows 构建与打包入口
```

## 当前边界

- `.sscomponent` 是原生动态库，拥有与宿主进程相同的系统权限；当前没有数字签名，只应导入可信组件。
- ABI 2 插件必须使用与宿主兼容的 Qt、编译器、CPU 架构和 Serein Core 构建；ABI 1 会被明确拒绝。
- Widgets 宿主通过公共 ABI 2 和可选 UI Provider 接入内置组件；QML 控制器仍有 Serial/TN160 的专用页面与控件名依赖，不能视为任意组件通用宿主。
- C++插件必须与宿主使用兼容的Qt、编译器和Serein插件ABI构建；`interfaceVersion`不匹配会拒绝加载。
- 固件、串口和标定流程仍需真实 Windows 设备验证；发布脚本保留组件生命周期 smoke 检查。
