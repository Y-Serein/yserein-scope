#pragma once

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
    bool flashVolume(const QString &rootPath);

    IHostServices *m_host;
    QString m_firmwarePath;
    QSet<QString> m_baselineVolumes;
    QSet<QString> m_attemptedVolumes;
    QTimer m_timer;
    QLabel *m_status = nullptr;
    QPushButton *m_monitor = nullptr;
    QPlainTextEdit *m_log = nullptr;
};

} // namespace serein::scope::tn160
