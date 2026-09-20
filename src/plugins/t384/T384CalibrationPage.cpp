#include "T384CalibrationPage.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QDateTime>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace serein::scope::t384 {

T384CalibrationPage::T384CalibrationPage(QWidget *parent)
    : QWidget(parent)
{
    m_contextPanel = new QWidget(this);
    auto *layout = new QVBoxLayout(m_contextPanel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    auto *title = new QLabel(tr("T256/T384/T640 实验双点标定"), m_contextPanel);
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    auto *warning = new QLabel(tr("仅主机侧模型 · experimental / non-OEM；不会写入设备"),
                               m_contextPanel);
    warning->setWordWrap(true);
    warning->setObjectName(QStringLiteral("pageSubtitle"));
    layout->addWidget(warning);

    auto *buttons = new QHBoxLayout;
    m_zeroButton = new QPushButton(tr("采集 0°C（30 帧）"), m_contextPanel);
    m_highButton = new QPushButton(tr("采集 50°C（30 帧）"), m_contextPanel);
    buttons->addWidget(m_zeroButton);
    buttons->addWidget(m_highButton);
    auto *import = new QPushButton(tr("导入 JSON"), m_contextPanel);
    auto *exportButton = new QPushButton(tr("导出 JSON"), m_contextPanel);
    buttons->addWidget(import);
    buttons->addWidget(exportButton);
    auto *upload = new QPushButton(tr("上传到设备"), m_contextPanel);
    auto *readback = new QPushButton(tr("回读"), m_contextPanel);
    auto *commit = new QPushButton(tr("提交"), m_contextPanel);
    auto *abort = new QPushButton(tr("撤销"), m_contextPanel);
    buttons->addWidget(upload);
    buttons->addWidget(readback);
    buttons->addWidget(commit);
    buttons->addWidget(abort);
    layout->addLayout(buttons);

    m_status = new QLabel(tr("就绪：请先选择温度并保持黑体稳定"), m_contextPanel);
    m_status->setWordWrap(true);
    m_stats = new QLabel(m_contextPanel);
    m_stats->setWordWrap(true);
    m_live = new QLabel(tr("实时温度：—"), m_contextPanel);
    m_live->setObjectName(QStringLiteral("statusPill"));
    layout->addWidget(m_status);
    layout->addWidget(m_stats);
    layout->addWidget(m_live);
    layout->addStretch();

    connect(m_zeroButton, &QPushButton::clicked, this, &T384CalibrationPage::captureZero);
    connect(m_highButton, &QPushButton::clicked, this, &T384CalibrationPage::captureHigh);
    connect(import, &QPushButton::clicked, this, &T384CalibrationPage::importModel);
    connect(exportButton, &QPushButton::clicked, this, &T384CalibrationPage::exportModel);
    connect(upload, &QPushButton::clicked, this, &T384CalibrationPage::uploadModel);
    connect(readback, &QPushButton::clicked, this, &T384CalibrationPage::readbackModel);
    connect(commit, &QPushButton::clicked, this, &T384CalibrationPage::commitModel);
    connect(abort, &QPushButton::clicked, this, &T384CalibrationPage::abortModel);
    updateUi();
}

void T384CalibrationPage::setDeviceIdentity(const QByteArray &identity)
{
    m_deviceIdentity = identity.left(32);
    updateUi();
}

double T384CalibrationPage::roiMean(const QByteArray &frame, const int width, const int height)
{
    if (width <= 0 || height <= 0 || frame.size() != width * height * 2) return 0.0;
    const int x0 = std::max(0, width / 2 - 8);
    const int y0 = std::max(0, height / 2 - 8);
    const int x1 = std::min(width, x0 + 16);
    const int y1 = std::min(height, y0 + 16);
    quint64 sum = 0;
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const auto *p = reinterpret_cast<const uchar *>(frame.constData() + (y * width + x) * 2);
            sum += (quint16(p[0]) << 8) | quint16(p[1]);
            ++count;
        }
    }
    return count ? double(sum) / count : 0.0;
}

void T384CalibrationPage::ingestFrame(const QByteArray &y16be, const int width, const int height)
{
    m_frameWidth = width;
    m_frameHeight = height;
    const double mean = roiMean(y16be, width, height);
    if (mean == 0.0 && y16be.size() != width * height * 2) {
        m_status->setText(tr("忽略无效帧：需要完整 Y16BE (%1×%2)").arg(width).arg(height));
        return;
    }
    m_lastRaw = mean;
    QVector<double> *samples = m_captureZero ? &m_zeroSamples : (m_captureHigh ? &m_highSamples : nullptr);
    if (samples && samples->size() < 30) {
        samples->append(mean);
        if (samples->size() == 30) {
            if (m_captureZero) m_captureZero = false;
            if (m_captureHigh) m_captureHigh = false;
            fitModel();
        }
    }
    updateUi();
}

void T384CalibrationPage::ingestFrame(const QByteArray &y16be)
{
    const int pixels = y16be.size() / 2;
    const int width = pixels == 256 * 192 ? 256
        : (pixels == 384 * 288 ? 384
        : (pixels == 640 * 512 ? 640 : 0));
    const int height = width == 256 ? 192
        : (width == 384 ? 288 : (width == 640 ? 512 : 0));
    ingestFrame(y16be, width, height);
}

void T384CalibrationPage::captureZero() { m_captureZero = true; m_captureHigh = false; m_zeroSamples.clear(); updateUi(); }
void T384CalibrationPage::captureHigh() { m_captureHigh = true; m_captureZero = false; m_highSamples.clear(); updateUi(); }

void T384CalibrationPage::fitModel()
{
    if (m_zeroSamples.size() != 30 || m_highSamples.size() != 30) return;
    auto avg = [](const QVector<double> &v) { double s = 0; for (double x : v) s += x; return s / v.size(); };
    m_zeroRaw = avg(m_zeroSamples);
    const double high = avg(m_highSamples);
    m_countsPerC = (high - m_zeroRaw) / 50.0;
    m_modelEnabled = std::isfinite(m_countsPerC) && m_countsPerC > 0.0;
    emit modelChanged(m_modelEnabled, m_zeroRaw, m_countsPerC);
}

void T384CalibrationPage::updateUi()
{
    m_zeroButton->setText(tr("采集 0°C（%1/30）").arg(m_zeroSamples.size()));
    m_highButton->setText(tr("采集 50°C（%1/30）").arg(m_highSamples.size()));
    m_status->setText(m_captureZero ? tr("采集中：0°C，请保持黑体稳定") : m_captureHigh ? tr("采集中：50°C，请保持黑体稳定") : (m_modelEnabled ? tr("模型就绪（experimental / non-OEM）") : tr("就绪：请采集两组各 30 帧")));
    m_stats->setText(tr("ROI 中心 16×16 · zero=%1 · counts/°C=%2").arg(m_zeroRaw, 0, 'f', 3).arg(m_countsPerC, 0, 'f', 3));
    if (m_modelEnabled && m_countsPerC > 0) m_live->setText(tr("实时温度：%1 °C（实验模型）").arg((m_lastRaw - m_zeroRaw) / m_countsPerC, 0, 'f', 2));
}

bool T384CalibrationPage::saveJson(const QString &path) const
{
    QJsonObject o{{QStringLiteral("schema"), QStringLiteral("t384-experimental-2point-v1")}, {QStringLiteral("zeroRaw"), m_zeroRaw}, {QStringLiteral("countsPerC"), m_countsPerC}, {QStringLiteral("roi"), QStringLiteral("center-16x16")}, {QStringLiteral("temperatureLowC"), 0}, {QStringLiteral("temperatureHighC"), 50}, {QStringLiteral("oem"), false}};
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return false; f.write(QJsonDocument(o).toJson(QJsonDocument::Indented)); return true;
}

bool T384CalibrationPage::loadJson(const QString &path)
{
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return false; const auto doc = QJsonDocument::fromJson(f.readAll());
    const auto o = doc.object(); const double zero = o.value(QStringLiteral("zeroRaw")).toDouble(); const double c = o.value(QStringLiteral("countsPerC")).toDouble();
    if (o.value(QStringLiteral("oem")).toBool(true) || !std::isfinite(c) || c <= 0) return false;
    m_zeroRaw = zero; m_countsPerC = c; m_modelEnabled = true; emit modelChanged(true, zero, c); updateUi(); return true;
}

void T384CalibrationPage::uploadModel()
{
    if (m_deviceIdentity.isEmpty()) {
        m_status->setText(tr("请先连接设备并读取 PN/SN/FW 身份"));
        return;
    }
    if (!m_modelEnabled || !std::isfinite(m_zeroRaw) || !std::isfinite(m_countsPerC)
        || m_countsPerC <= 0.0) {
        m_status->setText(tr("请先完成有效的 0°C/50°C 双点采集"));
        return;
    }
    QByteArray payload;
    auto put32 = [&payload](quint32 v) { payload.append(char(v)); payload.append(char(v >> 8)); payload.append(char(v >> 16)); payload.append(char(v >> 24)); };
    auto put16 = [&payload](quint16 v) { payload.append(char(v)); payload.append(char(v >> 8)); };
    QByteArray manifest(112, char(0x00));
    auto at32 = [&manifest](int o, quint32 v) { manifest[o]=char(v); manifest[o+1]=char(v>>8); manifest[o+2]=char(v>>16); manifest[o+3]=char(v>>24); };
    at32(0, 0x54334331u); at32(4, 1u); at32(8, 0u); at32(12, 16u);
    const quint32 id = quint32(QDateTime::currentSecsSinceEpoch()) | 1u; at32(24, id);
    const QByteArray model = QByteArrayLiteral("t384-empirical-2point-v1"); memcpy(manifest.data()+28, model.constData(), qMin(model.size(), 31));
    const QByteArray profile = QByteArray::number(m_frameWidth) + 'x' + QByteArray::number(m_frameHeight);
    memcpy(manifest.data()+60, profile.constData(), qMin(profile.size(), 15));
    memcpy(manifest.data()+76, m_deviceIdentity.constData(), qMin(m_deviceIdentity.size(), 32));
    manifest[108] = 1;
    payload = manifest;
    put16(0); put16(50); put32(quint32(std::llround(m_zeroRaw))); put32(quint32(std::llround(m_zeroRaw + m_countsPerC * 50.0))); put32(quint32(std::llround(m_countsPerC * 1000.0)));
    m_lastPackage = payload;
    emit uploadRequested(payload);
    m_status->setText(tr("正在上传实验模型…"));
}
void T384CalibrationPage::readbackModel() { emit readbackRequested(); m_status->setText(tr("正在回读设备标定…")); }
void T384CalibrationPage::commitModel() { emit commitRequested(); m_status->setText(tr("正在提交设备标定…")); }
void T384CalibrationPage::abortModel() { emit abortRequested(); m_status->setText(tr("已请求撤销设备标定")); }
void T384CalibrationPage::onCalibrationResult(const QString &operation, const bool success, const QString &message)
{ m_status->setText(success ? tr("标定操作 %1 成功").arg(operation) : tr("标定操作 %1 失败：%2").arg(operation, message)); }
void T384CalibrationPage::applyDeviceData(const QByteArray &data)
{
    if (data.size() < 16) { m_status->setText(tr("设备回读数据长度异常：%1").arg(data.size())); return; }
    auto le16 = [&data](int o) { return quint16(quint8(data.at(o))) | quint16(quint8(data.at(o + 1))) << 8; };
    auto le32 = [&data](int o) { return quint32(quint8(data.at(o))) | quint32(quint8(data.at(o + 1))) << 8 | quint32(quint8(data.at(o + 2))) << 16 | quint32(quint8(data.at(o + 3))) << 24; };
    const quint16 low = le16(0), high = le16(2);
    const quint32 zero = le32(4), scale = le32(12);
    if (low != 0 || high != 50 || scale == 0) { m_status->setText(tr("设备回读模型字段不匹配")); return; }
    m_zeroRaw = zero; m_countsPerC = double(scale) / 1000.0; m_modelEnabled = std::isfinite(m_countsPerC) && m_countsPerC > 0.0;
    emit modelChanged(m_modelEnabled, m_zeroRaw, m_countsPerC); updateUi();
    m_status->setText(tr("设备经验模型已回读并校验（experimental / non-OEM）"));
}
void T384CalibrationPage::importModel() { const QString p = QFileDialog::getOpenFileName(this, tr("导入实验模型"), {}, tr("JSON (*.json)")); if (!p.isEmpty() && !loadJson(p)) m_status->setText(tr("导入失败：不是有效的 experimental 模型")); }
void T384CalibrationPage::exportModel() { const QString p = QFileDialog::getSaveFileName(this, tr("导出实验模型"), QStringLiteral("t384-experimental-model.json"), tr("JSON (*.json)")); if (!p.isEmpty() && !saveJson(p)) m_status->setText(tr("导出失败")); }

} // namespace serein::scope::t384
