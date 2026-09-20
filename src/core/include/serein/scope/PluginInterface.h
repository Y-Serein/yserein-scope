#pragma once

#include "CoreExport.h"

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QtPlugin>

class QWidget;

namespace serein::scope {

struct WorkspaceComponentDefinition;
struct WorkspaceComponentInstance;
class IWorkspaceComponentProvider;

// Optional runtime bridge for applying persisted/edited instance state to a
// live component widget. The full component model is defined in
// WorkspaceComponents.h; this declaration stays here so Qt's optional
// interface metadata is visible to plugin headers without changing ABI 2.
class SEREIN_SCOPE_CORE_EXPORT IWorkspaceComponentStateAdapter {
public:
    virtual ~IWorkspaceComponentStateAdapter();
    virtual bool applyComponentState(
        QWidget *component, const WorkspaceComponentInstance &instance,
        QString *error = nullptr) = 0;
};

inline constexpr int PluginInterfaceVersion = 2;
inline constexpr auto DevicePluginIid = "com.serein.scope.IDevicePlugin/2.0";
inline constexpr auto WorkspaceIid = "com.serein.scope.IDeviceWorkspace/1.0";
inline constexpr auto WorkspaceUiProviderIid =
    "com.serein.scope.IWorkspaceUiProvider/1.0";
inline constexpr auto WorkspaceComponentProviderIid =
    "com.serein.scope.IWorkspaceComponentProvider/1.0";
inline constexpr auto WorkspaceComponentStateAdapterIid =
    "com.serein.scope.IWorkspaceComponentStateAdapter/1.0";

struct PluginMetadata {
    QString id;
    QString name;
    QString description;
    QString version;
    QString category;
    QString accent = QStringLiteral("#65d1c5");
    QStringList capabilities;
    int interfaceVersion = PluginInterfaceVersion;
};

class SEREIN_SCOPE_CORE_EXPORT IHostServices {
public:
    virtual ~IHostServices();
    virtual void log(const QString &level, const QString &message) = 0;
    virtual void setStatus(const QString &message) = 0;
};

class SEREIN_SCOPE_CORE_EXPORT IDeviceWorkspace {
public:
    virtual ~IDeviceWorkspace();
    virtual void shutdown() = 0;
};

// Optional UI contribution contract. This is deliberately separate from the
// ABI2 device/plugin interfaces: adding shell-facing UI content must not
// change the existing plugin vtables. The host owns all docks, geometry,
// visibility and splitter state; a component can only provide content for a
// fixed semantic slot.
enum class WorkspacePanelSlot {
    Context,
    Inspector,
    Output,
};

// Optional semantic side preference for the two horizontal host slots.  This
// lives outside the ABI2 by-value presentation structs; old providers keep
// the host default while newer providers can express designer-style layouts.
enum class WorkspacePanelSide {
    Default,
    Left,
    Right,
};

// The host supplies the outer panel spacing. Default is Standard for side
// panels and Flush for Output. A component can opt out for a scroll surface
// that already owns its content inset.
enum class WorkspacePanelMargins {
    Default,
    Standard,
    Flush,
};

enum class WorkspaceLayoutProfile {
    Standard,
    FirmwareTask,
    CompactTask,
    Diagnostics,
    CalibrationTask,
    Authoring,
};

struct WorkspacePanelPresentation {
    QWidget *content = nullptr;
    QString title;
    QObject *clearTarget = nullptr;
    QByteArray clearMethod;
};

// Optional shell presentation is carried separately from
// WorkspacePanelPresentation so the ABI2 by-value context layout remains
// unchanged for already-built components.
struct WorkspacePanelOptions {
    WorkspacePanelMargins margins = WorkspacePanelMargins::Default;
    QWidget *headerControl = nullptr;
};

struct WorkspaceUiPresentationOptions {
    WorkspacePanelOptions contextPanel;
    WorkspacePanelOptions inspectorPanel;
    WorkspacePanelOptions outputPanel;
};

struct WorkspaceLibraryItem {
    QString id;
    QString title;
    QString iconSource;
};

// The activation method must be invokable as bool method(const QString &id).
// It runs on the component-owned target; the host never searches component
// children or calls WorkspaceCanvas private implementation details.
struct WorkspaceLibraryPresentation {
    QList<WorkspaceLibraryItem> items;
    QObject *activationTarget = nullptr;
    QByteArray activationMethod;
};

struct WorkspaceUiContext {
    QString id;
    WorkspaceLayoutProfile layoutProfile = WorkspaceLayoutProfile::Standard;
    WorkspacePanelPresentation contextPanel;
    WorkspacePanelPresentation inspectorPanel;
    WorkspacePanelPresentation outputPanel;
    WorkspaceLibraryPresentation componentLibrary;
};

class SEREIN_SCOPE_CORE_EXPORT WorkspaceUiProvider final : public QObject {
    Q_OBJECT

public:
    explicit WorkspaceUiProvider(QObject *parent = nullptr);

    [[nodiscard]] const WorkspaceUiContext &context() const noexcept;
    [[nodiscard]] WorkspaceUiPresentationOptions presentationOptions() const noexcept;
    [[nodiscard]] WorkspacePanelSide panelSide(WorkspacePanelSlot slot) const noexcept;
    void setPanelSides(WorkspacePanelSide contextSide,
                       WorkspacePanelSide inspectorSide);
    void prepareContextChange();
    void setContext(WorkspaceUiContext context);
    void setContext(WorkspaceUiContext context,
                    WorkspaceUiPresentationOptions presentationOptions);

signals:
    void presentationAboutToChange();
    void presentationChanged();

private:
    WorkspaceUiContext m_context;
};

class SEREIN_SCOPE_CORE_EXPORT IWorkspaceUiProvider {
public:
    virtual ~IWorkspaceUiProvider();
    [[nodiscard]] virtual WorkspaceUiProvider *workspaceUiProvider() = 0;
};

class SEREIN_SCOPE_CORE_EXPORT IDevicePlugin {
public:
    virtual ~IDevicePlugin();
    [[nodiscard]] virtual PluginMetadata metadata() const = 0;
    virtual bool initialize(IHostServices *host) = 0;
    virtual QWidget *createWorkspace(QWidget *parent) = 0;
    [[nodiscard]] virtual bool canUnload() const = 0;
    virtual void shutdown() = 0;
};

} // namespace serein::scope

Q_DECLARE_INTERFACE(serein::scope::IDevicePlugin,
                    "com.serein.scope.IDevicePlugin/2.0")
Q_DECLARE_INTERFACE(serein::scope::IDeviceWorkspace,
                    "com.serein.scope.IDeviceWorkspace/1.0")
Q_DECLARE_INTERFACE(serein::scope::IWorkspaceUiProvider,
                    "com.serein.scope.IWorkspaceUiProvider/1.0")
Q_DECLARE_INTERFACE(serein::scope::IWorkspaceComponentStateAdapter,
                    "com.serein.scope.IWorkspaceComponentStateAdapter/1.0")
