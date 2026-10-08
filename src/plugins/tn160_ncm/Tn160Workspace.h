#pragma once

#include <serein/scope/Packet.h>
#include <serein/scope/PluginInterface.h>
#include <serein/scope/WorkspaceCanvas.h>
#include <serein/scope/WorkspaceComponents.h>

#include <QtCore/QVariantMap>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>

#include <memory>

class QLabel;
class QComboBox;
class QPushButton;
class QStackedWidget;
class QTabBar;

namespace serein::scope::thermal {
class ThermalWorkbenchUi;
}

namespace serein::scope::tn160ncm {


class FirmwarePage;
class ProductionCalibrationPage;
class ThermalView;
class TrendWidget;

class Tn160Workspace final : public QWidget, public IDeviceWorkspace,
                             public IWorkspaceUiProvider,
                             public IWorkspaceComponentProvider {
    Q_OBJECT
    Q_INTERFACES(serein::scope::IDeviceWorkspace
                 serein::scope::IWorkspaceUiProvider)

public:
    explicit Tn160Workspace(IHostServices *host, QWidget *parent = nullptr);
    ~Tn160Workspace() override;
    void shutdown() override;
    [[nodiscard]] WorkspaceUiProvider *workspaceUiProvider() override;
    [[nodiscard]] QList<WorkspaceComponentDefinition>
    componentDefinitions() const override;
    QWidget *createComponent(const QString &definitionId,
                              QWidget *parent) override;
    Q_INVOKABLE int currentPageIndex() const;

signals:
    void telemetryChanged(const QString &center, const QString &ntc,
                          const QString &centerAdu, const QString &vtemp,
                          const QString &fps);

private:
    QWidget *createLiveSurface();
    QWidget *createUnifiedContext();
    QWidget *createUnifiedInspector();
    QWidget *createUnifiedOutput();
    void handleLivePackets(const PacketList &packets);
    void applyWorkflowState(const QVariantMap &state);
    void revealLiveSurface();
    void exportTrendPng();
    void exportDiagnosticCsv();
    void generateFirmwareProfile();

    IHostServices *m_host = nullptr;
    std::unique_ptr<thermal::ThermalWorkbenchUi> m_workbenchUi;
    WorkspaceUiProvider *m_uiProvider = nullptr;
    FirmwarePage *m_firmware = nullptr;
    ProductionCalibrationPage *m_calibration = nullptr;
    QStackedWidget *m_centerStack = nullptr;
    QStackedWidget *m_outputStack = nullptr;
    QTabBar *m_outputTabs = nullptr;
    QWidget *m_contextPanel = nullptr;
    QWidget *m_inspectorPanel = nullptr;
    QWidget *m_outputPanel = nullptr;
    ThermalView *m_thermal = nullptr;
    TrendWidget *m_trend = nullptr;
    ::serein::scope::WorkspaceCanvas *m_liveCanvas = nullptr;
    QLabel *m_deviceIdentity = nullptr;
    QLabel *m_connectionState = nullptr;
    QLabel *m_centerMetric = nullptr;
    QLabel *m_ntcMetric = nullptr;
    QLabel *m_deltaNtcMetric = nullptr;
    QLabel *m_centerAduMetric = nullptr;
    QLabel *m_vtempMetric = nullptr;
    QLabel *m_fpsMetric = nullptr;
    QComboBox *m_endpointEdit = nullptr;
    QPushButton *m_scanButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QVector<SamplePacket> m_diagnosticHistory;
    QString m_historySerial;
    qint64 m_firstSampleTimestampUs = 0;
    qint64 m_previousSampleTimestampUs = 0;
    QList<QPointer<ThermalView>> m_componentThermals;
    QList<QPointer<TrendWidget>> m_componentTrends;
};

} // namespace serein::scope::tn160ncm
