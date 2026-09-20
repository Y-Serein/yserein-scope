#include "ZSMZipHelper.h"
#include "ZSMLoggingCategory.h"

#include <QtCore/QDir>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

ZSM_LOGGING_CATEGORY(ZSMZipHelperLog, "Common.ZSMZipHelper")

namespace ZSMZipHelper {

	bool zipDirectory(const QString& directoryPath, const QString& zipFilePath)
	{
        QDir dir(directoryPath);
        if (!dir.exists()) {
            zWarning(ZSMZipHelperLog) << "Directory does not exist:" << directoryPath;
            return false;
        }

        QFile zipFile(zipFilePath);
        if (!zipFile.open(QIODevice::WriteOnly)) {
            zWarning(ZSMZipHelperLog) << "Could not open zip file for writing:" << zipFilePath;
            return false;
        }

        QZipWriter zipWriter(&zipFile);
        zipWriter.setCompressionPolicy(QZipWriter::AutoCompress);

        QStringList fileList = dir.entryList(QDir::Files | QDir::NoDotAndDotDot | QDir::AllDirs, QDir::DirsFirst);

        for (const QString& fileName : fileList) {
            QString filePath = directoryPath + "/" + fileName;

            if (QFileInfo(filePath).isDir()) {
                zipWriter.addDirectory(fileName);
            }
            else {
                QFile file(filePath);
                if (file.open(QIODevice::ReadOnly)) {
                    zipWriter.addFile(fileName, file.readAll());
                    file.close();
                }
                else {
                    zWarning(ZSMZipHelperLog) << "Could not open file for reading:" << filePath;
                    return false;
                }
            }
        }

        zipWriter.close();
        return true;
	}

	bool unzipFile(const QString& zipFilePath, const QString& outputDirectoryPath)
	{
        QDir outputDir(outputDirectoryPath);
        if (!outputDir.exists()) {
            if (!outputDir.mkpath(outputDirectoryPath)) {
                zWarning(ZSMZipHelperLog) << "Failed to create output directory:" << outputDirectoryPath;
                return false;
            }
        }
        else {
            // 如果存在则清空
            QFileInfoList fileList = outputDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
            for (const QFileInfo& fileInfo : fileList) {
                if (fileInfo.isDir()) {
                    QDir(fileInfo.absoluteFilePath()).removeRecursively();
                }
                else {
                    QFile::remove(fileInfo.absoluteFilePath());
                }
            }
        }

        QFile zipFile(zipFilePath);
        if (!zipFile.open(QIODevice::ReadOnly)) {
            zWarning(ZSMZipHelperLog) << "Could not open zip file:" << zipFilePath;
            return false;
        }

        QZipReader zipReader(&zipFile);
        if (!zipReader.isReadable()) {
            zWarning(ZSMZipHelperLog) << "Could not read zip file:" << zipFilePath;
            return false;
        }

        const QList<QZipReader::FileInfo> allFiles = zipReader.fileInfoList();

        for (const QZipReader::FileInfo& fileInfo : allFiles) {
            QString filePath = outputDirectoryPath + "/" + fileInfo.filePath;

            if (fileInfo.isDir) {
                QDir().mkpath(filePath);
            }
            else if (fileInfo.isFile) {
                // 检查 dir
                QFileInfo _fileInfo(filePath);
                QDir dir = _fileInfo.dir();
                if (!dir.exists()) {
                    if (!dir.mkpath(dir.absolutePath())) {
                        zWarning(ZSMZipHelperLog) << "Failed to create output directory:" << outputDirectoryPath;
                        return false;
                    }
                }

                QFile outFile(filePath);

                   
                // 如果已存在文件自动覆盖
                if (outFile.open(QIODevice::WriteOnly)) {
                    outFile.write(zipReader.fileData(fileInfo.filePath));
                    outFile.close();
                }
                else {
                    zWarning(ZSMZipHelperLog) << "Could not open file for writing:" << filePath;
                    return false;
                }
            }
        }

        zipReader.close();
        return true;
	}
}// namespace ZSMZipHelper
