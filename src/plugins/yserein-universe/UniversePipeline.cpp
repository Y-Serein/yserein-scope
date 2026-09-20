#include "UniversePipeline.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>

namespace serein::scope::yserein_universe {

UniversePipeline::UniversePipeline(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &UniversePipeline::onReadyRead);
    connect(&m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &UniversePipeline::onFinished);
}

UniversePipeline::~UniversePipeline()
{
    stop();
}

bool UniversePipeline::start(const QString &projectRoot, RunMode mode)
{
    if (isRunning()) return false;

    m_projectRoot = QDir::cleanPath(projectRoot);
    m_mode = mode;
    m_outputLines.clear();
    m_incoming.clear();

    const QString script =
        QDir(m_projectRoot).filePath(QStringLiteral("scripts") +
                                     QDir::separator() +
                                     QStringLiteral("run_mission.ps1"));
    if (!QFileInfo::exists(script)) {
        emit statusChanged(tr("找不到 %1").arg(script));
        emit finished(false, tr("缺少 run_mission.ps1，请核对项目根目录"));
        return false;
    }

    m_process.setProgram(QStringLiteral("powershell.exe"));
    m_process.setArguments({
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script,
        m_mode == RunMode::SmokeTest ? QStringLiteral("-SmokeTestOpenSpace")
                                     : QStringLiteral("-LaunchOpenSpace"),
    });

    m_state = State::Running;
    emit statusChanged(m_mode == RunMode::SmokeTest
                           ? tr("闭环运行中（自动验证 OpenSpace）…")
                           : tr("闭环运行中（等待 OpenSpace 关闭）…"));
    emit lineOutput(tr("$ powershell.exe %1")
                        .arg(m_process.arguments().join(QLatin1Char(' '))));

    m_process.start();
    if (!m_process.waitForStarted(15000)) {
        m_state = State::Idle;
        emit statusChanged(tr("无法启动 powershell.exe"));
        emit finished(false, tr("无法启动 powershell.exe"));
        return false;
    }
    return true;
}

void UniversePipeline::stop()
{
    if (!isRunning()) return;

    // PowerShell 会启动 GMAT / Python / OpenSpace 子进程，必须按进程树结束。
    QProcess killer;
    killer.setProgram(QStringLiteral("taskkill"));
    killer.setArguments({QStringLiteral("/F"), QStringLiteral("/T"),
                         QStringLiteral("/PID"),
                         QString::number(m_process.processId())});
    killer.start();
    killer.waitForFinished(10000);

    m_process.kill();
    m_process.waitForFinished(5000);
    m_state = State::Idle;
    emit statusChanged(tr("已停止"));
}

void UniversePipeline::onReadyRead()
{
    m_incoming += QString::fromLocal8Bit(m_process.readAll());
    while (true) {
        const qsizetype nl = m_incoming.indexOf(QLatin1Char('\n'));
        if (nl < 0) break;
        const QString line = m_incoming.left(nl).trimmed();
        m_incoming.remove(0, nl + 1);
        appendLine(line);
    }
}

void UniversePipeline::onFinished(int exitCode, QProcess::ExitStatus)
{
    if (!m_incoming.isEmpty()) {
        appendLine(m_incoming.trimmed());
        m_incoming.clear();
    }
    m_state = State::Idle;

    bool passed = (exitCode == 0);
    QString maxError;

    static const QRegularExpression errorRe(
        QStringLiteral("maximum_error_m=([0-9.eE+-]+)"));
    for (const QString &line : m_outputLines) {
        const QRegularExpressionMatch match = errorRe.match(line);
        if (match.hasMatch() && maxError.isEmpty())
            maxError = match.captured(1);
        if (line.contains(QStringLiteral("PASS: pipeline outputs generated and validated.")))
            passed = true;
        if (line.contains(QStringLiteral("FAILED:")))
            passed = false;
    }

    QString summary;
    if (passed) {
        summary = tr("PASS：闭环完成");
        if (!maxError.isEmpty())
            summary += tr("，最大轨迹误差 %1 m").arg(maxError);
    } else {
        summary = tr("FAILED：退出码 %1").arg(exitCode);
        for (const QString &line : m_outputLines) {
            if (line.contains(QStringLiteral("FAILED:"))) {
                summary += tr("，%1").arg(line);
                break;
            }
        }
    }

    emit statusChanged(passed ? tr("闭环完成") : tr("闭环失败"));
    emit finished(passed, summary);
}

void UniversePipeline::appendLine(const QString &line)
{
    if (line.isEmpty()) return;
    m_outputLines.append(line);
    emit lineOutput(line);
}

} // namespace serein::scope::yserein_universe
