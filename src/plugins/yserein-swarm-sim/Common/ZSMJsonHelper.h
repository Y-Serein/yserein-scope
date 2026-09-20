#pragma once

#include <QJsonObject>

namespace ZSMJsonHelper
{
	//TODO 改为 show.json, cues.json, trajectory.json, lights.json 都可以读的函数
	//TODO 现在都在调用这个函数
	/// @brief	将 show.json 文件转换为 QJsonObject
	/// @param filePath		文件的路径
	/// @param errorString	报错信息
	QJsonObject openShowJsonFile(const QString& filePath, QString& errorString);

}// namespace ZSMJsonHelper