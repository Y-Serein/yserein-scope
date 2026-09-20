#pragma once

#include <QString>

namespace ZSMZipHelper {
    /// @brief  压缩文件夹为 .zip 文件
    /// @param directoryPath    需要压缩的文件夹
    /// @param zipFilePath      压缩后的文件文件存放位置
    bool zipDirectory(const QString& directoryPath, const QString& zipFilePath);

    /// @brief 解压一个 .zip 文件到指定文件夹
    /// @param zipFilePath          待压缩文件路径
    /// @param outputDirectoryPath  解压后的文件夹
    bool unzipFile(const QString& zipFilePath, const QString& outputDirectoryPath);
} // namespace ZSMZipHelper
