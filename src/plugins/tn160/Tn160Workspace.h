#pragma once

#include <serein/scope/PluginInterface.h>
#include <serein/scope/WorkspaceComponents.h>

#include <QtWidgets/QWidget>

class QStackedWidget;
class QTabBar;

namespace serein::scope::tn160 {

class CalibrationPage;
class FirmwarePage;
class LivePage;
class ProductionCalibrationPage;

class Tn160Workspace final : public QWidget, public IDeviceWorkspace,
                             public IWorkspaceUiProvider,
                             public IWorkspaceComponentProvider {
    Q_OBJECT
    Q_INTERFACES(serein::scope::IDeviceWorkspace
                 serein::scope::IWorkspaceUiProvider)

public:
    explicit Tn160Workspace(IHostServices *host, QWidget *parent = nullptr);
    ~Tn160Workspace() override = default;
    void shutdown() override;
    [[nodiscard]] WorkspaceUiProvider *workspaceUiProvider() override;
    [[nodiscard]] QList<WorkspaceComponentDefinition>
    componentDefinitions() const override;
    QWidget *createComponent(const QString &definitionId,
                              QWidget *parent) override;

    Q_INVOKABLE int currentPageIndex() const;

signals:
    void pageAboutToChange(int previousIndex, int nextIndex);

private slots:
    void selectPage(int index);

private:
    IHostServices *m_host;
    WorkspaceUiProvider *m_uiProvider = nullptr;
    QTabBar *m_tabs = nullptr;
    QStackedWidget *m_stack = nullptr;
    FirmwarePage *m_firmware = nullptr;
    LivePage *m_live = nullptr;
    CalibrationPage *m_calibrate = nullptr;
    CalibrationPage *m_dump = nullptr;
    ProductionCalibrationPage *m_production = nullptr;
};

} // namespace serein::scope::tn160
