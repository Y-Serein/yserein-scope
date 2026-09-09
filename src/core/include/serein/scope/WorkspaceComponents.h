#pragma once

#include "CoreExport.h"
#include "PluginInterface.h"

#include <QtCore/QList>
#include <QtCore/QFlags>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QSize>
#include <QtCore/QUuid>
#include <QtCore/QVariant>
#include <QtCore/QVariantMap>
#include <QtCore/QRectF>

class QWidget;

namespace serein::scope {

// Capabilities describe what the host may offer in edit mode. They are
// metadata only: the canvas remains responsible for interaction chrome,
// geometry and persistence.
enum class WorkspaceComponentCapability : quint32 {
    None = 0,
    Movable = 1u << 0,
    Resizable = 1u << 1,
    Maximizable = 1u << 2,
    Cloneable = 1u << 3,
    DataBinding = 1u << 4,
    PropertyEditing = 1u << 5,
};
Q_DECLARE_FLAGS(WorkspaceComponentCapabilities, WorkspaceComponentCapability)
Q_DECLARE_OPERATORS_FOR_FLAGS(WorkspaceComponentCapabilities)

// Binding kinds are protocol-neutral. A component consumes a structured
// channel/event supplied by the host, never a serial handle.
enum class WorkspaceBindingKind : quint32 {
    None = 0,
    SampleChannel = 1u << 0,
    ImageChannel = 1u << 1,
    EventStream = 1u << 2,
    TextStream = 1u << 3,
    Command = 1u << 4,
};
Q_DECLARE_FLAGS(WorkspaceBindingKinds, WorkspaceBindingKind)
Q_DECLARE_OPERATORS_FOR_FLAGS(WorkspaceBindingKinds)

struct SEREIN_SCOPE_CORE_EXPORT WorkspaceComponentProperty {
    QString key;
    QString title;
    QString type;
    QVariant defaultValue;
    QVariantMap constraints;
};

struct SEREIN_SCOPE_CORE_EXPORT WorkspaceBinding {
    WorkspaceBindingKind kind = WorkspaceBindingKind::None;
    QString channelId;
    QString parameter;
};

struct SEREIN_SCOPE_CORE_EXPORT WorkspaceComponentDefinition {
    QString definitionId;
    QString providerId;
    QString displayName;
    QString category;
    QString iconSource;
    QSize defaultSize;
    QSize minimumSize;
    WorkspaceComponentCapabilities capabilities;
    WorkspaceBindingKinds bindingKinds;
    QList<WorkspaceComponentProperty> properties;
};

// Serializable user state for one placed component. Runtime pointers,
// sessions and sampled data deliberately do not belong here; this is the
// boundary used later by WorkspaceDocument persistence.
struct SEREIN_SCOPE_CORE_EXPORT WorkspaceComponentInstance {
    QUuid instanceId = QUuid::createUuid();
    QString definitionId;
    QRectF normalizedGeometry;
    int zOrder = 0;
    bool visible = true;
    bool locked = false;
    bool maximized = false;
    QList<WorkspaceBinding> bindings;
    QVariantMap properties;
};

// Optional component-level extension. It is deliberately separate from
// IDevicePlugin/2.0 and IWorkspaceUiProvider/1.0, so adding component types
// never changes an existing plugin vtable. The provider is normally a
// workspace object so factories share that workspace's device session.
class SEREIN_SCOPE_CORE_EXPORT IWorkspaceComponentProvider {
public:
    virtual ~IWorkspaceComponentProvider();
    [[nodiscard]] virtual QList<WorkspaceComponentDefinition>
    componentDefinitions() const = 0;
    virtual QWidget *createComponent(const QString &definitionId,
                                      QWidget *parent) = 0;
};

class SEREIN_SCOPE_CORE_EXPORT WorkspaceComponentRegistry final : public QObject {
    Q_OBJECT

public:
    explicit WorkspaceComponentRegistry(QObject *parent = nullptr);

    bool registerProvider(IWorkspaceComponentProvider *provider,
                          QObject *lifetime = nullptr,
                          QString *error = nullptr);
    void unregisterProvider(IWorkspaceComponentProvider *provider);
    void clear();

    [[nodiscard]] QList<WorkspaceComponentDefinition> definitions() const;
    [[nodiscard]] WorkspaceComponentDefinition definition(
        const QString &definitionId) const;
    [[nodiscard]] bool contains(const QString &definitionId) const;
    QWidget *createComponent(const QString &definitionId, QWidget *parent,
                             QString *error = nullptr) const;

signals:
    void definitionsChanged();

private:
    struct Entry {
        WorkspaceComponentDefinition definition;
        IWorkspaceComponentProvider *provider = nullptr;
        QPointer<QObject> lifetime;
        bool tracksLifetime = false;
    };

    QList<Entry> m_entries;
};

} // namespace serein::scope
