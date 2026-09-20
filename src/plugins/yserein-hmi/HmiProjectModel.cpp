#include "HmiProjectModel.h"

#include "Change.h"
#include "ControlSchema.h"

#include <QtCore/QFile>
#include <QtCore/QJsonParseError>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QUuid>

#include <cmath>
#include <algorithm>
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

// Applies one dotted field key to a control. A null `value` removes a
// properties/extra entry; the key is "name", "geometry.<axis>", "definitionId",
// "properties.<key>" or "extra.<key>".
bool setControlField(ControlModel &control, const QString &key,
                     const QJsonValue &value, QString *error)
{
    if (key == QStringLiteral("name")) {
        control.name = value.toString();
        return true;
    }
    if (key == QStringLiteral("geometry.x")) {
        control.geometry.x = value.toDouble();
        return true;
    }
    if (key == QStringLiteral("geometry.y")) {
        control.geometry.y = value.toDouble();
        return true;
    }
    if (key == QStringLiteral("geometry.width")) {
        control.geometry.width = value.toDouble();
        return true;
    }
    if (key == QStringLiteral("geometry.height")) {
        control.geometry.height = value.toDouble();
        return true;
    }
    if (key == QStringLiteral("definitionId")) {
        const QString newId = value.toString();
        control.definitionId = newId;
        const ControlSchema *schema =
            ControlSchemaRegistry::instance().findByDefinitionId(newId);
        control.type =
            schema ? schema->keyword : ControlSchemaRegistry::instance().fallback()->keyword;
        return true;
    }
    if (key.startsWith(QLatin1String("properties."))) {
        const QString propertyKey = key.mid(int(11));
        if (propertyKey.isEmpty()) {
            setError(error, QStringLiteral("属性键不能为空：%1").arg(key));
            return false;
        }
        if (value.isNull()) control.properties.remove(propertyKey);
        else control.properties.insert(propertyKey, value);
        return true;
    }
    if (key.startsWith(QLatin1String("extra."))) {
        const QString extraKey = key.mid(int(6));
        if (extraKey.isEmpty()) {
            setError(error, QStringLiteral("扩展键不能为空：%1").arg(key));
            return false;
        }
        if (value.isNull()) control.extra.remove(extraKey);
        else control.extra.insert(extraKey, value);
        return true;
    }
    setError(error, QStringLiteral("未知字段：%1").arg(key));
    return false;
}

// Migration step: schemaVersion 1 -> 2.
//
// Version 1 kept controls as opaque JSON arrays whose validity was enforced by
// a chain of hard-coded checks. Version 2 promotes each entry to a typed
// ControlModel, denormalizes the schema keyword (`type`) from the definition id,
// fills any missing authorable attributes with their schema defaults, and keeps
// the rest of the document intact.
std::optional<QJsonObject> migrateV1ToV2(const QJsonObject &v1, QString *error)
{
    const ControlSchemaRegistry &registry = ControlSchemaRegistry::instance();
    QJsonObject v2 = v1;
    v2.insert(QStringLiteral("schemaVersion"), int(2));

    const QJsonValue pagesValue = v1.value(QStringLiteral("pages"));
    if (!pagesValue.isArray()) {
        setError(error, QStringLiteral("迁移到 v2 失败：pages 必须是数组"));
        return std::nullopt;
    }

    QJsonArray migratedPages;
    const QJsonArray pages = pagesValue.toArray();
    for (int pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
        const QJsonValue pageValue = pages.at(pageIndex);
        if (!pageValue.isObject()) {
            setError(error, QStringLiteral("迁移到 v2 失败：页面 #%1 必须是对象").arg(pageIndex + 1));
            return std::nullopt;
        }
        QJsonObject page = pageValue.toObject();
        const QJsonValue controlsValue = page.value(QStringLiteral("controls"));
        if (!controlsValue.isArray()) {
            setError(error, QStringLiteral("迁移到 v2 失败：页面 #%1 的 controls 必须是数组")
                                .arg(pageIndex + 1));
            return std::nullopt;
        }
        QJsonArray migratedControls;
        const QJsonArray controls = controlsValue.toArray();
        for (int controlIndex = 0; controlIndex < controls.size(); ++controlIndex) {
            const QJsonValue controlValue = controls.at(controlIndex);
            if (!controlValue.isObject()) {
                setError(error, QStringLiteral("迁移到 v2 失败：控件 #%1 必须是对象")
                                    .arg(controlIndex + 1));
                return std::nullopt;
            }
            QJsonObject control = controlValue.toObject();
            const QString definitionId =
                control.value(QStringLiteral("definitionId")).toString().trimmed();
            const ControlSchema *schema = registry.findByDefinitionId(definitionId);
            if (!schema) schema = registry.fallback();

            control.insert(QStringLiteral("type"), schema->keyword);

            QJsonObject properties =
                control.value(QStringLiteral("properties")).toObject();
            for (auto it = schema->defaultProperties.constBegin();
                 it != schema->defaultProperties.constEnd(); ++it) {
                if (!properties.contains(it.key())) {
                    properties.insert(it.key(), it.value());
                }
            }
            control.insert(QStringLiteral("properties"), properties);
            migratedControls.append(control);
        }
        page.insert(QStringLiteral("controls"), migratedControls);
        migratedPages.append(page);
    }
    v2.insert(QStringLiteral("pages"), migratedPages);
    return v2;
}

std::optional<QJsonObject> migrateToCurrent(const QJsonObject &source, QString *error)
{
    const QJsonValue versionValue = source.value(QStringLiteral("schemaVersion"));
    if (!versionValue.isDouble() || !finiteNumber(versionValue)
        || versionValue.toDouble() != static_cast<double>(versionValue.toInt())
        || versionValue.toInt() <= 0) {
        setError(error, QStringLiteral("schemaVersion 必须是正整数"));
        return std::nullopt;
    }
    const int version = versionValue.toInt();
    if (version > HmiProjectModel::CurrentSchemaVersion) {
        setError(error, QStringLiteral("不支持的 schemaVersion：%1").arg(version));
        return std::nullopt;
    }
    if (version < HmiProjectModel::MinimumSupportedSchemaVersion) {
        setError(error, QStringLiteral("过旧的 schemaVersion：%1").arg(version));
        return std::nullopt;
    }
    if (version == HmiProjectModel::CurrentSchemaVersion) return source;

    QJsonObject current = source;
    if (version == 1) {
        auto migrated = migrateV1ToV2(current, error);
        if (!migrated) return std::nullopt;
        current = *migrated;
    }
    return current;
}

} // namespace


QJsonObject HmiProjectModel::Page::toJsonObject() const
{
    QJsonObject result = extra;
    result.insert(QStringLiteral("pageId"), pageId);
    result.insert(QStringLiteral("name"), name);
    QJsonArray controlsArray;
    for (const ControlModel &control : controls) {
        controlsArray.append(control.toJsonObject());
    }
    result.insert(QStringLiteral("controls"), controlsArray);
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
    QSet<QString> names;
    for (const ControlModel &control : controls) {
        if (ids.contains(control.instanceId)) {
            setError(error, QStringLiteral("页面内控件 instanceId 重复：%1")
                                .arg(control.instanceId));
            return false;
        }
        ids.insert(control.instanceId);
        if (names.contains(control.name)) {
            setError(error, QStringLiteral("页面内控件名称重复：%1").arg(control.name));
            return false;
        }
        names.insert(control.name);
        if (!control.validate(nullptr, error)) return false;
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
    const QJsonValue controlsValue = object.value(QStringLiteral("controls"));
    if (!controlsValue.isArray()) {
        setError(error, QStringLiteral("页面 %1 的 controls 必须是数组").arg(page.pageId));
        return std::nullopt;
    }
    for (const QJsonValue &value : controlsValue.toArray()) {
        if (!value.isObject()) {
            setError(error, QStringLiteral("页面 %1 的控件必须是 JSON 对象").arg(page.pageId));
            return std::nullopt;
        }
        auto control = ControlModel::fromJsonObject(value.toObject(), nullptr, error);
        if (!control) {
            if (error && !error->isEmpty()) *error = QStringLiteral("控件：%1").arg(*error);
            return std::nullopt;
        }
        page.controls.append(std::move(*control));
    }
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

QJsonObject HmiProjectModel::Asset::toJsonObject() const
{
    QJsonObject result = extra;
    result.insert(QStringLiteral("assetId"), assetId);
    result.insert(QStringLiteral("kind"), kind);
    result.insert(QStringLiteral("source"), source);
    return result;
}

bool HmiProjectModel::Asset::validate(QString *error) const
{
    const QString trimmedId = assetId.trimmed();
    if (!validStableId(trimmedId)) {
        setError(error, QStringLiteral("资源 assetId 无效：%1").arg(assetId));
        return false;
    }
    if (kind.trimmed().isEmpty()) {
        setError(error, QStringLiteral("资源类型不能为空：%1").arg(trimmedId));
        return false;
    }
    if (source.trimmed().isEmpty()) {
        setError(error, QStringLiteral("资源源路径不能为空：%1").arg(assetId));
        return false;
    }
    return true;
}

std::optional<HmiProjectModel::Asset> HmiProjectModel::Asset::fromJsonObject(
    const QJsonObject &object, QString *error)
{
    Asset asset;
    if (!nonEmptyString(object, QStringLiteral("assetId"), &asset.assetId, error)
        || !nonEmptyString(object, QStringLiteral("kind"), &asset.kind, error)
        || !nonEmptyString(object, QStringLiteral("source"), &asset.source, error)) {
        return std::nullopt;
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() != QStringLiteral("assetId")
            && it.key() != QStringLiteral("kind")
            && it.key() != QStringLiteral("source")) {
            asset.extra.insert(it.key(), it.value());
        }
    }
    if (!asset.validate(error)) return std::nullopt;
    return asset;
}

HmiProjectModel HmiProjectModel::createDefault(const QString &targetProfile,
                                               const QString &projectId)
{
    HmiProjectModel model;
    model.m_schemaVersion = CurrentSchemaVersion;
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
    if (!m_assets.isEmpty()) {
        QJsonArray assetArray;
        for (const Asset &asset : m_assets) assetArray.append(asset.toJsonObject());
        result.insert(QStringLiteral("assets"), assetArray);
    }
    return result;
}

QJsonDocument HmiProjectModel::toJsonDocument() const
{
    return QJsonDocument(toJsonObject());
}

std::optional<HmiProjectModel> HmiProjectModel::fromJsonObject(
    const QJsonObject &object, QString *error)
{
    const auto current = migrateToCurrent(object, error);
    if (!current) return std::nullopt;

    HmiProjectModel model;
    model.m_schemaVersion = CurrentSchemaVersion;
    if (!nonEmptyString(*current, QStringLiteral("projectId"), &model.m_projectId, error)
        || !nonEmptyString(*current, QStringLiteral("targetProfile"), &model.m_targetProfile,
                           error)) {
        return std::nullopt;
    }
    if (!validStableId(model.m_projectId)) {
        setError(error, QStringLiteral("projectId 含非法字符：%1").arg(model.m_projectId));
        return std::nullopt;
    }
    const QJsonValue pages = current->value(QStringLiteral("pages"));
    if (!pages.isArray()) {
        setError(error, QStringLiteral("pages 必须是数组"));
        return std::nullopt;
    }
    const QJsonArray pageArray = pages.toArray();
    if (pageArray.isEmpty()) {
        setError(error, QStringLiteral("工程至少需要一个页面"));
        return std::nullopt;
    }
    QSet<QString> pageIds;
    QSet<QString> pageNames;
    for (int index = 0; index < pageArray.size(); ++index) {
        if (!pageArray.at(index).isObject()) {
            setError(error, QStringLiteral("页面 #%1 必须是 JSON 对象").arg(index + 1));
            return std::nullopt;
        }
        auto page = Page::fromJsonObject(pageArray.at(index).toObject(), error);
        if (!page) {
            if (error && !error->isEmpty()) *error =
                QStringLiteral("页面 #%1：%2").arg(index + 1).arg(*error);
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
        model.m_pages.append(std::move(*page));
    }
    const QJsonValue assetsValue = current->value(QStringLiteral("assets"));
    if (!assetsValue.isUndefined() && !assetsValue.isArray()) {
        setError(error, QStringLiteral("assets 必须是数组"));
        return std::nullopt;
    }
    for (const QJsonValue &value : assetsValue.toArray()) {
        if (!value.isObject()) {
            setError(error, QStringLiteral("资源记录必须是对象"));
            return std::nullopt;
        }
        auto asset = Asset::fromJsonObject(value.toObject(), error);
        if (!asset) {
            if (error && !error->isEmpty()) *error = QStringLiteral("资源记录：%1").arg(*error);
            return std::nullopt;
        }
        model.m_assets.append(std::move(*asset));
    }
    for (auto it = current->begin(); it != current->end(); ++it) {
        if (it.key() != QStringLiteral("schemaVersion")
            && it.key() != QStringLiteral("projectId")
            && it.key() != QStringLiteral("targetProfile")
            && it.key() != QStringLiteral("pages")
            && it.key() != QStringLiteral("assets")) {
            model.m_extra.insert(it.key(), it.value());
        }
    }
    if (!model.validate(error)) return std::nullopt;
    return model;
}

bool HmiProjectModel::setJsonObject(const QJsonObject &object, QString *error)
{
    auto parsed = fromJsonObject(object, error);
    if (!parsed) return false;
    *this = std::move(*parsed);
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

std::optional<QJsonObject> HmiProjectModel::migrateToCurrentVersion(
    const QJsonObject &document, QString *error)
{
    return migrateToCurrent(document, error);
}

bool HmiProjectModel::removeAsset(const QString &assetId)
{
    const QString target = assetId.trimmed();
    for (qsizetype index = 0; index < m_assets.size(); ++index) {
        if (m_assets.at(index).assetId.trimmed() == target) {
            m_assets.removeAt(index);
            return true;
        }
    }
    return false;
}

bool HmiProjectModel::hasAsset(const QString &assetId) const
{
    const QString target = assetId.trimmed();
    return std::any_of(m_assets.cbegin(), m_assets.cend(),
                       [&target](const Asset &asset) {
                           return asset.assetId.trimmed() == target;
                       });
}

const HmiProjectModel::Asset *HmiProjectModel::findAsset(const QString &assetId) const
{
    const QString target = assetId.trimmed();
    for (const Asset &asset : m_assets) {
        if (asset.assetId.trimmed() == target) return &asset;
    }
    return nullptr;
}

bool HmiProjectModel::validate(QString *error) const
{
    if (m_schemaVersion < MinimumSupportedSchemaVersion
        || m_schemaVersion > CurrentSchemaVersion) {
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
        const QString pageId = page.pageId.trimmed();
        if (pageIds.contains(pageId)) {
            setError(error, QStringLiteral("页面 pageId 重复：%1").arg(pageId));
            return false;
        }
        const QString pageName = page.name.trimmed().toCaseFolded();
        if (pageNames.contains(pageName)) {
            setError(error, QStringLiteral("页面名称重复：%1").arg(page.name));
            return false;
        }
        pageIds.insert(pageId);
        pageNames.insert(pageName);
        for (const ControlModel &control : page.controls) {
            if (controlIds.contains(control.instanceId)) {
                setError(error, QStringLiteral("工程内控件 instanceId 重复：%1")
                                    .arg(control.instanceId));
                return false;
            }
            controlIds.insert(control.instanceId);
        }
    }
    QHash<QString, QString> assetKinds;
    QSet<QString> assetIds;
    for (const Asset &asset : m_assets) {
        if (!asset.validate(error)) return false;
        const QString id = asset.assetId.trimmed();
        if (assetIds.contains(id)) {
            setError(error, QStringLiteral("资源 assetId 重复：%1").arg(id));
            return false;
        }
        assetIds.insert(id);
        assetKinds.insert(id, asset.kind.trimmed());
    }
    for (const Page &page : m_pages) {
        for (const ControlModel &control : page.controls) {
            const QString definitionId = control.definitionId;
            const ControlSchema *schema =
                ControlSchemaRegistry::instance().findByDefinitionId(definitionId);
            // 资源登记（assets[]）尚未接入 UI，按兼容策略只在模型确实登记了资源时
            // 强制图片引用可解析；旧工程没有 assets[] 时仍允许带 assetId 的图片控件保存。
            if (schema && schema->keyword == QStringLiteral("image")
                && !assetKinds.isEmpty()) {
                const QString assetId =
                    control.properties.value(QStringLiteral("assetId")).toString().trimmed();
                if (assetId.isEmpty() || assetKinds.value(assetId) != QStringLiteral("image")) {
                    setError(error, QStringLiteral("图片控件引用了未登记的图片资源：%1")
                                        .arg(assetId));
                    return false;
                }
            }
            if (control.eventType() == QStringLiteral("gotoPage")) {
                const QString target = control.eventTarget().trimmed();
                if (!pageIds.contains(target)) {
                    setError(error, QStringLiteral("页面跳转目标不存在：%1").arg(target));
                    return false;
                }
            }
        }
    }
    return true;
}

HmiProjectDocument HmiProjectDocument::createDefault(const QString &targetProfile,
                                                     const QString &projectId)
{
    return HmiProjectDocument(HmiProjectModel::createDefault(targetProfile, projectId));
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
    auto loaded = fromFile(filePath, error);
    if (!loaded) return false;
    *this = std::move(*loaded);
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
    return true;
}

bool HmiProjectDocument::saveToFileAtomic(QString *error)
{
    if (m_filePath.trimmed().isEmpty()) {
        setError(error, QStringLiteral("尚未指定工程文件路径"));
        return false;
    }
    return saveToFileAtomic(m_filePath, error);
}

std::optional<HmiProjectDocument> HmiProjectDocument::fromFile(
    const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法打开工程文件：%1").arg(file.errorString()));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("工程 JSON 无效：%1").arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!json.isObject()) {
        setError(error, QStringLiteral("工程文档根节点必须是 JSON 对象"));
        return std::nullopt;
    }
    auto model = HmiProjectModel::fromJsonObject(json.object(), error);
    if (!model) return std::nullopt;

    HmiProjectDocument document;
    document.m_model = std::move(*model);
    document.m_filePath = filePath;
    return document;
}

const HmiProjectModel::Page *HmiProjectModel::findPage(const QString &pageId) const
{
    const QString target = pageId.trimmed();
    for (const Page &page : m_pages)
        if (page.pageId.trimmed() == target) return &page;
    return nullptr;
}

const ControlModel *HmiProjectModel::findControl(const QString &pageId,
                                                 const QString &instanceId) const
{
    const Page *page = findPage(pageId);
    if (!page) return nullptr;
    const QString target = instanceId.trimmed();
    for (const ControlModel &control : page->controls)
        if (control.instanceId.trimmed() == target) return &control;
    return nullptr;
}

HmiProjectModel::Page *HmiProjectModel::findPageMut(const QString &pageId)
{
    const QString target = pageId.trimmed();
    for (Page &page : m_pages)
        if (page.pageId.trimmed() == target) return &page;
    return nullptr;
}

ControlModel *HmiProjectModel::findControlMut(const QString &pageId,
                                              const QString &instanceId)
{
    Page *page = findPageMut(pageId);
    if (!page) return nullptr;
    const QString target = instanceId.trimmed();
    for (ControlModel &control : page->controls)
        if (control.instanceId.trimmed() == target) return &control;
    return nullptr;
}

bool HmiProjectModel::applyChange(const Change &change, QString *error)
{
    HmiProjectModel staged = *this;
    for (const ChangeOp &op : change.ops) {
        const bool ok = std::visit([&staged, error](const auto &o) {
            return staged.applyOne(o, error);
        }, op);
        if (!ok) return false;
    }
    if (!staged.validate(error)) return false;
    *this = std::move(staged);
    return true;
}

bool HmiProjectModel::applyOne(const ControlAdds &op, QString *error)
{
    Page *page = findPageMut(op.pageId);
    if (!page) {
        setError(error, QStringLiteral("插入控件失败：页面不存在：%1").arg(op.pageId));
        return false;
    }
    const int index = op.index < 0 || op.index > page->controls.size()
                          ? page->controls.size()
                          : op.index;
    page->controls.insert(index, op.control);
    return true;
}

bool HmiProjectModel::applyOne(const ControlRemoves &op, QString *error)
{
    Page *page = findPageMut(op.pageId);
    if (!page) {
        setError(error, QStringLiteral("删除控件失败：页面不存在：%1").arg(op.pageId));
        return false;
    }
    const QString target = op.instanceId.trimmed();
    for (qsizetype i = 0; i < page->controls.size(); ++i) {
        if (page->controls.at(i).instanceId.trimmed() == target) {
            page->controls.removeAt(i);
            return true;
        }
    }
    setError(error, QStringLiteral("删除控件失败：控件不存在：%1").arg(op.instanceId));
    return false;
}

bool HmiProjectModel::applyOne(const ControlUpdates &op, QString *error)
{
    ControlModel *control = findControlMut(op.pageId, op.instanceId);
    if (!control) {
        setError(error, QStringLiteral("更新控件失败：控件不存在：%1").arg(op.instanceId));
        return false;
    }
    for (const FieldChange &field : op.fields) {
        if (!setControlField(*control, field.key, field.newValue, error)) return false;
    }
    return true;
}

bool HmiProjectModel::applyOne(const ControlMoves &op, QString *error)
{
    Page *page = findPageMut(op.pageId);
    if (!page) {
        setError(error, QStringLiteral("移动控件失败：页面不存在：%1").arg(op.pageId));
        return false;
    }
    const QString target = op.instanceId.trimmed();
    int oldIndex = op.oldIndex;
    if (oldIndex < 0 || oldIndex >= page->controls.size()) {
        oldIndex = -1;
        for (qsizetype i = 0; i < page->controls.size(); ++i) {
            if (page->controls.at(i).instanceId.trimmed() == target) {
                oldIndex = int(i);
                break;
            }
        }
        if (oldIndex < 0) {
            setError(error, QStringLiteral("移动控件失败：控件不存在：%1").arg(op.instanceId));
            return false;
        }
    }
    ControlModel control = page->controls.takeAt(oldIndex);
    int newIndex = op.newIndex;
    if (newIndex < 0) newIndex = 0;
    if (newIndex > page->controls.size()) newIndex = page->controls.size();
    page->controls.insert(newIndex, std::move(control));
    return true;
}

bool HmiProjectModel::applyOne(const PageAdds &op, QString *error)
{
    Q_UNUSED(error);
    const int index = op.index < 0 || op.index > m_pages.size() ? m_pages.size() : op.index;
    m_pages.insert(index, op.page);
    return true;
}

bool HmiProjectModel::applyOne(const PageRemoves &op, QString *error)
{
    const QString target = op.pageId.trimmed();
    for (qsizetype i = 0; i < m_pages.size(); ++i) {
        if (m_pages.at(i).pageId.trimmed() == target) {
            m_pages.removeAt(i);
            return true;
        }
    }
    setError(error, QStringLiteral("删除页面失败：页面不存在：%1").arg(op.pageId));
    return false;
}

bool HmiProjectModel::applyOne(const PageRenames &op, QString *error)
{
    Page *page = findPageMut(op.pageId);
    if (!page) {
        setError(error, QStringLiteral("重命名页面失败：页面不存在：%1").arg(op.pageId));
        return false;
    }
    if (op.newName.trimmed().isEmpty()) {
        setError(error, QStringLiteral("重命名页面失败：名称不能为空"));
        return false;
    }
    page->name = op.newName;
    return true;
}

} // namespace serein::scope::yserein_hmi
