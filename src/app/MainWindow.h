#pragma once

#include <serein/scope/PluginInterface.h>
#include <serein/scope/WorkspaceComponents.h>

#include <QtCore/QPoint>
#include <QtCore/QPointer>
#include <QtCore/QHash>
#include <QtWidgets/QMainWindow>

#include <memory>
#include <vector>

class QDockWidget;
class QEvent;
class QHBoxLayout;
class QVBoxLayout;
class QMainWindow;
class QPaintEvent;
class QResizeEvent;
class QGridLayout;
class QLabel;
class QListWidget;
class QMenuBar;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QFrame;

namespace serein::scope::app {

class ComponentManager;

class MainWindow final : public QMainWindow, public IHostServices {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void log(const QString &level, const QString &message) override;
    void setStatus(const QString &message) override;
    bool runComponentCycleTest(const QString &componentId, QString *error = nullptr);

private slots:
    void navigationChanged();
    void toggleComponentManagement();
    void importComponent();
    void refreshComponentUi();
    void preparePageLayoutChange();
    void resetDockLayout();
    void resetAllDockLayouts();
    void toggleDarkMode();

private:
    static constexpr int SidePanelDefaultWidth = 220;
    static constexpr int SidePanelMinimumWidth = 165;
    static constexpr int SidePanelMaximumWidth = 520;

    struct PageLayoutPreset {
        qreal outputFraction = 0.40;
        bool outputVisible = true;
        bool inspectorVisible = false;
        int sidePanelWidth = SidePanelDefaultWidth;
    };

    struct WorkspaceRecord {
        QString componentId;
        QPointer<QWidget> widget;
        int stackIndex = -1;
    };

    struct HeaderControlAttachment {
        QPointer<QWidget> control;
        QPointer<QWidget> owner;
        int layoutIndex = -1;
    };

    void prepareComponents();
    void buildCentralShell();
    void buildGlobalBar();
    QWidget *buildNavigationPanel();
    QWidget *buildHomePage();
    QWidget *buildComponentCenter();
    void buildDocks();
    void buildMenu();
    void applyDefaultDockLayout();
    [[nodiscard]] QString currentPageLayoutContext() const;
    [[nodiscard]] WorkspaceLayoutProfile currentLayoutProfile() const;
    [[nodiscard]] PageLayoutPreset defaultPageLayout(const QString &context) const;
    [[nodiscard]] PageLayoutPreset storedPageLayout(const QString &context) const;
    void savePageLayout(const QString &context);
    void switchPageLayoutContext();
    void applyPageLayout(const QString &context, const PageLayoutPreset &layout);
    void resizeSideDocksForPage(const PageLayoutPreset &layout);
    void resizeOutputDockForCurrentPage();
    void rebuildWorkspaceTabs();
    void selectWorkspaceTab(const QString &id);
    void rebuildNavigation();
    void rebuildCards(QGridLayout *grid, bool loadedOnly, bool managementActions);
    void activatePage(const QString &pageId, bool synchronizeNavigation = true);
    void activateComponent(const QString &componentId, bool synchronizeNavigation = true);
    void requestPrimaryAction(const QString &componentId);
    void requestUnload(const QString &componentId);
    void requestRemove(const QString &componentId);
    bool closeWorkspace(const QString &componentId);
    bool isWorkspaceOpen(const QString &componentId) const;
    [[nodiscard]] bool validateWorkspaceContract(QWidget *workspace,
                                                 QString *error) const;
    WorkspaceRecord *findWorkspace(const QString &componentId);
    const WorkspaceRecord *findWorkspace(const QString &componentId) const;
    void updateInspector(const PluginMetadata *metadata);
    void selectNavigation(const QString &id);
    void applyDarkMode(bool enabled);
    QString componentPackageStartPath() const;
    void refreshComponentLibrary();
    void showLibraryComponent(const QString &componentId);
    bool createRegisteredComponentAt(const QString &componentId,
                                     const QPoint &position);
    void synchronizeComponentRegistry();
    void synchronizeContextPanels();
    void synchronizePanelHeaderControls();
    void synchronizePanelHeaderControl(QDockWidget *dock, QWidget *panel,
                                       QWidget *header,
                                       const WorkspacePanelOptions &options,
                                       HeaderControlAttachment &attachment);
    void detachPanelHeaderControl(QWidget *header,
                                  HeaderControlAttachment &attachment);
    void detachWorkspaceHeaderControls();
    void detachWorkspacePanels();
    [[nodiscard]] WorkspaceUiProvider *activeWorkspaceUiProvider() const;
    void setDockTitle(QDockWidget *dock, const QString &title);

    ComponentManager *m_components = nullptr;
    WorkspaceComponentRegistry m_componentRegistry;
    std::vector<std::unique_ptr<WorkspaceRecord>> m_workspaces;
    QStringList m_pendingLogs;
    QString m_activePage;
    QString m_pageBeforeComponentManagement;
    QListWidget *m_navigation = nullptr;
    QStackedWidget *m_stack = nullptr;
    QGridLayout *m_homeGrid = nullptr;
    QGridLayout *m_componentGrid = nullptr;
    QLabel *m_homeEmpty = nullptr;
    QLabel *m_componentEmpty = nullptr;
    QLabel *m_pageTitle = nullptr;
    QLabel *m_pageSubtitle = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_buildInfo = nullptr;
    QFrame *m_globalBar = nullptr;
    QLabel *m_storePath = nullptr;
    QPushButton *m_closeWorkspace = nullptr;
    QPushButton *m_unloadComponent = nullptr;
    QTabBar *m_workspaceTabs = nullptr;
    QMenuBar *m_appMenuBar = nullptr;
    QToolButton *m_navigationToggle = nullptr;
    QToolButton *m_inspectorToggle = nullptr;
    QToolButton *m_outputToggle = nullptr;
    QToolButton *m_componentsToggle = nullptr;
    QToolButton *m_sleepButton = nullptr;
    QWidget *m_contextContributionHost = nullptr;
    QVBoxLayout *m_contextContributionLayout = nullptr;
    QLabel *m_contextFallback = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QPushButton *m_outputClear = nullptr;
    QStackedWidget *m_outputStack = nullptr;
    QWidget *m_outputTrendHost = nullptr;
    QVBoxLayout *m_outputTrendLayout = nullptr;
    QPointer<QLabel> m_outputTrendEmpty;
    QPointer<QWidget> m_attachedContextPanel;
    QPointer<QWidget> m_attachedContextPanelOwner;
    QPointer<QWidget> m_attachedInspectorPanel;
    QPointer<QWidget> m_attachedInspectorPanelOwner;
    QPointer<QWidget> m_attachedOutputPanel;
    QPointer<QWidget> m_attachedOutputPanelOwner;
    HeaderControlAttachment m_contextHeaderAttachment;
    HeaderControlAttachment m_inspectorHeaderAttachment;
    HeaderControlAttachment m_outputHeaderAttachment;
    QPointer<WorkspaceUiProvider> m_activeUiProvider;
    QListWidget *m_componentLibraryList = nullptr;
    QDockWidget *m_navigationDock = nullptr;
    QWidget *m_navigationDockHeader = nullptr;
    QDockWidget *m_componentLibraryDock = nullptr;
    QDockWidget *m_outputDock = nullptr;
    QWidget *m_outputDockHeader = nullptr;
    QDockWidget *m_inspectorDock = nullptr;
    QWidget *m_inspectorDockHeader = nullptr;
    QLabel *m_inspectorName = nullptr;
    QLabel *m_inspectorDescription = nullptr;
    QLabel *m_inspectorMeta = nullptr;
    QLabel *m_inspectorCapabilities = nullptr;
    QWidget *m_inspectorContributionHost = nullptr;
    QVBoxLayout *m_inspectorBodyLayout = nullptr;
    QVBoxLayout *m_inspectorContributionLayout = nullptr;
    QWidget *m_rootShell = nullptr;
    QHBoxLayout *m_rootLayout = nullptr;
    QMainWindow *m_workbenchWindow = nullptr;
    QMainWindow *m_centerWorkbenchWindow = nullptr;
    QFrame *m_activityRail = nullptr;
    QString m_pageLayoutContext;
    QHash<QString, WorkspaceLayoutProfile> m_pageLayoutProfiles;
    bool m_pageLayoutsReady = false;
    bool m_applyingPageLayout = false;
    quint64 m_pageLayoutRevision = 0;
    int m_homeCardColumns = 0;
    int m_componentCardColumns = 0;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
};

} // namespace serein::scope::app
