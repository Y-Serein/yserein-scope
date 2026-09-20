#pragma once

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace serein::scope::yserein_universe {

enum class RunMode {
    SmokeTest,      // 运行全链闭环并自动验证 OpenSpace 后关闭（等价 run_closed_loop.cmd）
    LaunchAndKeep   // 运行全链闭环并保持 OpenSpace 打开，等待用户手动关闭
};

class UniversePipeline final : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Running };
    Q_ENUM(State)

    explicit UniversePipeline(QObject *parent = nullptr);
    ~UniversePipeline() override;

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] bool isRunning() const noexcept { return m_state == State::Running; }

    bool start(const QString &projectRoot, RunMode mode);
    void stop();

signals:
    void lineOutput(const QString &line);
    void statusChanged(const QString &message);
    void finished(bool passed, const QString &summary);

private:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void appendLine(const QString &line);

    QProcess m_process;
    State m_state = State::Idle;
    QString m_projectRoot;
    QString m_incoming;
    QStringList m_outputLines;
    RunMode m_mode = RunMode::SmokeTest;
};

} // namespace serein::scope::yserein_universe
