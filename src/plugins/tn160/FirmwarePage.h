#pragma once

#include <serein/scope/BackendIoTaskGroup.h>

#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace serein::scope {
class IHostServices;
}

namespace serein::scope::tn160 {

class FirmwarePage final : public QWidget {
    Q_OBJECT

public:
    explicit FirmwarePage(IHostServices *host, QWidget *parent = nullptr);
    void shutdown();
    Q_INVOKABLE QString firmwarePath() const;
    Q_INVOKABLE QString firmwareStatus() const;
    Q_INVOKABLE bool monitoring() const;
    [[nodiscard]] QWidget *contextPanel() const noexcept;

public slots:
    void chooseFirmware();
    void toggleMonitoring();

signals:
    void firmwareStateChanged(const QString &path, const QString &status, bool monitoring);

private slots:
    void scanVolumes();

private:
    void appendLog(const QString &message);
    void setMonitoring(bool active);
    void flashVolume(const QString &rootPath);

    IHostServices *m_host;
    BackendIoTaskGroup m_ioTasks;
    QString m_firmwarePath;
    QSet<QString> m_baselineVolumes;
    QSet<QString> m_attemptedVolumes;
    QTimer m_timer;
    QLabel *m_status = nullptr;
    QLabel *m_contextPath = nullptr;
    QLabel *m_contextStatus = nullptr;
    QWidget *m_contextPanel = nullptr;
    QPushButton *m_monitor = nullptr;
    QPlainTextEdit *m_log = nullptr;
};

} // namespace serein::scope::tn160
