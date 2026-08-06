#include "LivePage.h"
#include "ThermalView.h"
#include "Tn160Engine.h"
#include "Tn160PortSelection.h"
#include "TrendWidget.h"
#include "WorkspaceCanvas.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QtEndian>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>

#include <cmath>
#include <exception>

namespace serein::scope::tn160 {

namespace {

constexpr int RawFrameWordsPerRow = FrameWidth + 2;
constexpr int RawFrameSize = 2 + FrameHeight * RawFrameWordsPerRow * 2 + 2;
constexpr int RawBaud = 2000000;

bool rawFramePlausible(const QByteArray &frame)
{
    if (frame.size() != RawFrameSize
        || frame.at(0) != char(0xff) || frame.at(1) != char(0xff)) {
        return false;
    }
    for (int offset = 2; offset + 1 < frame.size(); offset += 2) {
        const quint16 word = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar *>(frame.constData() + offset));
        if (word > 0x3fffu) return false;
    }
    return true;
}

QString saveRawCapture(const QVector<QByteArray> &frames, QString *error)
{
    if (frames.isEmpty()) {
        if (error) *error = QObject::tr("没有可保存的原始帧");
        return {};
    }
    const QString rootPath = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    QDir root(rootPath);
    if (!root.mkpath(QStringLiteral("thermal_state_capture"))) {
        if (error) *error = QObject::tr("无法创建原始采集目录");
        return {};
    }
    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    const QString folderName = QStringLiteral("thermal_%1").arg(stamp);
    const QString folderPath = root.filePath(
        QStringLiteral("thermal_state_capture/%1").arg(folderName));
    if (!root.mkpath(QStringLiteral("thermal_state_capture/%1").arg(folderName))) {
        if (error) *error = QObject::tr("无法创建原始采集结果目录");
        return {};
    }

    QByteArray bytes;
    bytes.reserve(frames.size() * RawFrameSize);
    for (const QByteArray &frame : frames) bytes.append(frame);
    const QByteArray digest = QCryptographicHash::hash(
        bytes, QCryptographicHash::Sha256).toHex();

    QSaveFile rawFile(QDir(folderPath).filePath(QStringLiteral("raw_capture.bin")));
    if (!rawFile.open(QIODevice::WriteOnly)
        || rawFile.write(bytes) != bytes.size() || !rawFile.commit()) {
        if (error) *error = QObject::tr("写入原始采集文件失败：%1")
                                  .arg(rawFile.errorString());
        return {};
    }

    QJsonObject protocol;
    protocol.insert(QStringLiteral("raw_command"), QStringLiteral("0xAA"));
    protocol.insert(QStringLiteral("monitor_restore_command"), QStringLiteral("0x11"));
    protocol.insert(QStringLiteral("baud"), RawBaud);
    protocol.insert(QStringLiteral("raw_frame_bytes"), RawFrameSize);
    QJsonObject capture;
    capture.insert(QStringLiteral("frame_count"), frames.size());
    capture.insert(QStringLiteral("width"), FrameWidth);
    capture.insert(QStringLiteral("height"), FrameHeight);
    capture.insert(QStringLiteral("row_metadata_words"), 2);
    QJsonObject archive;
    archive.insert(QStringLiteral("file"), QStringLiteral("raw_capture.bin"));
    archive.insert(QStringLiteral("sha256"), QString::fromLatin1(digest));
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"),
                   QStringLiteral("tn160.thermal-state-raw-capture.v1"));
    manifest.insert(QStringLiteral("captured_at"),
                   QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    manifest.insert(QStringLiteral("protocol"), protocol);
    manifest.insert(QStringLiteral("capture"), capture);
    manifest.insert(QStringLiteral("archive"), archive);

    QSaveFile manifestFile(QDir(folderPath).filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::WriteOnly)) {
        if (error) *error = QObject::tr("写入原始采集清单失败：%1")
                                  .arg(manifestFile.errorString());
        return {};
    }
    const QByteArray json = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (manifestFile.write(json) != json.size() || !manifestFile.commit()) {
        if (error) *error = QObject::tr("提交原始采集清单失败：%1")
                                  .arg(manifestFile.errorString());
        return {};
    }
    return folderPath;
}

} // namespace

LivePage::LivePage(IHostServices *host, QWidget *parent)
    : QWidget(parent), m_host(host), m_transport(this),
      m_decoder(std::make_unique<BufferedDecoder>(std::make_unique<Tn160Engine>()))
{
    setObjectName(QStringLiteral("tn160LivePage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_workspace = new WorkspaceCanvas(QStringLiteral("tn160/live-workbench-v4"));

    auto *imageContent = new QWidget;
    imageContent->setObjectName(QStringLiteral("componentContent"));
    auto *imageLayout = new QVBoxLayout(imageContent);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    m_thermal = new ThermalView;
    imageLayout->addWidget(m_thermal, 1);

    // The host owns the system-level data/property and output surfaces.
    // Keep the live values and trend source with this page, then let the host
    // attach the trend widget to its output surface when TN160 live is active.
    m_trend = new TrendWidget(this);
    m_trend->setObjectName(QStringLiteral("tn160TrendWidget"));
    m_trend->hide();

    m_workspace->addComponent(QStringLiteral("thermal"), tr("TN160 热图 · 160 × 120"),
                              imageContent, QRectF(0.012, 0.016, 0.976, 0.944),
                              QSize(380, 220));

    auto *transportState = new QWidget(this);
    transportState->setObjectName(QStringLiteral("tn160TransportState"));
    transportState->hide();
    m_port = new QComboBox(transportState);
    m_port->setObjectName(QStringLiteral("tn160Port"));
    m_port->setPlaceholderText(tr("未发现串口"));
    m_baud = new QSpinBox(transportState);
    m_baud->setRange(300, 12000000);
    m_baud->setValue(2000000);
    m_baud->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_scan = new QPushButton(tr("刷新"), transportState);
    m_state = new QLabel(tr("未连接"), transportState);
    m_state->setObjectName(QStringLiteral("statusPill"));
    m_capture = new QPushButton(tr("开始采集"), transportState);
    m_capture->setProperty("kind", QStringLiteral("primary"));

    root->addWidget(m_workspace, 1);

    connect(m_scan, &QPushButton::clicked, this, &LivePage::refreshPorts);
    connect(m_capture, &QPushButton::clicked, this, &LivePage::toggleCapture);
    connect(&m_transport, &SerialTransport::stateChanged, this, &LivePage::handleState);
    connect(&m_transport, &SerialTransport::dataReceived, this, &LivePage::handleData);
    connect(&m_transport, &SerialTransport::errorOccurred, this, &LivePage::handleError);
    m_rawTimeout.setSingleShot(true);
    connect(&m_rawTimeout, &QTimer::timeout, this, &LivePage::handleRawTimeout);
    refreshPorts();
}

void LivePage::shutdown()
{
    m_rawTimeout.stop();
    if (m_rawCapture || m_rawRestoring) {
        m_transport.write(QByteArray(1, char(0x11)));
        m_rawCapture = false;
        m_rawRestoring = false;
        m_transport.closeAfterWrites();
        return;
    }
    m_transport.close();
}

void LivePage::refreshPorts()
{
    const QString current = m_port->currentText();
    const QList<SerialPortInfo> ports = SerialTransport::availablePorts();
    m_port->clear();
    for (const SerialPortInfo &port : ports) {
        m_port->addItem(port.portName);
    }
    const int preferredIndex = preferredTn160PortIndex(ports);
    if (preferredIndex >= 0) {
        m_port->setCurrentIndex(preferredIndex);
        if (m_host && current != ports.at(preferredIndex).portName) {
            m_host->log(QStringLiteral("info"),
                        tr("TN160 自动选择 CH346 A2 候选：%1")
                            .arg(ports.at(preferredIndex).portName));
        }
    } else if (!current.trimmed().isEmpty()) {
        const int index = m_port->findText(current);
        if (index >= 0) {
            m_port->setCurrentIndex(index);
        } else {
            m_port->addItem(current.trimmed(), tr("手动端口"));
            m_port->setCurrentIndex(m_port->count() - 1);
        }
    }
    if (m_host) m_host->log(QStringLiteral("info"), tr("TN160 扫描到 %1 个串口").arg(ports.size()));
}

void LivePage::toggleCapture()
{
    if (m_rawCapture || m_rawRestoring) {
        cancelRawCapture();
        return;
    }
    if (m_transport.isActive()) {
        m_transport.close();
        return;
    }
    if (m_port->currentText().isEmpty()) {
        handleError(tr("请先扫描并选择串口"));
        return;
    }
    m_decoder->reset();
    m_trend->clear();
    m_thermal->clear();
    m_frames = 0;
    m_elapsed.restart();
    m_transport.open({m_port->currentText(), m_baud->value()});
}

void LivePage::startRawCapture(const int frameCount)
{
    if (m_rawCapture || m_rawRestoring) return;
    if (!m_transport.isActive()) {
        handleError(tr("请先连接 TN160，再开始原始帧采集"));
        return;
    }
    if (m_baud->value() != RawBaud) {
        handleError(tr("原始帧采集要求 2000000 波特率"));
        return;
    }
    m_rawBuffer.clear();
    m_rawFrames.clear();
    m_rawTargetFrames = std::clamp(frameCount, 1, 256);
    m_rawCapture = true;
    m_rawRestoring = false;
    m_rawCancelled = false;
    m_rawFailureMessage.clear();
    m_decoder->reset();
    m_transport.write(QByteArray(1, char(0xaa)));
    m_rawTimeout.start(3000);
    setState(QStringLiteral("raw-capturing"),
             tr("原始采集中 %1/%2").arg(0).arg(m_rawTargetFrames));
    if (m_host) m_host->log(QStringLiteral("info"),
                            tr("TN160 已发送 0xAA，开始采集 %1 帧原始 RAW_U16")
                                .arg(m_rawTargetFrames));
}

void LivePage::cancelRawCapture()
{
    if (!m_rawCapture && !m_rawRestoring) return;
    if (m_rawRestoring) return;
    m_rawTimeout.stop();
    m_rawCancelled = true;
    m_rawCapture = false;
    m_rawFrames.clear();
    m_rawBuffer.clear();
    restoreMonitorAfterRawCapture();
    if (m_host) m_host->log(QStringLiteral("warning"), tr("TN160 原始帧采集已取消"));
}

bool LivePage::rawCaptureActive() const noexcept
{
    return m_rawCapture || m_rawRestoring;
}

void LivePage::setPort(const QString &port)
{
    if (!m_port) return;
    const QString normalized = port.trimmed();
    if (normalized.isEmpty()) return;
    int index = m_port->findText(normalized, Qt::MatchFixedString);
    if (index < 0) {
        m_port->addItem(normalized, tr("手动端口"));
        index = m_port->count() - 1;
    }
    m_port->setCurrentIndex(index);
}

void LivePage::setBaud(const int baud)
{
    if (m_baud) m_baud->setValue(baud);
}

int LivePage::baudRate() const noexcept
{
    return m_baud ? m_baud->value() : 0;
}

bool LivePage::connectionActive() const noexcept
{
    return m_transport.isActive();
}

void LivePage::handleState(const QString &state)
{
    const bool locked = state != QStringLiteral("disconnected");
    const QString text = state == QStringLiteral("connected") ? tr("采集中")
        : state == QStringLiteral("connecting") ? tr("连接中")
        : state == QStringLiteral("disconnecting") ? tr("断开中") : tr("未连接");
    setState(state, text);
    m_port->setEnabled(!locked);
    m_baud->setEnabled(!locked);
    m_scan->setEnabled(!locked);
    m_capture->setText(state == QStringLiteral("connected") ? tr("停止采集") : tr("开始采集"));
    m_capture->setEnabled(state != QStringLiteral("connecting")
                          && state != QStringLiteral("disconnecting"));
    if (m_host) m_host->setStatus(tr("TN160 · %1").arg(text));
}

void LivePage::handleData(const QByteArray &data)
{
    if (m_rawCapture) {
        handleRawData(data);
        return;
    }
    try {
        const PacketList packets = m_decoder->feed(QByteArrayView(data));
        const bool restored = processPackets(packets);
        if (m_rawRestoring && restored) {
            m_rawRestoring = false;
            const bool success = !m_rawCancelled && m_rawFailureMessage.isEmpty();
            const QString message = m_rawFailureMessage;
            QTimer::singleShot(0, this, [this, success, message] {
                finishRawCapture(success, message);
            });
        }
    } catch (const std::exception &error) {
        m_decoder->reset();
        handleError(QString::fromUtf8(error.what()));
    }
}

bool LivePage::processPackets(const PacketList &packets)
{
    bool hasImage = false;
    for (const Packet &packet : packets) {
        if (const auto *image = std::get_if<ImagePacket>(&packet)) {
            hasImage = true;
            m_thermal->setFrame(image->pixels, image->width, image->height);
            ++m_frames;
        } else if (const auto *sample = std::get_if<SamplePacket>(&packet)) {
            updateSample(*sample);
        }
    }
    return hasImage;
}

void LivePage::handleRawData(const QByteArray &data)
{
    m_rawBuffer.append(data);
    while (m_rawCapture) {
        const qsizetype start = m_rawBuffer.indexOf(QByteArrayLiteral("\xff\xff"));
        if (start < 0) {
            if (m_rawBuffer.size() > 1) m_rawBuffer = m_rawBuffer.right(1);
            return;
        }
        if (start > 0) m_rawBuffer.remove(0, start);
        if (m_rawBuffer.size() < RawFrameSize) return;
        const QByteArray frame = m_rawBuffer.left(RawFrameSize);
        if (!rawFramePlausible(frame)) {
            m_rawBuffer.remove(0, 2);
            continue;
        }
        m_rawBuffer.remove(0, RawFrameSize);
        m_rawFrames.append(frame);
        m_rawTimeout.start(3000);
        const int count = m_rawFrames.size();
        if (count == 1 || count % 8 == 0 || count == m_rawTargetFrames) {
            setState(QStringLiteral("raw-capturing"),
                     tr("原始采集中 %1/%2").arg(count).arg(m_rawTargetFrames));
        }
        if (count >= m_rawTargetFrames) {
            m_rawCapture = false;
            m_rawBuffer.clear();
            m_rawTimeout.stop();
            restoreMonitorAfterRawCapture();
            return;
        }
    }
}

void LivePage::restoreMonitorAfterRawCapture()
{
    if (!m_transport.isActive()) {
        finishRawCapture(false, tr("恢复监控模式失败：串口已断开"));
        return;
    }
    m_rawRestoring = true;
    m_decoder->reset();
    m_transport.write(QByteArray(1, char(0x11)));
    setState(QStringLiteral("raw-restoring"), tr("正在恢复监控模式"));
    m_rawTimeout.start(4000);
}

void LivePage::finishRawCapture(const bool success, const QString &message)
{
    m_rawTimeout.stop();
    if (m_rawCancelled) {
        if (m_host) m_host->log(QStringLiteral("warning"), tr("TN160 原始帧采集已取消"));
        setState(m_transport.isActive() ? QStringLiteral("connected")
                                        : QStringLiteral("disconnected"),
                 m_transport.isActive() ? tr("采集中") : tr("未连接"));
        m_rawFrames.clear();
        m_rawFailureMessage.clear();
        m_rawCancelled = false;
        return;
    }
    if (!success) {
        if (m_host) m_host->log(QStringLiteral("error"), tr("TN160 原始采集失败：%1").arg(message));
        setState(QStringLiteral("error"), tr("错误"));
        m_rawFrames.clear();
        return;
    }
    QString error;
    const QString path = saveRawCapture(m_rawFrames, &error);
    if (path.isEmpty()) {
        if (m_host) m_host->log(QStringLiteral("error"),
                                tr("TN160 原始采集保存失败：%1").arg(error));
        setState(QStringLiteral("error"), tr("保存失败"));
    } else {
        if (m_host) {
            m_host->log(QStringLiteral("info"),
                        tr("TN160 原始采集已保存：%1（%2 帧）")
                            .arg(path).arg(m_rawFrames.size()));
        }
        setState(QStringLiteral("connected"), tr("采集中"));
    }
    m_rawFrames.clear();
    m_rawFailureMessage.clear();
}

void LivePage::updateSample(const SamplePacket &sample)
{
    if (sample.values.size() < 4) return;
    const auto formatTemperature = [](const double value) {
        return std::isfinite(value) ? QStringLiteral("%1 °C").arg(value, 0, 'f', 2)
                                    : QStringLiteral("—");
    };
    const QString center = formatTemperature(sample.values[0]);
    const QString ntc = formatTemperature(sample.values[1]);
    const QString centerAdu = QString::number(sample.values[2], 'f', 0);
    const QString vtemp = QString::number(sample.values[3], 'f', 0);
    const double seconds = std::max(m_elapsed.elapsed() / 1000.0, 0.001);
    const QString fps = QStringLiteral("%1 fps").arg(m_frames / seconds, 0, 'f', 1);
    m_trend->addSample({seconds, sample.values[0], sample.values[1],
                        sample.values[2], sample.values[3]});
    emit telemetryChanged(center, ntc, centerAdu, vtemp, fps);
}

void LivePage::handleError(const QString &message)
{
    m_rawTimeout.stop();
    m_rawCapture = false;
    m_rawRestoring = false;
    m_rawBuffer.clear();
    m_rawFrames.clear();
    m_rawFailureMessage.clear();
    m_rawCancelled = false;
    setState(QStringLiteral("error"), tr("错误"));
    if (m_host) {
        m_host->log(QStringLiteral("error"), QStringLiteral("TN160 · %1").arg(message));
        m_host->setStatus(tr("TN160 · %1").arg(message));
    }
}

void LivePage::handleRawTimeout()
{
    if (m_rawCapture) {
        m_rawCapture = false;
        m_rawBuffer.clear();
        m_rawFailureMessage = tr("等待原始帧超时");
        restoreMonitorAfterRawCapture();
        return;
    }
    if (m_rawRestoring) {
        m_rawRestoring = false;
        finishRawCapture(false, m_rawFailureMessage.isEmpty()
                                      ? tr("恢复监控模式超时") : m_rawFailureMessage);
    }
}

void LivePage::setState(const QString &state, const QString &text)
{
    m_state->setText(text);
    m_state->setProperty("state", state);
    m_state->style()->unpolish(m_state);
    m_state->style()->polish(m_state);
    emit connectionStateChanged(state, text);
    emit rawCaptureStateChanged(state, text);
}

} // namespace serein::scope::tn160
