#include "YsereinUniverseWorkspace.h"

#include "UniversePipeline.h"

#include <serein/scope/WorkspaceCanvas.h>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <utility>

namespace serein::scope::yserein_universe {

YsereinUniverseWorkspace::YsereinUniverseWorkspace(IHostServices *host, QWidget *parent)
    : QWidget(parent)
    , m_host(host)
{
    m_pipeline = new UniversePipeline(this);
    connect(m_pipeline, &UniversePipeline::lineOutput, this, &YsereinUniverseWorkspace::appendLog);
    connect(m_pipeline, &UniversePipeline::statusChanged, this, &YsereinUniverseWorkspace::onPipelineStatus);
    connect(m_pipeline, &UniversePipeline::finished, this, &YsereinUniverseWorkspace::onPipelineFinished);

    buildUi();
    loadSettings();

    m_uiProvider = new WorkspaceUiProvider(this);
    m_uiProvider->setPanelSides(WorkspacePanelSide::Left, WorkspacePanelSide::Right);
    WorkspaceUiContext uiContext;
    uiContext.id = QStringLiteral("yserein-universe/pipeline");
    uiContext.layoutProfile = WorkspaceLayoutProfile::Standard;
    uiContext.outputPanel = {m_outputLog, tr("YSerein Universe 运行日志"), nullptr, {}};
    m_uiProvider->setContext(std::move(uiContext));

    updateControlState();
    if (m_host) m_host->setStatus(tr("YSerein Universe · 闭环调用工具"));
}

void YsereinUniverseWorkspace::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    // 配置卡：项目根目录指向 Serein-Universe 部署根。
    auto *configSurface = new ComponentSurface(tr("管道配置"), this);
    auto *configLayout = new QVBoxLayout(configSurface->contentHost());
    auto *pathForm = new QFormLayout;
    m_projectRootEdit = new QLineEdit(configSurface->contentHost());
    m_projectRootEdit->setPlaceholderText(
        tr("例如 C:\\Serein_Y\\Project\\Serein-Universe"));
    pathForm->addRow(tr("项目根目录"), m_projectRootEdit);
    auto *configButtons = new QHBoxLayout;
    auto *browseButton = new QPushButton(tr("浏览…"), configSurface->contentHost());
    auto *saveButton = new QPushButton(tr("保存配置"), configSurface->contentHost());
    configButtons->addWidget(browseButton);
    configButtons->addWidget(saveButton);
    configButtons->addStretch();
    configLayout->addLayout(pathForm);
    configLayout->addLayout(configButtons);
    connect(browseButton, &QPushButton::clicked, this, &YsereinUniverseWorkspace::browseProjectRoot);
    connect(saveButton, &QPushButton::clicked, this, &YsereinUniverseWorkspace::saveProjectRoot);
    root->addWidget(configSurface);

    // 运行卡：一键启动全链闭环。
    auto *runSurface = new ComponentSurface(tr("闭环运行"), this);
    auto *runLayout = new QVBoxLayout(runSurface->contentHost());
    m_runButton = new QPushButton(tr("运行完整闭环（自动验证 OpenSpace）"), runSurface->contentHost());
    m_launchButton = new QPushButton(tr("运行并保持 OpenSpace 打开"), runSurface->contentHost());
    m_stopButton = new QPushButton(tr("停止"), runSurface->contentHost());
    runLayout->addWidget(m_runButton);
    runLayout->addWidget(m_launchButton);
    runLayout->addWidget(m_stopButton);
    connect(m_runButton, &QPushButton::clicked, this, &YsereinUniverseWorkspace::runClosedLoop);
    connect(m_launchButton, &QPushButton::clicked, this, &YsereinUniverseWorkspace::runLaunchAndKeep);
    connect(m_stopButton, &QPushButton::clicked, this, &YsereinUniverseWorkspace::stopRun);
    root->addWidget(runSurface);

    // 状态卡：展示运行状态与最近一次结果。
    auto *statusSurface = new ComponentSurface(tr("运行状态"), this);
    auto *statusLayout = new QVBoxLayout(statusSurface->contentHost());
    m_statusLabel = new QLabel(tr("空闲"), statusSurface->contentHost());
    m_statusLabel->setWordWrap(true);
    m_resultLabel = new QLabel(tr("尚未运行"), statusSurface->contentHost());
    m_resultLabel->setWordWrap(true);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addWidget(m_resultLabel);
    root->addWidget(statusSurface);
    root->addStretch();

    // 输出面板内容：独立子控件，交给宿主 Output 槽挂载，不占用中央布局。
    m_outputLog = new QPlainTextEdit(this);
    m_outputLog->setObjectName(QStringLiteral("ysereinUniverseOutputLog"));
    m_outputLog->setReadOnly(true);
    m_outputLog->setMaximumBlockCount(2000);
}

void YsereinUniverseWorkspace::shutdown()
{
    if (m_pipeline) m_pipeline->stop();
    if (m_host) m_host->log(QStringLiteral("info"), tr("YSerein Universe 闭环调用工具已关闭"));
    m_host = nullptr;
}

WorkspaceUiProvider *YsereinUniverseWorkspace::workspaceUiProvider()
{
    return m_uiProvider;
}

QString YsereinUniverseWorkspace::projectRoot() const
{
    return m_projectRootEdit->text().trimmed();
}

QString YsereinUniverseWorkspace::defaultProjectRoot() const
{
    const QString env = qEnvironmentVariable("SEREIN_UNIVERSE_ROOT");
    if (!env.trimmed().isEmpty()) return env.trimmed();
    return QStringLiteral("C:\\Serein_Y\\Project\\Serein-Universe");
}

void YsereinUniverseWorkspace::loadSettings()
{
    QSettings settings;
    const QString stored =
        settings.value(QStringLiteral("yserein-universe/projectRoot")).toString();
    m_projectRootEdit->setText(stored.isEmpty() ? defaultProjectRoot() : stored);
}

void YsereinUniverseWorkspace::saveProjectRoot()
{
    QSettings settings;
    settings.setValue(QStringLiteral("yserein-universe/projectRoot"), projectRoot());
    setStatus(tr("配置已保存"));
    if (m_host) m_host->log(QStringLiteral("info"),
                            tr("YSerein Universe 项目根目录已保存：%1").arg(projectRoot()));
}

void YsereinUniverseWorkspace::browseProjectRoot()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择 Serein-Universe 项目根目录"), projectRoot());
    if (!dir.isEmpty()) {
        m_projectRootEdit->setText(QDir::toNativeSeparators(dir));
        saveProjectRoot();
    }
}

void YsereinUniverseWorkspace::runClosedLoop()
{
    if (m_pipeline->start(projectRoot(), RunMode::SmokeTest)) updateControlState();
}

void YsereinUniverseWorkspace::runLaunchAndKeep()
{
    if (m_pipeline->start(projectRoot(), RunMode::LaunchAndKeep)) updateControlState();
}

void YsereinUniverseWorkspace::stopRun()
{
    m_pipeline->stop();
    updateControlState();
}

void YsereinUniverseWorkspace::appendLog(const QString &line)
{
    m_outputLog->appendPlainText(line);
}

void YsereinUniverseWorkspace::onPipelineStatus(const QString &message)
{
    setStatus(message);
}

void YsereinUniverseWorkspace::onPipelineFinished(bool passed, const QString &summary)
{
    m_resultLabel->setText(tr("结果：%1").arg(summary));
    m_resultLabel->setStyleSheet(passed ? QStringLiteral("color:#2e7d32;")
                                        : QStringLiteral("color:#c62828;"));
    setStatus(passed ? tr("闭环完成") : tr("闭环失败"));
    if (m_host) m_host->log(passed ? QStringLiteral("info") : QStringLiteral("error"), summary);
    updateControlState();
}

void YsereinUniverseWorkspace::setStatus(const QString &message)
{
    m_statusLabel->setText(message);
    if (m_host) m_host->setStatus(message);
}

void YsereinUniverseWorkspace::updateControlState()
{
    const bool running = m_pipeline->isRunning();
    m_runButton->setEnabled(!running);
    m_launchButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
}

} // namespace serein::scope::yserein_universe
