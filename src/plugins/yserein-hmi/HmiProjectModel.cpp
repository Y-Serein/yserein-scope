#include "HmiProjectModel.h"

#include <QtCore/QFile>
#include <QtCore/QJsonParseError>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QUuid>

#include <cmath>
#include <utility>

namespace serein::scope::yserein_hmi {
namespace {

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

bool nonEmptyString(const QJsonObject &object, const QString &key,
                   QString *value, QString *error)
{
    const QJsonValue candidate = object.value(key);
    if (!candidate.isString()) {
        setError(error, QStringLiteral("字段 %1 必须是字符串").arg(key));
        return false;
    }
    const QString text = candidate.toString().trimmed();
    if (text.isEmpty()) {
        setError(error, QStringLiteral("字段 %1 不能为空").arg(key));
        return false;
    }
    if (value) *value = text;
    return true;
}

bool validStableId(const QString &value)
{
    if (value.isEmpty()) return false;
    for (const QChar character : value) {
        if (!(character.isLetterOrNumber() || character == QLatin1Char('_')
              || character == QLatin1Char('-') || character == QLatin1Char('.'))) {
            return false;
        }
    }
    return true;
}

bool finiteNumber(const QJsonValue &value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}

bool validateControl(const QJsonValue &value, QSet<QString> *ids,
                     QString *error, int index)
{
    if (!value.isObject()) {
        setError(error, QStringLiteral("控件 #%1 必须是 JSON 对象").arg(index + 1));
        return false;
    }
    const QJsonObject control = value.toObject();
    QString instanceId;
    if (!nonEmptyString(control, QStringLiteral("instanceId"), &instanceId,
                        error)) {
        setError(error, QStringLiteral("控件 #%1 的 instanceId 无效：%2")
                              .arg(index + 1)
                              .arg(error ? *error : QString()));
        return false;
    }
    if (!validStableId(instanceId)) {
        setError(error, QStringLiteral("控件 #%1 的 instanceId 含非法字符：%2")
                              .arg(index + 1)
                              .arg(instanceId));
        return false;
    }
    if (ids && ids->contains(instanceId)) {
        setError(error, QStringLiteral("页面内控件 instanceId 重复：%1").arg(instanceId));
        return false;
    }
    if (ids) ids->insert(instanceId);

    QString definitionId;
    if (!nonEmptyString(control, QStringLiteral("definitionId"), &definitionId,
                        error)) {
        setError(error, QStringLiteral("控件 #%1 的 definitionId 无效：%2")
                              .arg(index + 1)
                              .arg(error ? *error : QString()));
        return false;
    }
    if (!validStableId(definitionId)) {
        setError(error, QStringLiteral("控件 #%1 的 definitionId 含非法字符：%2")
                              .arg(index + 1)
                              .arg(definitionId));
        return false;
    }

    const QJsonValue geometry = control.value(QStringLiteral("geometry"));
    if (!geometry.isUndefined()) {
        if (!geometry.isObject()) {
            setError(error, QStringLiteral("控件 #%1 的 geometry 必须是对象")
                                  .arg(index + 1));
            return false;
        }
        const QJsonObject rect = geometry.toObject();
        for (const QString &key : {QStringLiteral("x"), QStringLiteral("y"),
                                    QStringLiteral("width"), QStringLiteral("height")}) {
            if (!finiteNumber(rect.value(key))) {
                setError(error, QStringLiteral("控件 #%1 的 geometry.%2 必须是有限数值")
                                      .arg(index + 1)
                                      .arg(key));
                return false;
            }
        }
        if (rect.value(QStringLiteral("width")).toDouble() <= 0.0
            || rect.value(QStringLiteral("height")).toDouble() <= 0.0) {
            setError(error, QStringLiteral("控件 #%1 的 geometry 尺寸必须大于 0")
                                  .arg(index + 1));
            return false;
        }
    }
    return true;
}

} // namespace

QJsonObject HmiProjectModel::Page::toJsonObject() const
{
    QJsonObject result = extra;
    result.insert(QStringLiteral("pageId"), pageId);
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("controls"), controls);
    return result;
}

bool HmiProjectModel::Page::validate(QString *error) const
{
    const QString trimmedId = pageId.trimmed();
    if (!validStableId(trimmedId)) {
        setError(error, QStringLiteral("页面 pageId 无效：%1").arg(pageId));
        return false;
    }
    if (name.trimmed().isEmpty()) {
        setError(error, QStringLiteral("页面名称不能为空：%1").arg(trimmedId));
        return false;
    }
    QSet<QString> ids;
    for (int index = 0; index < controls.size(); ++index) {
        if (!validateControl(controls.at(index), &ids, error, index)) return false;
    }
    return true;
}

std::optional<HmiProjectModel::Page> HmiProjectModel::Page::fromJsonObject(
    const QJsonObject &object, QString *error)
{
    Page page;
    if (!nonEmptyString(object, QStringLiteral("pageId"), &page.pageId, error)
        || !nonEmptyString(object, QStringLiteral("name"), &page.name, error)) {
        return std::nullopt;
    }
    const QJsonValue controls = object.value(QStringLiteral("controls"));
    if (!controls.isArray()) {
        setError(error, QStringLiteral("页面 %1 的 controls 必须是数组").arg(page.pageId));
        return std::nullopt;
    }
    page.controls = controls.toArray();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() != QStringLiteral("pageId")
            && it.key() != QStringLiteral("name")
            && it.key() != QStringLiteral("controls")) {
            page.extra.insert(it.key(), it.value());
        }
    }
    if (!page.validate(error)) return std::nullopt;
    return page;
}

HmiProjectModel HmiProjectModel::createDefault(const QString &targetProfile,
                                               const QString &projectId)
{
    HmiProjectModel model;
    model.m_projectId = projectId.trimmed().isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : projectId.trimmed();
    model.m_targetProfile = targetProfile.trimmed().isEmpty()
        ? QStringLiteral("generic")
        : targetProfile.trimmed();
    Page page;
    page.pageId = QStringLiteral("page_1");
    page.name = QStringLiteral("Page 1");
    model.m_pages.append(std::move(page));
    return model;
}

QJsonObject HmiProjectModel::toJsonObject() const
{
    QJsonObject result = m_extra;
    result.insert(QStringLiteral("schemaVersion"), m_schemaVersion);
    result.insert(QStringLiteral("projectId"), m_projectId);
    result.insert(QStringLiteral("targetProfile"), m_targetProfile);
    QJsonArray pageArray;
    for (const Page &page : m_pages) pageArray.append(page.toJsonObject());
    result.insert(QStringLiteral("pages"), pageArray);
    return result;
}

QJsonDocument HmiProjectModel::toJsonDocument() const
{
    return QJsonDocument(toJsonObject());
}

std::optional<HmiProjectModel> HmiProjectModel::fromJsonObject(
    const QJsonObject &object, QString *error)
{
    HmiProjectModel model;
    const QJsonValue schema = object.value(QStringLiteral("schemaVersion"));
    if (!schema.isDouble() || schema.toInt() <= 0
        || schema.toDouble() != static_cast<double>(schema.toInt())) {
        setError(error, QStringLiteral("schemaVersion 必须是正整数"));
        return std::nullopt;
    }
    model.m_schemaVersion = schema.toInt();
    if (model.m_schemaVersion > CurrentSchemaVersion) {
        setError(error, QStringLiteral("不支持的 schemaVersion：%1")
                              .arg(model.m_schemaVersion));
        return std::nullopt;
    }
    if (!nonEmptyString(object, QStringLiteral("projectId"), &model.m_projectId,
                        error)
        || !nonEmptyString(object, QStringLiteral("targetProfile"),
                           &model.m_targetProfile, error)) {
        return std::nullopt;
    }
    if (!validStableId(model.m_projectId)) {
        setError(error, QStringLiteral("projectId 含非法字符：%1").arg(model.m_projectId));
        return std::nullopt;
    }
    const QJsonValue pages = object.value(QStringLiteral("pages"));
    if (!pages.isArray()) {
        setError(error, QStringLiteral("pages 必须是数组"));
        return std::nullopt;
    }
    if (pages.toArray().isEmpty()) {
        setError(error, QStringLiteral("工程至少需要一个页面"));
        return std::nullopt;
    }
    QSet<QString> pageIds;
    QSet<QString> pageNames;
    const QJsonArray pageArray = pages.toArray();
    for (int index = 0; index < pageArray.size(); ++index) {
        if (!pageArray.at(index).isObject()) {
            setError(error, QStringLiteral("页面 #%1 必须是 JSON 对象").arg(index + 1));
            return std::nullopt;
        }
        const auto page = Page::fromJsonObject(pageArray.at(index).toObject(), error);
        if (!page) {
            if (error && !error->isEmpty()) *error = QStringLiteral("页面 #%1：%2")
                .arg(index + 1).arg(*error);
            return std::nullopt;
        }
        if (pageIds.contains(page->pageId)) {
            setError(error, QStringLiteral("页面 pageId 重复：%1").arg(page->pageId));
            return std::nullopt;
        }
        const QString pageName = page->name.trimmed().toCaseFolded();
        if (pageNames.contains(pageName)) {
            setError(error, QStringLiteral("页面名称重复：%1").arg(page->name));
            return std::nullopt;
        }
        pageIds.insert(page->pageId);
        pageNames.insert(pageName);
        model.m_pages.append(*page);
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() != QStringLiteral("schemaVersion")
            && it.key() != QStringLiteral("projectId")
            && it.key() != QStringLiteral("targetProfile")
            && it.key() != QStringLiteral("pages")) {
            model.m_extra.insert(it.key(), it.value());
        }
    }
    if (!model.validate(error)) return std::nullopt;
    return model;
}

bool HmiProjectModel::setJsonObject(const QJsonObject &object, QString *error)
{
    const auto parsed = fromJsonObject(object, error);
    if (!parsed) return false;
    *this = *parsed;
    return true;
}

bool HmiProjectModel::setJsonDocument(const QJsonDocument &document,
                                      QString *error)
{
    if (!document.isObject()) {
        setError(error, QStringLiteral("工程文档根节点必须是 JSON 对象"));
        return false;
    }
    return setJsonObject(document.object(), error);
}

bool HmiProjectModel::validate(QString *error) const
{
    if (m_schemaVersion <= 0 || m_schemaVersion > CurrentSchemaVersion) {
        setError(error, QStringLiteral("不支持的 schemaVersion：%1").arg(m_schemaVersion));
        return false;
    }
    if (!validStableId(m_projectId.trimmed())) {
        setError(error, QStringLiteral("projectId 无效：%1").arg(m_projectId));
        return false;
    }
    if (m_targetProfile.trimmed().isEmpty()) {
        setError(error, QStringLiteral("targetProfile 不能为空"));
        return false;
    }
    QSet<QString> pageIds;
    QSet<QString> pageNames;
    QSet<QString> controlIds;
    if (m_pages.isEmpty()) {
        setError(error, QStringLiteral("工程至少需要一个页面"));
        return false;
    }
    for (const Page &page : m_pages) {
        if (!page.validate(error)) return false;
        if (pageIds.contains(page.pageId.trimmed())) {
            setError(error, QStringLiteral("页面 pageId 重复：%1").arg(page.pageId));
            return false;
        }
        const QString pageName = page.name.trimmed().toCaseFolded();
        if (pageNames.contains(pageName)) {
            setError(error, QStringLiteral("页面名称重复：%1").arg(page.name));
            return false;
        }
        pageIds.insert(page.pageId.trimmed());
        pageNames.insert(pageName);
        for (const QJsonValue &controlValue : page.controls) {
            const QString instanceId = controlValue.toObject()
                .value(QStringLiteral("instanceId")).toString().trimmed();
            if (controlIds.contains(instanceId)) {
                setError(error, QStringLiteral("工程内控件 instanceId 重复：%1")
                                      .arg(instanceId));
                return false;
            }
            controlIds.insert(instanceId);
        }
    }
    return true;
}

HmiProjectDocument HmiProjectDocument::createDefault(const QString &targetProfile,
                                                     const QString &projectId)
{
    return HmiProjectDocument(HmiProjectModel::createDefault(targetProfile,
                                                              projectId));
}

bool HmiProjectDocument::setJsonObject(const QJsonObject &object, QString *error)
{
    return m_model.setJsonObject(object, error);
}

bool HmiProjectDocument::setJsonDocument(const QJsonDocument &document,
                                         QString *error)
{
    return m_model.setJsonDocument(document, error);
}

bool HmiProjectDocument::loadFromFile(const QString &filePath, QString *error)
{
    const auto loaded = fromFile(filePath, error);
    if (!loaded) return false;
    m_model = loaded->m_model;
    m_filePath = loaded->m_filePath;
    return true;
}

bool HmiProjectDocument::saveToFileAtomic(const QString &filePath, QString *error)
{
    const QString path = filePath.trimmed();
    if (path.isEmpty()) {
        setError(error, QStringLiteral("工程文件路径不能为空"));
        return false;
    }
    if (!m_model.validate(error)) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("无法写入工程文件：%1").arg(file.errorString()));
        return false;
    }
    const QByteArray bytes = m_model.toJsonDocument().toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        setError(error, QStringLiteral("写入工程文件失败：%1").arg(file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("原子替换工程文件失败：%1").arg(file.errorString()));
        return false;
    }
    m_filePath = path;
    return true;
}

bool HmiProjectDocument::saveToFileAtomic(QString *error)
{
    return saveToFileAtomic(m_filePath, error);
}

std::optional<HmiProjectDocument> HmiProjectDocument::fromFile(
    const QString &filePath, QString *error)
{
    const QString path = filePath.trimmed();
    if (path.isEmpty()) {
        setError(error, QStringLiteral("工程文件路径不能为空"));
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法读取工程文件：%1").arg(file.errorString()));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("工程 JSON 无效：%1").arg(parseError.errorString()));
        return std::nullopt;
    }
    HmiProjectDocument result;
    if (!result.setJsonDocument(json, error)) return std::nullopt;
    result.m_filePath = path;
    return result;
}

} // namespace serein::scope::yserein_hmi
