# Swarm 面板回归

`prepare_fixture.py [output-directory]` 从当前生产 QML 生成离屏夹具，默认只写 `build/swarm-panel-qml-tests/`。用安装的 Qt 6 `qmltestrunner -input <output-directory> -import <output-directory>` 运行，环境 `QT_QPA_PLATFORM=offscreen`。本轮使用 Qt 6.11.1 / MinGW。

覆盖箱式/导入来源的就绪、播放、暂停、缺失和清除状态，以及220/400px宽度、360/780px高度、列表/详情不重叠、输入控件水平边界和详情滚动。控制器、Theme、Styles、ZButton用隔离实现，不发送网络数据，不代表实际后台/宿主集成通过。原生 Windows 样式在offscreen下有OpenThemeData句柄提示，不隐藏日志。

本轮命令与结果保存在相邻 yserein-swarm 的 `out/test-run/2026-10-12-scope-panel-fix/`。未修改CMake注册或增加生产依赖。
