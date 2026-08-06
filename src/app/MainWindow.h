#pragma once

#include <serein/scope/PluginInterface.h>

#include <QtCore/QPointer>
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
class QComboBox;
class QAbstractSpinBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QMenuBar;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QSpinBox;
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
    void importComponent();
    void refreshComponentUi();
    void resetDockLayout();
    void refreshConnectionPanel();
    void scanConnectionPorts();
    void toggleConnection();
    void toggleDarkMode();
    void updateConnectionState(const QString &state, const QString &text);
    void updateCalibrationState(const QString &state, const QString &text);
    void updateFirmwareState(const QString &path, const QString &status, bool monitoring);
    void updateRawCaptureState(const QString &state, const QString &text);
    void updateTn160Telemetry(const QString &center, const QString &ntc,
                              const QString &centerAdu, const QString &vtemp,
                              const QString &fps);

private:
    struct WorkspaceRecord {
        QString componentId;
        QPointer<QWidget> widget;
        int stackIndex = -1;
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
    WorkspaceRecord *findWorkspace(const QString &componentId);
    const WorkspaceRecord *findWorkspace(const QString &componentId) const;
    void updateInspector(const PluginMetadata *metadata);
    void selectNavigation(const QString &id);
    void applyDarkMode(bool enabled);
    QString componentPackageStartPath() const;
    void refreshComponentLibrary();
    void showLibraryComponent(const QString &componentId);
    void synchronizeContextPanels();
    void detachTn160Trend();
    QObject *connectionTargetForCurrentWorkspace() const;
    bool connectionTargetActive(QObject *target) const;
    void setConnectionConfigurationEnabled(bool canConnect, bool serial,
                                           bool calibration, bool locked);

    ComponentManager *m_components = nullptr;
    std::vector<std::unique_ptr<WorkspaceRecord>> m_workspaces;
    QStringList m_pendingLogs;
    QString m_activePage;
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
    QComboBox *m_connectionPort = nullptr;
    QLabel *m_connectionBaudLabel = nullptr;
    QSpinBox *m_connectionBaud = nullptr;
    QLabel *m_connectionTriggerBaudLabel = nullptr;
    QSpinBox *m_connectionTriggerBaud = nullptr;
    QLabel *m_connectionReadBaudLabel = nullptr;
    QSpinBox *m_connectionReadBaud = nullptr;
    QLabel *m_tnRawFrameLabel = nullptr;
    QSpinBox *m_tnRawFrameCount = nullptr;
    QPushButton *m_tnRawCapture = nullptr;
    QPushButton *m_connectionScan = nullptr;
    QPushButton *m_connectionToggle = nullptr;
    QLabel *m_connectionState = nullptr;
    QLabel *m_connectionEngineLabel = nullptr;
    QComboBox *m_connectionEngine = nullptr;
    QStackedWidget *m_connectionModeStack = nullptr;
    QPushButton *m_firmwareChoose = nullptr;
    QPushButton *m_firmwareMonitor = nullptr;
    QLabel *m_firmwarePath = nullptr;
    QLabel *m_firmwareState = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QStackedWidget *m_outputStack = nullptr;
    QWidget *m_outputTrendHost = nullptr;
    QVBoxLayout *m_outputTrendLayout = nullptr;
    QPointer<QLabel> m_outputTrendEmpty;
    QPointer<QWidget> m_attachedTn160Trend;
    QPointer<QWidget> m_attachedTn160TrendOwner;
    QListWidget *m_componentLibraryList = nullptr;
    QDockWidget *m_navigationDock = nullptr;
    QDockWidget *m_componentLibraryDock = nullptr;
    QDockWidget *m_outputDock = nullptr;
    QDockWidget *m_inspectorDock = nullptr;
    QLabel *m_inspectorName = nullptr;
    QLabel *m_inspectorDescription = nullptr;
    QLabel *m_inspectorMeta = nullptr;
    QLabel *m_inspectorCapabilities = nullptr;
    QWidget *m_liveMetrics = nullptr;
    QLabel *m_liveCenter = nullptr;
    QLabel *m_liveNtc = nullptr;
    QLabel *m_liveCenterAdu = nullptr;
    QLabel *m_liveVtemp = nullptr;
    QLabel *m_liveFps = nullptr;
    QWidget *m_rootShell = nullptr;
    QHBoxLayout *m_rootLayout = nullptr;
    QMainWindow *m_workbenchWindow = nullptr;
    QMainWindow *m_centerWorkbenchWindow = nullptr;
    QFrame *m_activityRail = nullptr;
    int m_homeCardColumns = 0;
    int m_componentCardColumns = 0;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
};

} // namespace serein::scope::app
