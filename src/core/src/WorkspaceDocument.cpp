#include <serein/scope/WorkspaceDocument.h>

#include <QtCore/QFile>
#include <QtCore/QJsonParseError>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>

#include <cmath>
#include <utility>

namespace serein::scope {
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

bool validId(const QString &value)
{
    if (value.trimmed().isEmpty()) return false;
    for (const QChar character : value) {
        if (!(character.isLetterOrNumber() || character == QLatin1Char('_')
              || character == QLatin1Char('-') || character == QLatin1Char('.')
              || character == QLatin1Char('/'))) {
            return false;
        }
    }
    return true;
}

bool finiteNumber(const QJsonValue &value)
{
    return value.isDouble() && std::isfinite(value.toDouble());
}

QString bindingKindName(const WorkspaceBindingKind kind)
{
    switch (kind) {
    case WorkspaceBindingKind::SampleChannel: return QStringLiteral("sample-channel");
    case WorkspaceBindingKind::ImageChannel: return QStringLiteral("image-channel");
    case WorkspaceBindingKind::EventStream: return QStringLiteral("event-stream");
    case WorkspaceBindingKind::TextStream: return QStringLiteral("text-stream");
    case WorkspaceBindingKind::Command: return QStringLiteral("command");
    case WorkspaceBindingKind::None: break;
    }
    return QStringLiteral("none");
}

std::optional<WorkspaceBindingKind> bindingKindFromName(const QString &name)
{
    const QString value = name.trimmed().toCaseFolded();
    if (value == QStringLiteral("sample-channel")) return WorkspaceBindingKind::SampleChannel;
    if (value == QStringLiteral("image-channel")) return WorkspaceBindingKind::ImageChannel;
    if (value == QStringLiteral("event-stream")) return WorkspaceBindingKind::EventStream;
    if (value == QStringLiteral("text-stream")) return WorkspaceBindingKind::TextStream;
    if (value == QStringLiteral("command")) return WorkspaceBindingKind::Command;
    if (value == QStringLiteral("none")) return WorkspaceBindingKind::None;
    return std::nullopt;
}

QJsonObject bindingToJson(const WorkspaceBinding &binding)
{
    return {
        {QStringLiteral("kind"), bindingKindName(binding.kind)},
        {QStringLiteral("channelId"), binding.channelId},
        {QStringLiteral("parameter"), binding.parameter},
    };
}

bool bindingFromJson(const QJsonValue &value, WorkspaceBinding *binding,
                    QString *error, const int index)
{
    if (!value.isObject()) {
        setError(error, QStringLiteral("绑定 #%1 必须是对象").arg(index + 1));
        return false;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue kindValue = object.value(QStringLiteral("kind"));
    if (!kindValue.isString()) {
        setError(error, QStringLiteral("绑定 #%1 的 kind 无效").arg(index + 1));
        return false;
    }
    const auto kind = bindingKindFromName(kindValue.toString());
    if (!kind) {
        setError(error, QStringLiteral("绑定 #%1 的 kind 未知：%2")
                                  .arg(index + 1).arg(kindValue.toString()));
        return false;
    }
    const QString channelId = object.value(QStringLiteral("channelId")).toString().trimmed();
    if (*kind != WorkspaceBindingKind::Command && channelId.isEmpty()) {
        setError(error, QStringLiteral("绑定 #%1 缺少 channelId").arg(index + 1));
        return false;
    }
    if (!channelId.isEmpty() && !validId(channelId)) {
        setError(error, QStringLiteral("绑定 #%1 的 channelId 无效").arg(index + 1));
        return false;
    }
    if (binding) {
        binding->kind = *kind;
        binding->channelId = channelId;
        binding->parameter = object.value(QStringLiteral("parameter")).toString();
    }
    return true;
}

bool componentFromJson(const QJsonValue &value, WorkspaceComponentInstance *component,
                       QString *error, const int index)
{
    if (!value.isObject()) {
        setError(error, QStringLiteral("组件 #%1 必须是对象").arg(index + 1));
        return false;
    }
    const QJsonObject object = value.toObject();
    const QString instanceText = object.value(QStringLiteral("instanceId")).toString().trimmed();
    const QUuid instanceId(instanceText);
    if (instanceId.isNull()) {
        setError(error, QStringLiteral("组件 #%1 的 instanceId 无效").arg(index + 1));
        return false;
    }
    QString definitionId;
    if (!nonEmptyString(object, QStringLiteral("definitionId"), &definitionId, error)
        || !validId(definitionId)) {
        setError(error, QStringLiteral("组件 #%1 的 definitionId 无效").arg(index + 1));
        return false;
    }

    const QJsonObject geometry = object.value(QStringLiteral("geometry")).toObject();
    const QRectF normalizedGeometry(geometry.value(QStringLiteral("x")).toDouble(),
                                    geometry.value(QStringLiteral("y")).toDouble(),
                                    geometry.value(QStringLiteral("width")).toDouble(),
                                    geometry.value(QStringLiteral("height")).toDouble());
    for (const QString &key : {QStringLiteral("x"), QStringLiteral("y"),
                               QStringLiteral("width"), QStringLiteral("height")}) {
        if (!finiteNumber(geometry.value(key))) {
            setError(error, QStringLiteral("组件 #%1 的 geometry.%2 无效")
                                      .arg(index + 1).arg(key));
            return false;
        }
    }
    if (normalizedGeometry.x() < 0.0 || normalizedGeometry.y() < 0.0
        || normalizedGeometry.width() <= 0.0 || normalizedGeometry.height() <= 0.0
        || normalizedGeometry.right() > 1.0 || normalizedGeometry.bottom() > 1.0) {
        setError(error, QStringLiteral("组件 #%1 的归一化几何超出 0～1 范围")
                                  .arg(index + 1));
        return false;
    }

    QList<WorkspaceBinding> bindings;
    const QJsonValue bindingsValue = object.value(QStringLiteral("bindings"));
    if (!bindingsValue.isUndefined()) {
        if (!bindingsValue.isArray()) {
            setError(error, QStringLiteral("组件 #%1 的 bindings 必须是数组").arg(index + 1));
            return false;
        }
        const QJsonArray array = bindingsValue.toArray();
        for (int bindingIndex = 0; bindingIndex < array.size(); ++bindingIndex) {
            WorkspaceBinding binding;
            if (!bindingFromJson(array.at(bindingIndex), &binding, error, bindingIndex)) {
                return false;
            }
            bindings.append(std::move(binding));
        }
    }

    QJsonObject extensions;
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() != QStringLiteral("instanceId")
            && it.key() != QStringLiteral("definitionId")
            && it.key() != QStringLiteral("geometry")
            && it.key() != QStringLiteral("zOrder")
            && it.key() != QStringLiteral("visible")
            && it.key() != QStringLiteral("locked")
            && it.key() != QStringLiteral("maximized")
            && it.key() != QStringLiteral("bindings")
            && it.key() != QStringLiteral("properties")) {
            extensions.insert(it.key(), it.value());
        }
    }

    if (component) {
        component->instanceId = instanceId;
        component->definitionId = definitionId;
        component->normalizedGeometry = normalizedGeometry;
        component->zOrder = object.value(QStringLiteral("zOrder")).toInt(0);
        component->visible = object.value(QStringLiteral("visible")).toBool(true);
        component->locked = object.value(QStringLiteral("locked")).toBool(false);
        component->maximized = object.value(QStringLiteral("maximized")).toBool(false);
        component->bindings = std::move(bindings);
        component->properties = object.value(QStringLiteral("properties")).toObject().toVariantMap();
        component->extensions = std::move(extensions);
    }
    return true;
}

} // namespace

QJsonObject WorkspaceDocumentPage::toJsonObject() const
{
    QJsonArray componentsArray;
    for (const WorkspaceComponentInstance &component : components) {
        QJsonArray bindingsArray;
        for (const WorkspaceBinding &binding : component.bindings) {
            bindingsArray.append(bindingToJson(binding));
        }
        QJsonObject componentObject = component.extensions;
        componentObject.insert(QStringLiteral("instanceId"),
                               component.instanceId.toString(QUuid::WithoutBraces));
        componentObject.insert(QStringLiteral("definitionId"), component.definitionId);
        componentObject.insert(QStringLiteral("geometry"), QJsonObject{
            {QStringLiteral("x"), component.normalizedGeometry.x()},
            {QStringLiteral("y"), component.normalizedGeometry.y()},
            {QStringLiteral("width"), component.normalizedGeometry.width()},
            {QStringLiteral("height"), component.normalizedGeometry.height()},
        });
        componentObject.insert(QStringLiteral("zOrder"), component.zOrder);
        componentObject.insert(QStringLiteral("visible"), component.visible);
        componentObject.insert(QStringLiteral("locked"), component.locked);
        componentObject.insert(QStringLiteral("maximized"), component.maximized);
        componentObject.insert(QStringLiteral("bindings"), bindingsArray);
        componentObject.insert(QStringLiteral("properties"),
                               QJsonObject::fromVariantMap(component.properties));
        componentsArray.append(componentObject);
    }
    QJsonObject result = extensions;
    result.insert(QStringLiteral("pageId"), pageId);
    result.insert(QStringLiteral("title"), title);
    result.insert(QStringLiteral("components"), componentsArray);
    return result;
}

bool WorkspaceDocumentPage::validate(QString *error) const
{
    if (!validId(pageId)) {
        setError(error, QStringLiteral("页面 pageId 无效：%1").arg(pageId));
        return false;
    }
    if (title.trimmed().isEmpty()) {
        setError(error, QStringLiteral("页面标题不能为空：%1").arg(pageId));
        return false;
    }
    QSet<QUuid> ids;
    for (int index = 0; index < components.size(); ++index) {
        const WorkspaceComponentInstance &component = components.at(index);
        if (component.instanceId.isNull() || ids.contains(component.instanceId)) {
            setError(error, QStringLiteral("页面 %1 的组件 UUID 缺失或重复（#%2）")
                                      .arg(pageId).arg(index + 1));
            return false;
        }
        if (!validId(component.definitionId)) {
            setError(error, QStringLiteral("页面 %1 的组件定义 ID 无效：%2")
                                      .arg(pageId, component.definitionId));
            return false;
        }
        const QRectF &geometry = component.normalizedGeometry;
        if (!std::isfinite(geometry.x()) || !std::isfinite(geometry.y())
            || !std::isfinite(geometry.width()) || !std::isfinite(geometry.height())
            || geometry.x() < 0.0 || geometry.y() < 0.0 || geometry.width() <= 0.0
            || geometry.height() <= 0.0 || geometry.right() > 1.0
            || geometry.bottom() > 1.0) {
            setError(error, QStringLiteral("页面 %1 的组件几何无效：%2")
                                      .arg(pageId, component.definitionId));
            return false;
        }
        QSet<QString> bindingIds;
        for (const WorkspaceBinding &binding : component.bindings) {
            if (binding.kind == WorkspaceBindingKind::None
                || (binding.kind != WorkspaceBindingKind::Command
                    && !validId(binding.channelId))) {
                setError(error, QStringLiteral("页面 %1 的组件绑定无效：%2")
                                          .arg(pageId, component.definitionId));
                return false;
            }
            if (!binding.channelId.isEmpty() && bindingIds.contains(binding.channelId)) {
                setError(error, QStringLiteral("页面 %1 的组件绑定重复：%2")
                                          .arg(pageId, binding.channelId));
                return false;
            }
            bindingIds.insert(binding.channelId);
        }
        ids.insert(component.instanceId);
    }
    return true;
}

std::optional<WorkspaceDocumentPage> WorkspaceDocumentPage::fromJsonObject(
    const QJsonObject &object, QString *error)
{
    WorkspaceDocumentPage page;
    if (!nonEmptyString(object, QStringLiteral("pageId"), &page.pageId, error)
        || !nonEmptyString(object, QStringLiteral("title"), &page.title, error)) {
        return std::nullopt;
    }
    const QJsonValue componentsValue = object.value(QStringLiteral("components"));
    if (!componentsValue.isArray()) {
        setError(error, QStringLiteral("页面 %1 的 components 必须是数组").arg(page.pageId));
        return std::nullopt;
    }
    const QJsonArray components = componentsValue.toArray();
    for (int index = 0; index < components.size(); ++index) {
        WorkspaceComponentInstance component;
        if (!componentFromJson(components.at(index), &component, error, index)) {
            return std::nullopt;
        }
        page.components.append(std::move(component));
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() != QStringLiteral("pageId")
            && it.key() != QStringLiteral("title")
            && it.key() != QStringLiteral("components")) {
            page.extensions.insert(it.key(), it.value());
        }
    }
    return page.validate(error) ? std::optional<WorkspaceDocumentPage>(std::move(page))
                                : std::nullopt;
}

WorkspaceDocument WorkspaceDocument::createDefault(const QString &title,
                                                   const QString &documentId)
{
    WorkspaceDocument document;
    document.m_documentId = documentId.trimmed().isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces) : documentId.trimmed();
    document.m_title = title.trimmed().isEmpty()
        ? QStringLiteral("Serein Workspace") : title.trimmed();
    document.m_pages.append({QStringLiteral("main"), document.m_title, {}, {}});
    return document;
}

QJsonObject WorkspaceDocument::toJsonObject() const
{
    QJsonArray pagesArray;
    for (const WorkspaceDocumentPage &page : m_pages) pagesArray.append(page.toJsonObject());
    QJsonObject result = m_extensions;
    result.insert(QStringLiteral("schemaVersion"), m_schemaVersion);
    result.insert(QStringLiteral("documentId"), m_documentId);
    result.insert(QStringLiteral("title"), m_title);
    result.insert(QStringLiteral("targetProfile"), m_targetProfile);
    result.insert(QStringLiteral("pages"), pagesArray);
    return result;
}

QJsonDocument WorkspaceDocument::toJsonDocument() const
{
    return QJsonDocument(toJsonObject());
}

bool WorkspaceDocument::setJsonObject(const QJsonObject &object, QString *error)
{
    auto parsed = fromJsonObject(object, error);
    if (!parsed) return false;
    *this = std::move(*parsed);
    return true;
}

bool WorkspaceDocument::setJsonDocument(const QJsonDocument &document, QString *error)
{
    if (!document.isObject()) {
        setError(error, QStringLiteral("工作台文档根节点必须是对象"));
        return false;
    }
    return setJsonObject(document.object(), error);
}

bool WorkspaceDocument::validate(QString *error) const
{
    if (m_schemaVersion <= 0 || m_schemaVersion > CurrentSchemaVersion) {
        setError(error, QStringLiteral("不支持的 schemaVersion：%1").arg(m_schemaVersion));
        return false;
    }
    if (!validId(m_documentId)) {
        setError(error, QStringLiteral("documentId 无效：%1").arg(m_documentId));
        return false;
    }
    if (m_title.trimmed().isEmpty() || m_targetProfile.trimmed().isEmpty()) {
        setError(error, QStringLiteral("工作台标题或 targetProfile 不能为空"));
        return false;
    }
    if (m_pages.isEmpty()) {
        setError(error, QStringLiteral("工作台至少需要一个页面"));
        return false;
    }
    QSet<QString> pageIds;
    QSet<QUuid> componentIds;
    for (const WorkspaceDocumentPage &page : m_pages) {
        if (!page.validate(error) || pageIds.contains(page.pageId)) {
            if (pageIds.contains(page.pageId)) {
                setError(error, QStringLiteral("页面 pageId 重复：%1").arg(page.pageId));
            }
            return false;
        }
        pageIds.insert(page.pageId);
        for (const WorkspaceComponentInstance &component : page.components) {
            if (componentIds.contains(component.instanceId)) {
                setError(error, QStringLiteral("工作台组件 UUID 重复：%1")
                                          .arg(component.instanceId.toString()));
                return false;
            }
            componentIds.insert(component.instanceId);
        }
    }
    return true;
}

std::optional<WorkspaceDocument> WorkspaceDocument::fromJsonObject(
    const QJsonObject &object, QString *error)
{
    WorkspaceDocument document;
    const QJsonValue schema = object.value(QStringLiteral("schemaVersion"));
    if (!schema.isDouble() || schema.toInt() <= 0
        || schema.toDouble() != static_cast<double>(schema.toInt())) {
        setError(error, QStringLiteral("schemaVersion 必须是正整数"));
        return std::nullopt;
    }
    document.m_schemaVersion = schema.toInt();
    if (document.m_schemaVersion > CurrentSchemaVersion) {
        setError(error, QStringLiteral("不支持的 schemaVersion：%1")
                                  .arg(document.m_schemaVersion));
        return std::nullopt;
    }
    if (!nonEmptyString(object, QStringLiteral("documentId"), &document.m_documentId, error)
        || !nonEmptyString(object, QStringLiteral("title"), &document.m_title, error)
        || !nonEmptyString(object, QStringLiteral("targetProfile"),
                           &document.m_targetProfile, error)) {
        return std::nullopt;
    }
    if (!validId(document.m_documentId)) {
        setError(error, QStringLiteral("documentId 含非法字符：%1").arg(document.m_documentId));
        return std::nullopt;
    }
    const QJsonValue pagesValue = object.value(QStringLiteral("pages"));
    if (!pagesValue.isArray()) {
        setError(error, QStringLiteral("pages 必须是数组"));
        return std::nullopt;
    }
    const QJsonArray pages = pagesValue.toArray();
    for (int index = 0; index < pages.size(); ++index) {
        if (!pages.at(index).isObject()) {
            setError(error, QStringLiteral("页面 #%1 必须是对象").arg(index + 1));
            return std::nullopt;
        }
        auto page = WorkspaceDocumentPage::fromJsonObject(
            pages.at(index).toObject(), error);
        if (!page) {
            if (error && !error->isEmpty()) {
                *error = QStringLiteral("页面 #%1：%2").arg(index + 1).arg(*error);
            }
            return std::nullopt;
        }
        document.m_pages.append(std::move(*page));
    }
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() != QStringLiteral("schemaVersion")
            && it.key() != QStringLiteral("documentId")
            && it.key() != QStringLiteral("title")
            && it.key() != QStringLiteral("targetProfile")
            && it.key() != QStringLiteral("pages")) {
            document.m_extensions.insert(it.key(), it.value());
        }
    }
    return document.validate(error) ? std::optional<WorkspaceDocument>(std::move(document))
                                     : std::nullopt;
}

QStringList WorkspaceDocument::missingDefinitionIds(
    const QStringList &availableDefinitionIds) const
{
    QSet<QString> available;
    for (const QString &id : availableDefinitionIds) available.insert(id.trimmed());
    QSet<QString> missing;
    for (const WorkspaceDocumentPage &page : m_pages) {
        for (const WorkspaceComponentInstance &component : page.components) {
            if (!available.contains(component.definitionId)) missing.insert(component.definitionId);
        }
    }
    QStringList result = missing.values();
    result.sort();
    return result;
}

bool WorkspaceDocument::loadFromFile(const QString &filePath, QString *error)
{
    auto loaded = fromFile(filePath, error);
    if (!loaded) return false;
    *this = std::move(*loaded);
    return true;
}

bool WorkspaceDocument::saveToFileAtomic(const QString &filePath, QString *error) const
{
    if (!validate(error)) return false;
    const QString path = filePath.trimmed();
    if (path.isEmpty()) {
        setError(error, QStringLiteral("工作台文档路径不能为空"));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("无法写入工作台文档：%1").arg(file.errorString()));
        return false;
    }
    const QByteArray bytes = toJsonDocument().toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error, QStringLiteral("原子保存工作台文档失败：%1").arg(file.errorString()));
        file.cancelWriting();
        return false;
    }
    return true;
}

std::optional<WorkspaceDocument> WorkspaceDocument::fromFile(
    const QString &filePath, QString *error)
{
    const QString path = filePath.trimmed();
    if (path.isEmpty()) {
        setError(error, QStringLiteral("工作台文档路径不能为空"));
        return std::nullopt;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法读取工作台文档：%1").arg(file.errorString()));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("工作台 JSON 无效：%1").arg(parseError.errorString()));
        return std::nullopt;
    }
    WorkspaceDocument result;
    return result.setJsonDocument(document, error)
        ? std::optional<WorkspaceDocument>(std::move(result)) : std::nullopt;
}

} // namespace serein::scope
