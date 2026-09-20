#include <serein/scope/IDataEngine.h>
#include <serein/scope/PluginInterface.h>

#include <QtCore/QPointer>
#include <QtCore/QVariant>
#include <QtWidgets/QWidget>

#include <utility>

namespace serein::scope {
namespace {

constexpr auto PresentationStateProperty = "_sereinWorkspaceUiPresentationState";

struct StoredPanelOptions {
    WorkspacePanelMargins margins = WorkspacePanelMargins::Default;
    QPointer<QWidget> headerControl;
};

class WorkspaceUiPresentationState final : public QObject {
public:
    explicit WorkspaceUiPresentationState(QObject *parent)
        : QObject(parent)
    {
    }

    StoredPanelOptions contextPanel;
    StoredPanelOptions inspectorPanel;
    StoredPanelOptions outputPanel;
    WorkspacePanelSide contextSide = WorkspacePanelSide::Default;
    WorkspacePanelSide inspectorSide = WorkspacePanelSide::Default;
};

WorkspaceUiPresentationState *presentationState(const WorkspaceUiProvider *provider)
{
    if (!provider) return nullptr;
    QObject *object = provider->property(PresentationStateProperty).value<QObject *>();
    return static_cast<WorkspaceUiPresentationState *>(object);
}

void storeOptions(StoredPanelOptions &target, const WorkspacePanelOptions &source)
{
    target.margins = source.margins;
    target.headerControl = source.headerControl;
}

WorkspacePanelOptions loadOptions(const StoredPanelOptions &source)
{
    return {source.margins, source.headerControl.data()};
}

WorkspacePanelSide normalizeSide(const WorkspacePanelSide side)
{
    return side == WorkspacePanelSide::Left || side == WorkspacePanelSide::Right
        ? side : WorkspacePanelSide::Default;
}

void markPanelContribution(WorkspacePanelPresentation &panel, const char *slot)
{
    if (!panel.content) return;
    panel.content->setProperty("workspacePanelContribution", true);
    panel.content->setProperty("workspacePanelSlot", slot);
    panel.content->setMinimumWidth(0);
}

} // namespace

IHostServices::~IHostServices() = default;
IDeviceWorkspace::~IDeviceWorkspace() = default;
IDevicePlugin::~IDevicePlugin() = default;
IWorkspaceUiProvider::~IWorkspaceUiProvider() = default;
IDataEngine::~IDataEngine() = default;

WorkspaceUiProvider::WorkspaceUiProvider(QObject *parent)
    : QObject(parent)
{
    auto *state = new WorkspaceUiPresentationState(this);
    setProperty(PresentationStateProperty,
                QVariant::fromValue(static_cast<QObject *>(state)));
}

const WorkspaceUiContext &WorkspaceUiProvider::context() const noexcept
{
    return m_context;
}

WorkspaceUiPresentationOptions WorkspaceUiProvider::presentationOptions() const noexcept
{
    const WorkspaceUiPresentationState *state = presentationState(this);
    if (!state) return {};
    return {loadOptions(state->contextPanel),
            loadOptions(state->inspectorPanel),
            loadOptions(state->outputPanel)};
}

WorkspacePanelSide WorkspaceUiProvider::panelSide(const WorkspacePanelSlot slot) const noexcept
{
    const WorkspaceUiPresentationState *state = presentationState(this);
    if (!state) return WorkspacePanelSide::Default;
    switch (slot) {
    case WorkspacePanelSlot::Context: return state->contextSide;
    case WorkspacePanelSlot::Inspector: return state->inspectorSide;
    case WorkspacePanelSlot::Output: return WorkspacePanelSide::Default;
    }
    return WorkspacePanelSide::Default;
}

void WorkspaceUiProvider::setPanelSides(const WorkspacePanelSide contextSide,
                                        const WorkspacePanelSide inspectorSide)
{
    WorkspaceUiPresentationState *state = presentationState(this);
    if (!state) return;
    const WorkspacePanelSide normalizedContext = normalizeSide(contextSide);
    const WorkspacePanelSide normalizedInspector = normalizeSide(inspectorSide);
    if (state->contextSide == normalizedContext
        && state->inspectorSide == normalizedInspector) return;
    state->contextSide = normalizedContext;
    state->inspectorSide = normalizedInspector;
    emit presentationChanged();
}

void WorkspaceUiProvider::setContext(WorkspaceUiContext context)
{
    setContext(std::move(context), {});
}

void WorkspaceUiProvider::setContext(
    WorkspaceUiContext context,
    WorkspaceUiPresentationOptions presentationOptions)
{
    // Panel chrome, geometry and spacing belong to the host. Contributions
    // only publish body content and receive a stable semantic role that the
    // host theme can style without knowing a component id or private widget
    // name.
    WorkspaceUiPresentationState *state = presentationState(this);
    if (state) {
        storeOptions(state->contextPanel, presentationOptions.contextPanel);
        storeOptions(state->inspectorPanel, presentationOptions.inspectorPanel);
        storeOptions(state->outputPanel, presentationOptions.outputPanel);
    }
    markPanelContribution(context.contextPanel, "context");
    markPanelContribution(context.inspectorPanel, "inspector");
    markPanelContribution(context.outputPanel, "output");
    m_context = std::move(context);
    emit presentationChanged();
}

void WorkspaceUiProvider::prepareContextChange()
{
    emit presentationAboutToChange();
}

} // namespace serein::scope
