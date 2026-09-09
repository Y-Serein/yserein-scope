#include "FirmwarePage.h"

#include <serein/scope/PluginInterface.h>

#include <QtCore/QDir>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QStorageInfo>
#include <QtCore/QThread>
#include <QtCore/QTime>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>

#include <memory>

namespace serein::scope::tn160ncm {

FirmwarePage::FirmwarePage(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host)
{
    m_contextPanel = new QWidget(this);
    m_contextPanel->setObjectName(QStringLiteral("workspaceContextContent"));
    auto *contextLayout = new QVBoxLayout(m_contextPanel);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    contextLayout->setSpacing(8);
    auto *hint = new QLabel(tr("持续监听 · 可连续烧录多台设备"), m_contextPanel);
    hint->setObjectName(QStringLiteral("tn160FirmwareHint"));
    hint->setProperty("textRole", QStringLiteral("pageSubtitle"));
    hint->setWordWrap(true);
    contextLayout->addWidget(hint);
    m_choose = new QPushButton(tr("1. 选择 UF2"), m_contextPanel);
    m_choose->setObjectName(QStringLiteral("tn160UnifiedChooseFirmware"));
    m_monitor = new QPushButton(tr("2. 开始监听设备"), m_contextPanel);
    m_monitor->setProperty("kind", QStringLiteral("primary"));
    m_autoConnect = new QCheckBox(tr("烧录后自动连接设备"), m_contextPanel);
    m_autoConnect->setObjectName(
        QStringLiteral("tn160AutoConnectAfterFlash"));
    m_autoConnect->setChecked(true);
    m_contextPath = new QLabel(tr("未选择固件"), m_contextPanel);
    m_contextPath->setObjectName(QStringLiteral("pageSubtitle"));
    m_contextPath->setWordWrap(true);
    m_contextStatus = new QLabel(tr("等待选择 UF2"), m_contextPanel);
    m_contextStatus->setObjectName(QStringLiteral("statusPill"));
    contextLayout->addWidget(m_choose);
    contextLayout->addWidget(m_contextPath);
    contextLayout->addWidget(m_monitor);
    contextLayout->addWidget(m_autoConnect);
    contextLayout->addWidget(m_contextStatus);
    contextLayout->addStretch();

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

    connect(m_choose, &QPushButton::clicked, this, &FirmwarePage::chooseFirmware);
    connect(m_monitor, &QPushButton::clicked, this, &FirmwarePage::toggleMonitoring);
    connect(&m_timer, &QTimer::timeout, this, &FirmwarePage::scanVolumes);
    m_timer.setInterval(500);
}

void FirmwarePage::shutdown()
{
    setMonitoring(false);
    m_ioTasks.closeAndWait();
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

bool FirmwarePage::autoConnectAfterFlash() const noexcept
{
    return m_autoConnect && m_autoConnect->isChecked();
}

QWidget *FirmwarePage::contextPanel() const noexcept
{
    return m_contextPanel;
}

void FirmwarePage::chooseFirmware()
{
    if (monitoring()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择 UF2 固件"), QString(), tr("UF2 固件 (*.uf2)"));
    if (path.isEmpty()) {
        return;
    }
    m_firmwarePath = path;
    m_status->setText(tr("固件已就绪"));
    m_contextPath->setText(QDir::toNativeSeparators(path));
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

void FirmwarePage::setNextDeviceBlocked(const bool blocked)
{
    if (m_nextDeviceBlocked == blocked) return;
    m_nextDeviceBlocked = blocked;
    if (!monitoring() || !m_inFlightVolumes.isEmpty()) return;
    m_status->setText(blocked
        ? tr("监听中 · 当前设备处理中 · 拔出后接收下一台")
        : tr("监听中 · 已烧录 %1 台 · 等待下一台 BOOTSEL")
              .arg(m_successfulFlashes));
    if (m_contextStatus) m_contextStatus->setText(m_status->text());
    emit firmwareStateChanged(m_firmwarePath, m_status->text(), true);
}

void FirmwarePage::setMonitoring(const bool active)
{
    if (active) {
        m_successfulFlashes = 0;
        m_inFlightVolumes.clear();
        m_timer.start();
        m_monitor->setText(tr("停止监听"));
        m_monitor->setProperty("kind", QStringLiteral("warning"));
        m_status->setText(m_nextDeviceBlocked
            ? tr("监听中 · 当前设备处理中 · 拔出后接收下一台")
            : tr("监听中 · 已完成 0 台 · 等待 BOOTSEL 设备"));
    } else {
        m_timer.stop();
        m_monitor->setText(tr("2. 开始监听设备"));
        m_monitor->setProperty("kind", QStringLiteral("primary"));
        if (!m_firmwarePath.isEmpty()) {
            m_status->setText(!m_inFlightVolumes.isEmpty()
                ? tr("已停止接收新设备 · 等待当前烧录完成")
                : m_successfulFlashes > 0
                    ? tr("监听已停止 · 本次已烧录 %1 台").arg(m_successfulFlashes)
                    : tr("监听已停止"));
        }
    }
    const bool writePending = !m_inFlightVolumes.isEmpty();
    if (m_choose) m_choose->setEnabled(!active && !writePending);
    m_monitor->setEnabled(active || !writePending);
    m_monitor->style()->unpolish(m_monitor);
    m_monitor->style()->polish(m_monitor);
    if (m_contextStatus) {
        m_contextStatus->setText(m_status->text());
        m_contextStatus->setProperty(
            "state", active || writePending ? QStringLiteral("connecting")
                                               : QStringLiteral("ready"));
        m_contextStatus->style()->unpolish(m_contextStatus);
        m_contextStatus->style()->polish(m_contextStatus);
    }
    emit firmwareStateChanged(m_firmwarePath, m_status->text(), active);
}

void FirmwarePage::scanVolumes()
{
    QSet<QString> mountedVolumes;
    const auto volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &storage : volumes) {
        if (storage.isValid() && storage.isReady()) {
            mountedVolumes.insert(
                QDir::cleanPath(storage.rootPath()).toCaseFolded());
        }
    }
    for (auto it = m_attemptedVolumes.begin(); it != m_attemptedVolumes.end();) {
        if (!mountedVolumes.contains(*it) && !m_inFlightVolumes.contains(*it)) {
            it = m_attemptedVolumes.erase(it);
        } else {
            ++it;
        }
    }
    if (m_nextDeviceBlocked || !m_inFlightVolumes.isEmpty()) return;
    for (const QStorageInfo &storage : volumes) {
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
        m_inFlightVolumes.insert(key);
        m_status->setText(tr("监听中 · 正在烧录第 %1 台")
                              .arg(m_successfulFlashes + m_inFlightVolumes.size()));
        if (m_contextStatus) m_contextStatus->setText(m_status->text());
        appendLog(tr("检测到候选卷：%1 · %2 · %3 MiB")
                      .arg(QDir::toNativeSeparators(root), storage.displayName())
                      .arg(capacity / (1024 * 1024)));
        flashVolume(root);
        // The product workflow owns one device at a time. Even if multiple
        // BOOTSEL volumes appear together, only the first can enter this cycle.
        break;
    }
}

void FirmwarePage::flashVolume(const QString &rootPath)
{
    struct FlashResult {
        QStringList messages;
        QString firmwareSha256;
        bool succeeded = false;
    };

    const QString firmwarePath = m_firmwarePath;
    const QString volumeKey = QDir::cleanPath(rootPath).toCaseFolded();
    auto result = std::make_shared<FlashResult>();
    QPointer<FirmwarePage> page(this);
    const bool accepted = m_ioTasks.run(
        [page, firmwarePath, rootPath, volumeKey, result] {
            QFile source(firmwarePath);
            if (!source.open(QIODevice::ReadOnly)) {
                result->messages.append(
                    FirmwarePage::tr("错误：无法读取固件：%1").arg(source.errorString()));
            } else {
                const QByteArray firmware = source.readAll();
                result->firmwareSha256 = QString::fromLatin1(
                    QCryptographicHash::hash(firmware,
                                             QCryptographicHash::Sha256)
                        .toHex());
                const QString destinationPath = QDir(rootPath).filePath(
                    QStringLiteral("flash.uf2"));
                for (int attempt = 1; attempt <= 3 && !result->succeeded; ++attempt) {
                    QFile destination(destinationPath);
                    if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        result->messages.append(
                            FirmwarePage::tr("尝试 %1/3 写入失败：%2")
                                .arg(attempt).arg(destination.errorString()));
                        QThread::msleep(500);
                        continue;
                    }
                    const qint64 written = destination.write(firmware);
                    destination.flush();
                    destination.close();
                    if (written != firmware.size()) {
                        result->messages.append(
                            FirmwarePage::tr("尝试 %1/3 写入不完整：%2 / %3 bytes")
                                .arg(attempt).arg(written).arg(firmware.size()));
                        QThread::msleep(500);
                        continue;
                    }
                    QThread::msleep(100);
                    if (!QFileInfo::exists(destinationPath)) {
                        result->messages.append(
                            FirmwarePage::tr("写入完成后设备已复位，盘符消失 · 视为成功"));
                        result->succeeded = true;
                        break;
                    }
                    const qint64 finalSize = QFileInfo(destinationPath).size();
                    if (finalSize == firmware.size()) {
                        result->succeeded = true;
                    } else {
                        result->messages.append(
                            FirmwarePage::tr("尝试 %1/3 大小校验失败：%2 / %3 bytes")
                                .arg(attempt).arg(finalSize).arg(firmware.size()));
                        QThread::msleep(500);
                    }
                }
            }

            if (!page) return;
            QMetaObject::invokeMethod(page, [page, rootPath, volumeKey, result] {
                if (!page) return;
                for (const QString &message : result->messages) page->appendLog(message);
                if (!result->succeeded) {
                    page->m_inFlightVolumes.remove(volumeKey);
                    page->appendLog(FirmwarePage::tr(
                        "错误：烧录失败，请重新连接设备后重试"));
                    if (page->monitoring()) {
                        page->m_status->setText(FirmwarePage::tr(
                            "监听中 · 烧录失败 · 等待下一台 BOOTSEL"));
                        if (page->m_contextStatus) {
                            page->m_contextStatus->setText(page->m_status->text());
                        }
                    } else if (page->m_inFlightVolumes.isEmpty()) {
                        if (page->m_choose) page->m_choose->setEnabled(true);
                        page->m_monitor->setEnabled(true);
                        if (page->m_contextStatus) {
                            page->m_contextStatus->setProperty(
                                "state", QStringLiteral("ready"));
                            page->m_contextStatus->style()->unpolish(
                                page->m_contextStatus);
                            page->m_contextStatus->style()->polish(
                                page->m_contextStatus);
                        }
                    }
                    return;
                }
                page->appendLog(FirmwarePage::tr("烧录完成并通过大小校验：%1")
                                    .arg(QDir::toNativeSeparators(rootPath)));
                page->m_inFlightVolumes.remove(volumeKey);
                ++page->m_successfulFlashes;
                page->m_status->setText(page->monitoring()
                    ? FirmwarePage::tr("监听中 · 已烧录 %1 台 · 等待下一台 BOOTSEL")
                          .arg(page->m_successfulFlashes)
                    : FirmwarePage::tr("监听已停止 · 本次已烧录 %1 台")
                          .arg(page->m_successfulFlashes));
                if (page->m_contextStatus) {
                    page->m_contextStatus->setText(page->m_status->text());
                }
                if (!page->monitoring() && page->m_inFlightVolumes.isEmpty()) {
                    if (page->m_choose) page->m_choose->setEnabled(true);
                    page->m_monitor->setEnabled(true);
                    if (page->m_contextStatus) {
                        page->m_contextStatus->setProperty(
                            "state", QStringLiteral("ready"));
                        page->m_contextStatus->style()->unpolish(
                            page->m_contextStatus);
                        page->m_contextStatus->style()->polish(
                            page->m_contextStatus);
                    }
                }
                emit page->firmwareStateChanged(page->m_firmwarePath,
                                                page->m_status->text(),
                                                page->monitoring());
                emit page->firmwareFlashSucceeded(
                    page->m_firmwarePath, result->firmwareSha256, rootPath);
                if (page->m_host) {
                    page->m_host->setStatus(FirmwarePage::tr("TN160 · UF2 烧录成功"));
                }
            }, Qt::QueuedConnection);
        });

    if (!accepted) {
        m_attemptedVolumes.remove(volumeKey);
        m_inFlightVolumes.remove(volumeKey);
        if (!monitoring() && m_inFlightVolumes.isEmpty()) {
            if (m_choose) m_choose->setEnabled(true);
            m_monitor->setEnabled(true);
        }
        appendLog(tr("错误：后台 I/O 队列已满，本次烧录未启动"));
    }
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

} // namespace serein::scope::tn160ncm
