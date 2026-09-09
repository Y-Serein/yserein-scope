#pragma once

#include "ComponentManager.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QPointer>
#include <QtCore/QVariantList>
#include <QtGui/QWindow>

#include <memory>
#include <vector>

class QTabBar;
class QWidget;

namespace serein::scope::app {

class QuickComponentModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        VersionRole,
        CategoryRole,
        CapabilitiesRole,
        AccentRole,
        LoadedRole,
        ErrorRole,
    };
    Q_ENUM(Role)

    explicit QuickComponentModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replace(QList<ComponentSnapshot> components);

private:
    QList<ComponentSnapshot> m_components;
};

class QuickWorkbenchController final : public QObject, public IHostServices {
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *components READ components CONSTANT)
    Q_PROPERTY(QVariantList workspaceTabs READ workspaceTabs NOTIFY workspaceTabsChanged)
    Q_PROPERTY(QString activePage READ activePage NOTIFY activePageChanged)
    Q_PROPERTY(QWindow *workspaceWindow READ workspaceWindow NOTIFY workspaceWindowChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logLinesChanged)
    Q_PROPERTY(QStringList ports READ ports NOTIFY connectionContextChanged)
    Q_PROPERTY(QString selectedPort READ selectedPort WRITE setSelectedPort
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(int baudRate READ baudRate WRITE setBaudRate
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(int triggerBaudRate READ triggerBaudRate WRITE setTriggerBaudRate
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(int readBaudRate READ readBaudRate WRITE setReadBaudRate
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(QString engineId READ engineId WRITE setEngineId
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(QString connectionState READ connectionState
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(QString connectionAction READ connectionAction
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(bool canConnect READ canConnect NOTIFY connectionContextChanged)
    Q_PROPERTY(bool connectionActive READ connectionActive
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(bool serialWorkspace READ serialWorkspace
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(bool calibrationContext READ calibrationContext
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(bool productionCalibrationContext READ productionCalibrationContext
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(bool firmwareContext READ firmwareContext
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(bool rawCaptureAvailable READ rawCaptureAvailable
                   NOTIFY connectionContextChanged)
    Q_PROPERTY(QString activeComponentName READ activeComponentName
                   NOTIFY inspectorChanged)
    Q_PROPERTY(QString activeComponentDescription READ activeComponentDescription
                   NOTIFY inspectorChanged)
    Q_PROPERTY(QString activeComponentMeta READ activeComponentMeta
                   NOTIFY inspectorChanged)
    Q_PROPERTY(QString activeComponentCapabilities READ activeComponentCapabilities
                   NOTIFY inspectorChanged)
    Q_PROPERTY(QString centerValue READ centerValue NOTIFY telemetryChanged)
    Q_PROPERTY(QString ntcValue READ ntcValue NOTIFY telemetryChanged)
    Q_PROPERTY(QString centerAduValue READ centerAduValue NOTIFY telemetryChanged)
    Q_PROPERTY(QString vtempValue READ vtempValue NOTIFY telemetryChanged)
    Q_PROPERTY(QString fpsValue READ fpsValue NOTIFY telemetryChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(bool navigationVisible READ navigationVisible WRITE setNavigationVisible
                   NOTIFY panelVisibilityChanged)
    Q_PROPERTY(bool inspectorVisible READ inspectorVisible WRITE setInspectorVisible
                   NOTIFY panelVisibilityChanged)
    Q_PROPERTY(bool outputVisible READ outputVisible WRITE setOutputVisible
                   NOTIFY panelVisibilityChanged)
    Q_PROPERTY(bool componentLibraryVisible READ componentLibraryVisible
                   WRITE setComponentLibraryVisible NOTIFY panelVisibilityChanged)

public:
    explicit QuickWorkbenchController(bool persistState = true,
                                      QObject *parent = nullptr);
    ~QuickWorkbenchController() override;

    [[nodiscard]] QAbstractListModel *components() noexcept;
    [[nodiscard]] QVariantList workspaceTabs() const;
    [[nodiscard]] QString activePage() const;
    [[nodiscard]] QWindow *workspaceWindow() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QStringList logLines() const;
    [[nodiscard]] QStringList ports() const;
    [[nodiscard]] QString selectedPort() const;
    [[nodiscard]] int baudRate() const noexcept;
    [[nodiscard]] int triggerBaudRate() const noexcept;
    [[nodiscard]] int readBaudRate() const noexcept;
    [[nodiscard]] QString engineId() const;
    [[nodiscard]] QString connectionState() const;
    [[nodiscard]] QString connectionAction() const;
    [[nodiscard]] bool canConnect() const;
    [[nodiscard]] bool connectionActive() const;
    [[nodiscard]] bool serialWorkspace() const;
    [[nodiscard]] bool calibrationContext() const;
    [[nodiscard]] bool productionCalibrationContext() const;
    [[nodiscard]] bool firmwareContext() const;
    [[nodiscard]] bool rawCaptureAvailable() const;
    [[nodiscard]] QString activeComponentName() const;
    [[nodiscard]] QString activeComponentDescription() const;
    [[nodiscard]] QString activeComponentMeta() const;
    [[nodiscard]] QString activeComponentCapabilities() const;
    [[nodiscard]] QString centerValue() const;
    [[nodiscard]] QString ntcValue() const;
    [[nodiscard]] QString centerAduValue() const;
    [[nodiscard]] QString vtempValue() const;
    [[nodiscard]] QString fpsValue() const;
    [[nodiscard]] bool darkMode() const noexcept;
    [[nodiscard]] bool navigationVisible() const noexcept;
    [[nodiscard]] bool inspectorVisible() const noexcept;
    [[nodiscard]] bool outputVisible() const noexcept;
    [[nodiscard]] bool componentLibraryVisible() const noexcept;

    Q_INVOKABLE void activatePage(const QString &pageId);
    Q_INVOKABLE bool openComponent(const QString &componentId);
    Q_INVOKABLE bool closeWorkspace(const QString &componentId);
    Q_INVOKABLE bool unloadComponent(const QString &componentId);
    Q_INVOKABLE bool importComponent(const QUrl &sourceUrl);
    Q_INVOKABLE void scanPorts();
    Q_INVOKABLE void toggleConnection();
    Q_INVOKABLE void startRawCapture(int frameCount);
    Q_INVOKABLE void chooseFirmware();
    Q_INVOKABLE void toggleFirmwareMonitoring();
    Q_INVOKABLE bool showLibraryComponent(const QString &componentId);
    Q_INVOKABLE QStringList availableLibraryComponents() const;
    Q_INVOKABLE QString libraryComponentTitle(const QString &componentId) const;
    Q_INVOKABLE void selectTemplate(int index);
    Q_INVOKABLE void resetPanels();

    void setSelectedPort(const QString &port);
    void setBaudRate(int baudRate);
    void setTriggerBaudRate(int baudRate);
    void setReadBaudRate(int baudRate);
    void setEngineId(const QString &engineId);
    void setDarkMode(bool enabled);
    void setNavigationVisible(bool visible);
    void setInspectorVisible(bool visible);
    void setOutputVisible(bool visible);
    void setComponentLibraryVisible(bool visible);

    void log(const QString &level, const QString &message) override;
    void setStatus(const QString &message) override;

signals:
    void workspaceTabsChanged();
    void activePageChanged();
    void workspaceWindowChanged();
    void statusTextChanged();
    void logLinesChanged();
    void connectionContextChanged();
    void inspectorChanged();
    void telemetryChanged();
    void darkModeChanged();
    void panelVisibilityChanged();
    void operationFailed(const QString &title, const QString &message);

private slots:
    void refreshComponents();
    void handleWorkspacePageChanged();
    void handleConnectionState(const QString &state, const QString &text);
    void handleTransferState(const QString &state, const QString &text);
    void handleRawCaptureState(const QString &state, const QString &text);
    void handleTelemetry(const QString &center, const QString &ntc,
                         const QString &centerAdu, const QString &vtemp,
                         const QString &fps);

private:
    struct WorkspaceRecord {
        QString componentId;
        QPointer<QWidget> widget;
        QPointer<QWindow> window;
    };

    void prepareComponents();
    void restoreInitialPage();
    void connectWorkspaceSignals(QWidget *workspace);
    void refreshConnectionContext();
    void applyDarkModeToWorkspace(QWidget *workspace) const;
    [[nodiscard]] WorkspaceRecord *findWorkspace(const QString &componentId);
    [[nodiscard]] const WorkspaceRecord *findWorkspace(const QString &componentId) const;
    [[nodiscard]] QWidget *activeWorkspace() const;
    [[nodiscard]] QObject *connectionTarget() const;
    [[nodiscard]] int tn160PageIndex() const;
    [[nodiscard]] bool targetActive(QObject *target) const;
    [[nodiscard]] QString componentDirectoryArgument() const;
    void setPanelFlag(bool &target, bool value);

    QuickComponentModel m_componentModel;
    std::unique_ptr<ComponentManager> m_components;
    std::vector<std::unique_ptr<WorkspaceRecord>> m_workspaces;
    QString m_activePage = QStringLiteral("__home__");
    QString m_statusText = QStringLiteral("Scope · 就绪");
    QStringList m_logLines;
    QStringList m_ports;
    QString m_selectedPort;
    QString m_engineId = QStringLiteral("delimited");
    QString m_connectionState = QStringLiteral("未打开可连接工作区");
    QString m_centerValue = QStringLiteral("--.-- °C");
    QString m_ntcValue = QStringLiteral("—");
    QString m_centerAduValue = QStringLiteral("—");
    QString m_vtempValue = QStringLiteral("—");
    QString m_fpsValue = QStringLiteral("—");
    int m_baudRate = 2000000;
    int m_triggerBaudRate = 2000000;
    int m_readBaudRate = 5000000;
    bool m_darkMode = false;
    bool m_navigationVisible = true;
    bool m_inspectorVisible = false;
    bool m_outputVisible = false;
    bool m_componentLibraryVisible = false;
};

} // namespace serein::scope::app
