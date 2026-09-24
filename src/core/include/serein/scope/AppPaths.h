#pragma once

#include "CoreExport.h"

#include <QString>

namespace serein::scope {

// 应用存储路径统一入口。
//
// 本仓库按“便携式安装、自选目录”定位运行：配置、日志、缓存、组件仓库、
// 插件数据全部以 exe 所在目录为根，不写入用户 AppData / Program Files，
// 安装到 D 盘自定义目录后普通权限即可读写，且整个目录可整体迁移。
//
// 目录布局：
//   <exe目录>/
//     SereinScope.exe
//     Components/           组件仓库（与打包组件同一处）
//     runtime/
//       config/             QSettings .ini
//       logs/               日志（预留，当前无文件日志）
//       cache/              缓存（预留，当前无磁盘缓存）
//       plugins/            各插件运行时数据
class SEREIN_SCOPE_CORE_EXPORT AppPaths {
public:
    // exe 所在目录（绝对路径，不含尾部分隔符）
    static QString appDir();

    // 运行时数据根目录：<appDir>/runtime
    static QString dataRoot();

    // 配置目录：<appDir>/runtime/config
    static QString configDir();

    // 日志目录：<appDir>/runtime/logs
    static QString logDir();

    // 缓存目录：<appDir>/runtime/cache
    static QString cacheDir();

    // 组件仓库目录：<appDir>/Components（与打包组件同一处）
    static QString componentsDir();

    // 插件运行时数据目录：<appDir>/runtime/plugins
    static QString pluginDataDir();
};

} // namespace serein::scope
