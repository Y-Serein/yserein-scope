#pragma once

#include <serein/scope/PluginInterface.h>

#include <QtWidgets/QWidget>
#include <QtCore/QString>

class QLineEdit;
class QPushButton;
class QLabel;
class QPlainTextEdit;

namespace serein::scope::yserein_universe {

class UniversePipeline;

class YsereinUniverseWorkspace final : public QWidget, public IDeviceWorkspace,
                                       public IWorkspaceUiProvider {
    Q_OBJECT
    Q_INTERFACES(serein::scope::IDeviceWorkspace
                 serein::scope::IWorkspaceUiProvider)

public:
    explicit YsereinUniverseWorkspace(IHostServices *host, QWidget *parent = nullptr);
    ~YsereinUniverseWorkspace() override = default;

    void shutdown() override;
    [[nodiscard]] WorkspaceUiProvider *workspaceUiProvider() override;

public slots:
    void browseProjectRoot();
    void saveProjectRoot();
    void runClosedLoop();
    void runLaunchAndKeep();
    void stopRun();
    void appendLog(const QString &line);
    void onPipelineStatus(const QString &message);
    void onPipelineFinished(bool passed, const QString &summary);

private:
    void buildUi();
    QString projectRoot() const;
    QString defaultProjectRoot() const;
    void loadSettings();
    void saveSettings();
    void updateControlState();
    void setStatus(const QString &message);

    IHostServices *m_host = nullptr;
    UniversePipeline *m_pipeline = nullptr;
    WorkspaceUiProvider *m_uiProvider = nullptr;

    QLineEdit *m_projectRootEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QPushButton *m_launchButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_resultLabel = nullptr;
    QPlainTextEdit *m_outputLog = nullptr;
};

} // namespace serein::scope::yserein_universe
