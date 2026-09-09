#pragma once

#include <serein/scope/BackendIoTaskGroup.h>

#include <QtCore/QSet>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;

namespace serein::scope {
class IHostServices;
}

namespace serein::scope::tn160ncm {

class FirmwarePage final : public QWidget {
    Q_OBJECT

public:
    explicit FirmwarePage(IHostServices *host, QWidget *parent = nullptr);
    void shutdown();
    Q_INVOKABLE QString firmwarePath() const;
    Q_INVOKABLE QString firmwareStatus() const;
    Q_INVOKABLE bool monitoring() const;
    [[nodiscard]] bool autoConnectAfterFlash() const noexcept;
    [[nodiscard]] QWidget *contextPanel() const noexcept;

public slots:
    void chooseFirmware();
    void toggleMonitoring();
    void setNextDeviceBlocked(bool blocked);

signals:
    void firmwareStateChanged(const QString &path, const QString &status, bool monitoring);
    void firmwareFlashSucceeded(const QString &firmwarePath,
                                const QString &firmwareSha256,
                                const QString &bootselRoot);

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
    QPushButton *m_choose = nullptr;
    QPushButton *m_monitor = nullptr;
    QCheckBox *m_autoConnect = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QSet<QString> m_inFlightVolumes;
    int m_successfulFlashes = 0;
    bool m_nextDeviceBlocked = false;
};

} // namespace serein::scope::tn160ncm
