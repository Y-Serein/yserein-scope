#pragma once

#include <serein/scope/Packet.h>
#include <serein/scope/PluginInterface.h>
#include <serein/scope/WorkspaceCanvas.h>
#include <serein/scope/WorkspaceComponents.h>

#include <QtCore/QPointer>
#include <QtCore/QVariantMap>
#include <QtWidgets/QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace serein::scope::t384 {
class T384Transport;
class T384Engine;
class T384CalibrationPage;
class ThermalView;
class TrendWidget;

class T384Workspace final : public QWidget, public IDeviceWorkspace,
                            public IWorkspaceUiProvider,
                            public IWorkspaceComponentProvider {
    Q_OBJECT
    Q_INTERFACES(serein::scope::IDeviceWorkspace
                 serein::scope::IWorkspaceUiProvider
                 serein::scope::IWorkspaceComponentProvider)
public:
    explicit T384Workspace(IHostServices *host, QWidget *parent = nullptr);
    ~T384Workspace() override;
    void shutdown() override;
    WorkspaceUiProvider *workspaceUiProvider() override { return m_uiProvider; }
    QList<WorkspaceComponentDefinition> componentDefinitions() const override;
    QWidget *createComponent(const QString &definitionId, QWidget *parent) override;
    Q_INVOKABLE bool activateLibraryComponent(const QString &definitionId);

private slots:
    void handlePackets(const PacketList &packets);

private:
    void appendLog(const QString &level, const QString &message);
    void updateDiagnostics(const QVariantMap &values);
    void updateContext();
    IHostServices *m_host = nullptr;
    WorkspaceUiProvider *m_uiProvider = nullptr;
    T384Engine *m_engine = nullptr;
    T384Transport *m_transport = nullptr;
    T384CalibrationPage *m_calibration = nullptr;
    WorkspaceCanvas *m_canvas = nullptr;
    QWidget *m_contextPanel = nullptr;
    QWidget *m_outputPanel = nullptr;
    QLineEdit *m_endpoint = nullptr;
    QPushButton *m_connect = nullptr;
    QPushButton *m_stop = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_identity = nullptr;
    QLabel *m_format = nullptr;
    QLabel *m_pipeline = nullptr;
    QLabel *m_stream = nullptr;
    QLabel *m_traffic = nullptr;
    QLabel *m_error = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QVariantMap m_lastDiagnostics;
    qint64 m_bytesReceived = 0;
    qint64 m_framesReceived = 0;
    QList<QPointer<ThermalView>> m_thermals;
    QList<QPointer<TrendWidget>> m_trends;
};
}
