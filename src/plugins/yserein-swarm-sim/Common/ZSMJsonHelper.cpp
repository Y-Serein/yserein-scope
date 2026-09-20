#include "ZSMJsonHelper.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QObject>

QJsonObject ZSMJsonHelper::openShowJsonFile(const QString& filePath, QString& errorString)
{
    // 读取文件
    QFile jsonFile(filePath);

    errorString.clear();

    if (!jsonFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorString = QObject::tr("打开舞步 json 文件失败");
        return QJsonObject();
    }

    QByteArray bytes = jsonFile.readAll();
    jsonFile.close();
    QJsonParseError jsonParseError;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &jsonParseError);

    // 文件读取错误
    if (jsonParseError.error != QJsonParseError::NoError) {
        errorString = QObject::tr("无法解析 json 文件: %1 error: %2 offset: %3").arg(filePath).arg(jsonParseError.errorString()).arg(jsonParseError.offset);
        return QJsonObject();
    }

    if (!doc.isObject()) {
        errorString = QObject::tr("json 文件的根不是对象 :%1").arg(filePath);
        return QJsonObject();
    }

    // 文件解析
    QJsonObject jsonObject = doc.object();
    // 验证文件类型
    //TODO 加上对 show.json, cue.json, drone.json 格式的验证
    // bool success =

    return jsonObject;
}
