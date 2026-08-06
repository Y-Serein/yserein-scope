#include "FirmwarePage.h"

#include <serein/scope/PluginInterface.h>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QStorageInfo>
#include <QtCore/QThread>
#include <QtCore/QTime>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>

namespace serein::scope::tn160 {

FirmwarePage::FirmwarePage(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host)
{
    // The host's right context panel owns the visible actions. This hidden
    // state owner keeps UF2 selection and the monitoring timer inside the
    // firmware page, so the panel cannot bypass its safety checks.
    auto *transportState = new QWidget(this);
    transportState->setObjectName(QStringLiteral("tn160FirmwareTransportState"));
    transportState->hide();
    auto *choose = new QPushButton(tr("选择 UF2"), transportState);
    m_monitor = new QPushButton(tr("开始监听"), transportState);
    m_monitor->setProperty("kind", QStringLiteral("primary"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(10);

    auto *notice = new QFrame;
    notice->setObjectName(QStringLiteral("panel"));
    auto *noticeLayout = new QVBoxLayout(notice);
    auto *title = new QLabel(tr("批量 BOOTSEL 烧录"));
    title->setObjectName(QStringLiteral("cardTitle"));
    auto *text = new QLabel(tr("监听开始后，只处理新出现、可写且容量小于 512 MiB 的卷；系统盘和监听前已经存在的卷不会写入。完成后继续等待下一台设备。"));
    text->setObjectName(QStringLiteral("pageSubtitle"));
    text->setWordWrap(true);
    m_status = new QLabel(tr("等待选择 UF2"));
    m_status->setObjectName(QStringLiteral("statusPill"));
    noticeLayout->addWidget(title);
    noticeLayout->addWidget(text);
    noticeLayout->addWidget(m_status, 0, Qt::AlignLeft);
    root->addWidget(notice);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(tr("检测、写入和校验结果显示在这里"));
    root->addWidget(m_log, 1);

    connect(choose, &QPushButton::clicked, this, &FirmwarePage::chooseFirmware);
    connect(m_monitor, &QPushButton::clicked, this, &FirmwarePage::toggleMonitoring);
    connect(&m_timer, &QTimer::timeout, this, &FirmwarePage::scanVolumes);
    m_timer.setInterval(500);
}

void FirmwarePage::shutdown()
{
    setMonitoring(false);
}

QString FirmwarePage::firmwarePath() const
{
    return m_firmwarePath;
}

QString FirmwarePage::firmwareStatus() const
{
    return m_status ? m_status->text() : QString();
}

bool FirmwarePage::monitoring() const
{
    return m_timer.isActive();
}

void FirmwarePage::chooseFirmware()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择 UF2 固件"), QString(), tr("UF2 固件 (*.uf2)"));
    if (path.isEmpty()) {
        return;
    }
    m_firmwarePath = path;
    m_status->setText(tr("固件已就绪"));
    appendLog(tr("选择固件：%1（%2 bytes）")
                  .arg(QDir::toNativeSeparators(path))
                  .arg(QFileInfo(path).size()));
    emit firmwareStateChanged(m_firmwarePath, m_status->text(), monitoring());
}

void FirmwarePage::toggleMonitoring()
{
    if (m_timer.isActive()) {
        setMonitoring(false);
        return;
    }
    if (m_firmwarePath.isEmpty() || !QFileInfo::exists(m_firmwarePath)) {
        appendLog(tr("错误：请先选择有效的 UF2 固件"));
        return;
    }
    m_baselineVolumes.clear();
    m_attemptedVolumes.clear();
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (storage.isValid() && storage.isReady()) {
            m_baselineVolumes.insert(QDir::cleanPath(storage.rootPath()).toCaseFolded());
        }
    }
    setMonitoring(true);
    appendLog(tr("开始监听；已保护当前存在的 %1 个卷").arg(m_baselineVolumes.size()));
}

void FirmwarePage::setMonitoring(const bool active)
{
    if (active) {
        m_timer.start();
        m_monitor->setText(tr("停止监听"));
        m_monitor->setProperty("kind", QStringLiteral("warning"));
        m_status->setText(tr("监听中 · 请连接 BOOTSEL 设备"));
    } else {
        m_timer.stop();
        m_monitor->setText(tr("2. 开始监听设备"));
        m_monitor->setProperty("kind", QStringLiteral("primary"));
        if (!m_firmwarePath.isEmpty()) {
            m_status->setText(tr("监听已停止"));
        }
    }
    m_monitor->style()->unpolish(m_monitor);
    m_monitor->style()->polish(m_monitor);
    emit firmwareStateChanged(m_firmwarePath, m_status->text(), active);
}

void FirmwarePage::scanVolumes()
{
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady() || storage.isReadOnly()) {
            continue;
        }
        const QString root = QDir::cleanPath(storage.rootPath());
        const QString key = root.toCaseFolded();
        if (m_baselineVolumes.contains(key) || m_attemptedVolumes.contains(key)) {
            continue;
        }
        const qint64 capacity = storage.bytesTotal();
        if (root == QDir::rootPath() || capacity < 1024 * 1024
            || capacity >= 512LL * 1024 * 1024) {
            continue;
        }
        m_attemptedVolumes.insert(key);
        appendLog(tr("检测到候选卷：%1 · %2 · %3 MiB")
                      .arg(QDir::toNativeSeparators(root), storage.displayName())
                      .arg(capacity / (1024 * 1024)));
        flashVolume(root);
    }
}

bool FirmwarePage::flashVolume(const QString &rootPath)
{
    QFile source(m_firmwarePath);
    if (!source.open(QIODevice::ReadOnly)) {
        appendLog(tr("错误：无法读取固件：%1").arg(source.errorString()));
        return false;
    }
    const QByteArray firmware = source.readAll();
    const QString destinationPath = QDir(rootPath).filePath(QStringLiteral("flash.uf2"));
    bool succeeded = false;
    for (int attempt = 1; attempt <= 3 && !succeeded; ++attempt) {
        QFile destination(destinationPath);
        if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendLog(tr("尝试 %1/3 写入失败：%2").arg(attempt).arg(destination.errorString()));
            QThread::msleep(500);
            continue;
        }
        const qint64 written = destination.write(firmware);
        destination.flush();
        destination.close();
        if (written != firmware.size()) {
            appendLog(tr("尝试 %1/3 写入不完整：%2 / %3 bytes")
                          .arg(attempt).arg(written).arg(firmware.size()));
            QThread::msleep(500);
            continue;
        }
        QThread::msleep(100);
        if (!QFileInfo::exists(destinationPath)) {
            appendLog(tr("写入完成后设备已复位，盘符消失 · 视为成功"));
            succeeded = true;
            break;
        }
        const qint64 finalSize = QFileInfo(destinationPath).size();
        if (finalSize == firmware.size()) {
            succeeded = true;
        } else {
            appendLog(tr("尝试 %1/3 大小校验失败：%2 / %3 bytes")
                          .arg(attempt).arg(finalSize).arg(firmware.size()));
            QThread::msleep(500);
        }
    }
    if (!succeeded) return false;
    appendLog(tr("烧录完成并通过大小校验：%1").arg(QDir::toNativeSeparators(rootPath)));
    m_status->setText(tr("上一台设备烧录成功 · 继续监听"));
    if (m_host) {
        m_host->setStatus(tr("TN160 · UF2 烧录成功"));
    }
    return true;
}

void FirmwarePage::appendLog(const QString &message)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2")
                               .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                    message));
    if (m_host) {
        m_host->log(message.startsWith(tr("错误")) ? QStringLiteral("error")
                                                    : QStringLiteral("info"),
                    QStringLiteral("TN160 Flash · %1").arg(message));
    }
}

} // namespace serein::scope::tn160
