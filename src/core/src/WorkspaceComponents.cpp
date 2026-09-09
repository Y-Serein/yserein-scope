#include <serein/scope/WorkspaceComponents.h>

#include <QtCore/QSet>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <exception>

namespace serein::scope {
namespace {

void setError(QString *target, const QString &message)
{
    if (target) *target = message;
}

bool validDefinition(const WorkspaceComponentDefinition &definition,
                     QString *error)
{
    if (definition.definitionId.trimmed().isEmpty()) {
        setError(error, QStringLiteral("组件定义缺少稳定 ID"));
        return false;
    }
    if (definition.displayName.trimmed().isEmpty()) {
        setError(error, QStringLiteral("组件定义缺少显示名称：%1")
                              .arg(definition.definitionId));
        return false;
    }
    if (definition.defaultSize.width() <= 0 || definition.defaultSize.height() <= 0) {
        setError(error, QStringLiteral("组件默认尺寸无效：%1")
                              .arg(definition.definitionId));
        return false;
    }
    if (definition.minimumSize.width() <= 0 || definition.minimumSize.height() <= 0
        || definition.minimumSize.width() > definition.defaultSize.width()
        || definition.minimumSize.height() > definition.defaultSize.height()) {
        setError(error, QStringLiteral("组件最小尺寸无效：%1")
                              .arg(definition.definitionId));
        return false;
    }
    QSet<QString> propertyKeys;
    for (const WorkspaceComponentProperty &property : definition.properties) {
        const QString key = property.key.trimmed();
        if (key.isEmpty() || property.title.trimmed().isEmpty()
            || property.type.trimmed().isEmpty() || propertyKeys.contains(key)) {
            setError(error, QStringLiteral("组件属性 Schema 无效：%1")
                                  .arg(definition.definitionId));
            return false;
        }
        propertyKeys.insert(key);
    }
    return true;
}

} // namespace

IWorkspaceComponentProvider::~IWorkspaceComponentProvider() = default;

WorkspaceComponentRegistry::WorkspaceComponentRegistry(QObject *parent)
    : QObject(parent)
{
}

bool WorkspaceComponentRegistry::registerProvider(
    IWorkspaceComponentProvider *provider, QObject *lifetime, QString *error)
{
    if (!provider) {
        setError(error, QStringLiteral("组件 Provider 为空"));
        return false;
    }

    QList<WorkspaceComponentDefinition> provided;
    try {
        provided = provider->componentDefinitions();
    } catch (const std::exception &exception) {
        setError(error, QStringLiteral("组件定义工厂异常：%1").arg(exception.what()));
        return false;
    } catch (...) {
        setError(error, QStringLiteral("组件定义 Provider 抛出未知异常"));
        return false;
    }
    const bool lifetimeAlreadyTracked = lifetime
        && std::any_of(m_entries.cbegin(), m_entries.cend(),
                       [provider, lifetime](const Entry &entry) {
                           return entry.provider == provider
                               && entry.tracksLifetime
                               && entry.lifetime.data() == lifetime;
                       });
    QSet<QString> ids;
    for (WorkspaceComponentDefinition candidate : provided) {
        candidate.definitionId = candidate.definitionId.trimmed();
        candidate.providerId = candidate.providerId.trimmed();
        if (candidate.providerId.trimmed().isEmpty()) {
            setError(error, QStringLiteral("组件 Provider ID 为空：%1")
                                  .arg(candidate.definitionId));
            return false;
        }
        if (!validDefinition(candidate, error) || ids.contains(candidate.definitionId)) {
            if (ids.contains(candidate.definitionId)) {
                setError(error, QStringLiteral("组件定义 ID 重复：%1")
                                      .arg(candidate.definitionId));
            }
            return false;
        }
        ids.insert(candidate.definitionId);
        for (const Entry &entry : m_entries) {
            if (entry.provider != provider && entry.definition.definitionId
                == candidate.definitionId) {
                setError(error, QStringLiteral("组件定义 ID 已注册：%1")
                                      .arg(candidate.definitionId));
                return false;
            }
        }
    }

    bool changed = false;
    for (int index = m_entries.size() - 1; index >= 0; --index) {
        if (m_entries.at(index).provider == provider) {
            m_entries.removeAt(index);
            changed = true;
        }
    }
    for (WorkspaceComponentDefinition candidate : provided) {
        candidate.definitionId = candidate.definitionId.trimmed();
        candidate.providerId = candidate.providerId.trimmed();
        Entry entry;
        entry.definition = std::move(candidate);
        entry.provider = provider;
        entry.lifetime = lifetime;
        entry.tracksLifetime = lifetime != nullptr;
        m_entries.append(std::move(entry));
    }
    if (lifetime && !lifetimeAlreadyTracked) {
        QObject::connect(lifetime, &QObject::destroyed, this,
                         [this, provider] { unregisterProvider(provider); });
    }
    if (changed || !provided.isEmpty()) emit definitionsChanged();
    return true;
}

void WorkspaceComponentRegistry::unregisterProvider(IWorkspaceComponentProvider *provider)
{
    if (!provider) return;
    bool changed = false;
    for (int index = m_entries.size() - 1; index >= 0; --index) {
        if (m_entries.at(index).provider == provider) {
            m_entries.removeAt(index);
            changed = true;
        }
    }
    if (changed) emit definitionsChanged();
}

void WorkspaceComponentRegistry::clear()
{
    if (m_entries.isEmpty()) return;
    m_entries.clear();
    emit definitionsChanged();
}

QList<WorkspaceComponentDefinition> WorkspaceComponentRegistry::definitions() const
{
    QList<WorkspaceComponentDefinition> result;
    result.reserve(m_entries.size());
    for (const Entry &entry : m_entries) result.append(entry.definition);
    return result;
}

WorkspaceComponentDefinition WorkspaceComponentRegistry::definition(
    const QString &definitionId) const
{
    const QString id = definitionId.trimmed();
    for (const Entry &entry : m_entries) {
        if (entry.definition.definitionId == id) return entry.definition;
    }
    return {};
}

bool WorkspaceComponentRegistry::contains(const QString &definitionId) const
{
    return !definition(definitionId).definitionId.isEmpty();
}

QWidget *WorkspaceComponentRegistry::createComponent(const QString &definitionId,
                                                       QWidget *parent,
                                                       QString *error) const
{
    const QString id = definitionId.trimmed();
    for (const Entry &entry : m_entries) {
        if (entry.definition.definitionId != id) continue;
        if (!entry.provider || (entry.tracksLifetime && entry.lifetime.isNull())) {
            setError(error, QStringLiteral("组件 Provider 已失效：%1").arg(id));
            return nullptr;
        }
        QWidget *widget = nullptr;
        try {
            widget = entry.provider->createComponent(id, parent);
        } catch (const std::exception &exception) {
            setError(error, QStringLiteral("组件工厂异常：%1").arg(exception.what()));
            return nullptr;
        } catch (...) {
            setError(error, QStringLiteral("组件工厂抛出未知异常：%1").arg(id));
            return nullptr;
        }
        if (!widget) {
            setError(error, QStringLiteral("组件工厂未创建视图：%1").arg(id));
            return nullptr;
        }
        widget->setProperty("workspaceComponentDefinitionId", id);
        widget->setMinimumSize(widget->minimumSize().expandedTo(
            entry.definition.minimumSize));
        return widget;
    }
    setError(error, QStringLiteral("未找到组件定义：%1").arg(id));
    return nullptr;
}

} // namespace serein::scope
