#pragma once

#include "TrendWidget.h"

#include <serein/scope/Packet.h>
#include <serein/scope/HttpRequestClient.h>
#include <serein/scope/PluginInterface.h>
#include <serein/scope/WorkspaceCanvas.h>
#include <serein/scope/WorkspaceComponents.h>

#include <QtCore/QPointer>
#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QVariantMap>
#include <QtWidgets/QWidget>

#include <limits>
#include <memory>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabBar;

namespace serein::scope::thermal {
class ThermalWorkbenchUi;
}

namespace serein::scope::t384 {
class T384Transport;
class T384Engine;
class T384CalibrationPage;
class T384CalibrationTransaction;
class ThermalView;

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
    void buildContextPanel();
    void buildInspectorPanel();
    void buildOutputPanel();
    void updateInspector();
    void updateCalibrationResult(const QJsonObject &result,
                                 const QString &detail,
                                 bool success, bool uncertain);
    void exportTrendPng();
    void exportTrendCsv();
    void updateDiagnostics(const QVariantMap &values);
    void updateContext();
    void beginAutomaticCalibrationWrite(const QByteArray &packet,
                                        const QByteArray &evidence);
    void beginReadOnlyCalibrationVerify(const QByteArray &packet,
                                        const QByteArray &evidence);
    void startCalibrationTransaction(bool verifyOnly);
    void finishCalibrationTransaction(int outcome, const QString &detail,
                                      const QJsonObject &result);
    bool beginCalibrationArchive(const QByteArray &packet,
                                 const QByteArray &evidence, QString *error);
    bool writeArchiveFile(const QString &name, const QByteArray &data,
                          QString *error) const;
    [[nodiscard]] QString currentProfile() const;
    IHostServices *m_host = nullptr;
    std::unique_ptr<thermal::ThermalWorkbenchUi> m_workbenchUi;
    WorkspaceUiProvider *m_uiProvider = nullptr;
    T384Engine *m_engine = nullptr;
    T384Transport *m_transport = nullptr;
    T384CalibrationPage *m_calibration = nullptr;
    HttpRequestClient *m_calibrationHttp = nullptr;
    T384CalibrationTransaction *m_calibrationTransaction = nullptr;
    WorkspaceCanvas *m_canvas = nullptr;
    QWidget *m_contextPanel = nullptr;
    QWidget *m_inspectorPanel = nullptr;
    QWidget *m_outputPanel = nullptr;
    QTabBar *m_outputTabs = nullptr;
    QStackedWidget *m_outputStack = nullptr;
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
    QLabel *m_inspectorIdentity = nullptr;
    QLabel *m_inspectorProfile = nullptr;
    QLabel *m_inspectorTemperature = nullptr;
    QLabel *m_inspectorRaw = nullptr;
    QLabel *m_inspectorSourceFps = nullptr;
    QLabel *m_inspectorStreamFps = nullptr;
    QLabel *m_inspectorCompleteFrames = nullptr;
    QLabel *m_inspectorSequenceGaps = nullptr;
    QLabel *m_inspectorLink = nullptr;
    QLabel *m_calibrationResult = nullptr;
    QLabel *m_calibrationArchive = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QVariantMap m_lastDiagnostics;
    QByteArray m_pendingCalibrationPacket;
    QByteArray m_pendingCalibrationEvidence;
    QString m_calibrationSessionPath;
    bool m_calibrationAutoWritePending = false;
    qint64 m_bytesReceived = 0;
    qint64 m_framesReceived = 0;
    qint64 m_firstSampleTimestampUs = 0;
    quint32 m_lastFrameSequence = 0;
    quint64 m_hostSequenceGaps = 0;
    double m_lastCenterTemperature = std::numeric_limits<double>::quiet_NaN();
    double m_lastCenterRaw = std::numeric_limits<double>::quiet_NaN();
    QVector<TrendSample> m_trendHistory;
    TrendWidget *m_outputTrend = nullptr;
    QList<QPointer<ThermalView>> m_thermals;
    QList<QPointer<TrendWidget>> m_trends;
};
}
