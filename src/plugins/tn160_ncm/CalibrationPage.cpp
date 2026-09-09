#include "CalibrationPage.h"
#include "ThermalView.h"

#include <serein/scope/WorkspaceCanvas.h>

#include <QtCore/QtEndian>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStyle>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>

namespace serein::scope::tn160ncm {
namespace {

struct ResultPanelChrome {
    QFrame *panel = nullptr;
    QVBoxLayout *bodyLayout = nullptr;
};

ResultPanelChrome makeResultPanel(const QString &title)
{
    auto *panel = new ComponentSurface(title);
    auto *bodyLayout = new QVBoxLayout(panel->contentHost());
    bodyLayout->setContentsMargins(10, 8, 10, 10);
    bodyLayout->setSpacing(6);
    return {panel, bodyLayout};
}

} // namespace

CalibrationPage::CalibrationPage(const CalibrationTransfer::Mode mode,
                                 IHostServices *host, QWidget *parent)
    : QWidget(parent), m_mode(mode), m_host(host), m_transfer(this)
{
    m_contextPanel = new QWidget(this);
    m_contextPanel->setObjectName(QStringLiteral("workspaceContextContent"));
    auto *contextLayout = new QVBoxLayout(m_contextPanel);
    contextLayout->setContentsMargins(0, 0, 0, 0);
    contextLayout->setSpacing(8);
    auto *hint = new QLabel(mode == CalibrationTransfer::Mode::Calibrate
                                ? tr("工程双点标定已整合到“生产标定”；"
                                     "该流程通过 USB NCM 完成采集、写入与回读")
                                : tr("通过 USB NCM 只读检查 V1 标定数据"),
                            m_contextPanel);
    hint->setObjectName(QStringLiteral("pageSubtitle"));
    hint->setWordWrap(true);
    contextLayout->addWidget(hint);
    contextLayout->addWidget(new QLabel(tr("USB NCM 地址"), m_contextPanel));
    m_port = new QComboBox(m_contextPanel);
    m_port->setEditable(true);
    m_port->setObjectName(mode == CalibrationTransfer::Mode::Calibrate
                              ? QStringLiteral("tn160CalibrationPort")
                              : QStringLiteral("tn160CalibrationDataPort"));
    m_port->setMinimumWidth(120);
    contextLayout->addWidget(m_port);
    auto *actions = new QHBoxLayout;
    auto *scan = new QPushButton(tr("扫描设备"), m_contextPanel);
    m_run = new QPushButton(mode == CalibrationTransfer::Mode::Calibrate
                                ? tr("打开生产标定") : tr("读取标定数据"), m_contextPanel);
    m_run->setProperty("kind", QStringLiteral("primary"));
    actions->addWidget(scan);
    actions->addWidget(m_run, 1);
    contextLayout->addLayout(actions);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(10);
    m_state = new QLabel(tr("就绪"), m_contextPanel);
    m_state->setObjectName(QStringLiteral("statusPill"));
    contextLayout->addWidget(m_state);
    contextLayout->addStretch();

    auto *results = new QGridLayout;
    results->setContentsMargins(0, 0, 0, 0);
    results->setHorizontalSpacing(10);
    results->setVerticalSpacing(10);
    auto makeImagePanel = [](const QString &title, const ThermalView::PaletteMode palette,
                             ThermalView **view) {
        const ResultPanelChrome chrome = makeResultPanel(title);
        *view = new ThermalView(chrome.panel);
        (*view)->setPaletteMode(palette);
        (*view)->setCrosshairVisible(false);
        (*view)->setMinimumSize(120, 90);
        chrome.bodyLayout->addWidget(*view, 1);
        return chrome.panel;
    };
    const auto makeBadPointPanel = [this] {
        const ResultPanelChrome chrome = makeResultPanel(tr("坏点分布"));
        m_badPointSummary = new QLabel(tr("暂无标定数据"), chrome.panel);
        m_badPointSummary->setObjectName(QStringLiteral("badPointSummary"));
        m_badPointSummary->setAlignment(Qt::AlignCenter);
        m_badPointSummary->setWordWrap(true);
        chrome.bodyLayout->addWidget(m_badPointSummary, 1);
        return chrome.panel;
    };
    if (mode == CalibrationTransfer::Mode::Calibrate) {
        results->addWidget(makeImagePanel(tr("低温帧 · img_l"), ThermalView::PaletteMode::Grayscale,
                                          &m_lowImage), 0, 0);
        results->addWidget(makeImagePanel(tr("高温帧 · img_h"), ThermalView::PaletteMode::Grayscale,
                                          &m_highImage), 0, 1);
        results->addWidget(makeImagePanel(tr("温差图 · img_dt"), ThermalView::PaletteMode::Grayscale,
                                          &m_deltaImage), 0, 2);
        results->addWidget(makeImagePanel(tr("Flash 背景帧 · img_bg"), ThermalView::PaletteMode::Grayscale,
                                          &m_backgroundImage), 1, 0);
        results->addWidget(makeImagePanel(tr("逐像素 Gain Map"), ThermalView::PaletteMode::Gain,
                                          &m_gainImage), 1, 1);
        results->addWidget(makeBadPointPanel(), 1, 2);
        results->setRowStretch(0, 1);
        results->setRowStretch(1, 1);
    } else {
        results->addWidget(makeImagePanel(tr("Flash 背景帧 · img_bg"), ThermalView::PaletteMode::Grayscale,
                                          &m_backgroundImage), 0, 0);
        results->addWidget(makeImagePanel(tr("逐像素 Gain Map"), ThermalView::PaletteMode::Gain,
                                          &m_gainImage), 0, 1);
        results->addWidget(makeBadPointPanel(), 0, 2);
        results->setRowStretch(0, 1);
    }
    for (int column = 0; column < 3; ++column) results->setColumnStretch(column, 1);
    root->addLayout(results, 1);

    connect(m_run, &QPushButton::clicked, this, &CalibrationPage::startTransfer);
    connect(scan, &QPushButton::clicked, this, &CalibrationPage::refreshPorts);
    connect(m_port, &QComboBox::editTextChanged,
            this, &CalibrationPage::endpointChanged);
    connect(&m_transfer, &CalibrationTransfer::progress,
            this, &CalibrationPage::appendProgress);
    connect(&m_transfer, &CalibrationTransfer::payloadReady,
            this, &CalibrationPage::receivePayload);
    connect(&m_transfer, &CalibrationTransfer::completed,
            this, &CalibrationPage::transferCompleted);
    refreshPorts();
}

void CalibrationPage::shutdown()
{
    m_transfer.cancel();
}

QWidget *CalibrationPage::contextPanel() const noexcept
{
    return m_contextPanel;
}

void CalibrationPage::refreshPorts()
{
    if (m_port->currentText().trimmed().isEmpty()) {
        setPort(QStringLiteral("http://192.168.17.1"));
    }
    emit scanRequested();
}

void CalibrationPage::startTransfer()
{
    if (m_mode == CalibrationTransfer::Mode::Calibrate) {
        emit productionCalibrationRequested();
        return;
    }
    if (m_transfer.isActive()) return;
    if (m_port->currentText().isEmpty()) {
        appendProgress(tr("错误：请先填写 USB NCM 地址"));
        return;
    }
    m_payloads.clear();
    if (m_lowImage) m_lowImage->clear();
    if (m_highImage) m_highImage->clear();
    if (m_deltaImage) m_deltaImage->clear();
    if (m_backgroundImage) m_backgroundImage->clear();
    if (m_gainImage) m_gainImage->clear();
    if (m_badPointSummary) m_badPointSummary->setText(tr("暂无标定数据"));
    m_run->setEnabled(false);
    m_state->setText(tr("运行中"));
    m_state->setProperty("state", QStringLiteral("connecting"));
    m_transfer.start(m_mode, m_port->currentText());
    emit transferStateChanged(QStringLiteral("running"), tr("标定传输中"));
}

void CalibrationPage::appendProgress(const QString &message)
{
    if (m_host) m_host->log(QStringLiteral("info"), QStringLiteral("TN160 CAL · %1").arg(message));
}

void CalibrationPage::receivePayload(const int index, const QByteArray &payload)
{
    while (m_payloads.size() <= index) m_payloads.append(QByteArray{});
    m_payloads[index] = payload;
}

void CalibrationPage::transferCompleted(const bool success, const QString &message)
{
    appendProgress((success ? tr("完成：") : tr("失败：")) + message);
    m_run->setEnabled(true);
    m_state->setText(success ? tr("完成") : tr("失败"));
    m_state->setProperty("state", success ? QStringLiteral("connected")
                                           : QStringLiteral("error"));
    m_state->style()->unpolish(m_state);
    m_state->style()->polish(m_state);
    if (success) renderResults();
    emit transferStateChanged(success ? QStringLiteral("ready") : QStringLiteral("error"),
                              success ? tr("传输完成") : tr("传输失败"));
    if (m_host) m_host->setStatus(success ? tr("TN160 · 标定传输完成")
                                          : tr("TN160 · 标定失败：%1").arg(message));
}

void CalibrationPage::setPort(const QString &port)
{
    if (!m_port) return;
    const QString normalized = port.trimmed();
    if (normalized.isEmpty()) return;
    int index = m_port->findText(normalized, Qt::MatchFixedString);
    if (index < 0) {
        m_port->addItem(normalized, tr("手动地址"));
        index = m_port->count() - 1;
    }
    m_port->setCurrentIndex(index);
}

void CalibrationPage::setDiscoveredEndpoints(
    const QStringList &endpoints)
{
    const QString current = m_port ? m_port->currentText().trimmed()
                                   : QString{};
    for (const QString &endpoint : endpoints) {
        if (m_port && m_port->findText(endpoint, Qt::MatchFixedString) < 0) {
            m_port->addItem(endpoint);
        }
    }
    if (!current.isEmpty()) setPort(current);
}

void CalibrationPage::setTriggerBaud(const int baud)
{
    Q_UNUSED(baud)
}

void CalibrationPage::setReadBaud(const int baud)
{
    Q_UNUSED(baud)
}

bool CalibrationPage::transferActive() const noexcept
{
    return m_transfer.isActive();
}

QVector<quint16> CalibrationPage::decodeU16(const QByteArray &payload, const bool bigEndian)
{
    QVector<quint16> result(payload.size() / 2);
    const auto *data = reinterpret_cast<const uchar *>(payload.constData());
    for (int index = 0; index < result.size(); ++index) {
        result[index] = bigEndian ? qFromBigEndian<quint16>(data + index * 2)
                                  : qFromLittleEndian<quint16>(data + index * 2);
    }
    return result;
}

QVector<float> CalibrationPage::decodeFloat32Le(const QByteArray &payload)
{
    QVector<float> result(payload.size() / 4);
    const auto *data = reinterpret_cast<const uchar *>(payload.constData());
    for (int index = 0; index < result.size(); ++index) {
        const quint32 bits = qFromLittleEndian<quint32>(data + index * 4);
        result[index] = std::bit_cast<float>(bits);
    }
    return result;
}

QVector<QPoint> CalibrationPage::decodeBadPoints(const QByteArray &payload)
{
    QVector<QPoint> result;
    result.reserve(payload.size() / 2);
    for (int index = 0; index + 1 < payload.size(); index += 2) {
        const int y = quint8(payload[index]);
        const int x = quint8(payload[index + 1]);
        if (x == 0xff && y == 0xff) continue;
        if (x >= 0 && x < 160 && y >= 0 && y < 120) result.append(QPoint(x, y));
    }
    return result;
}

QByteArray CalibrationPage::mapToGray(const QVector<double> &values,
                                      const double lowerFraction,
                                      const double upperFraction)
{
    if (values.isEmpty()) return {};
    QVector<double> finiteValues;
    finiteValues.reserve(values.size());
    for (const double value : values) {
        if (std::isfinite(value)) finiteValues.append(value);
    }
    if (finiteValues.isEmpty()) return QByteArray(values.size(), char(0));
    std::sort(finiteValues.begin(), finiteValues.end());
    const int last = finiteValues.size() - 1;
    const int lowerIndex = std::clamp(int(last * lowerFraction), 0, last);
    const int upperIndex = std::clamp(int(last * upperFraction), lowerIndex, last);
    const double minimum = finiteValues[lowerIndex];
    const double maximum = finiteValues[upperIndex];
    const double range = std::max(maximum - minimum, 1e-9);
    QByteArray gray(values.size(), char(0));
    for (int index = 0; index < values.size(); ++index) {
        if (!std::isfinite(values[index])) continue;
        gray[index] = char(std::clamp(int((values[index] - minimum) * 255.0 / range), 0, 255));
    }
    return gray;
}

QString CalibrationPage::scalarStats(const QString &name, const QVector<double> &values)
{
    if (values.isEmpty()) return QStringLiteral("%1: no data").arg(name);
    const auto [minimum, maximum] = std::minmax_element(values.cbegin(), values.cend());
    const double mean = std::accumulate(values.cbegin(), values.cend(), 0.0) / values.size();
    double variance = 0;
    for (const double value : values) variance += (value - mean) * (value - mean);
    const double deviation = std::sqrt(variance / values.size());
    return QStringLiteral("%1  μ %2 · %3…%4 · σ %5")
        .arg(name).arg(mean, 0, 'f', 4).arg(*minimum, 0, 'f', 4)
        .arg(*maximum, 0, 'f', 4).arg(deviation, 0, 'f', 4);
}

void CalibrationPage::renderResults()
{
    const int expected = m_mode == CalibrationTransfer::Mode::Calibrate ? 5 : 3;
    if (m_payloads.size() != expected) {
        appendProgress(tr("payload 数量异常：%1 / %2").arg(m_payloads.size()).arg(expected));
        return;
    }

    if (m_mode == CalibrationTransfer::Mode::Calibrate) {
        const QVector<quint16> low = decodeU16(m_payloads[0], true);
        const QVector<quint16> high = decodeU16(m_payloads[1], true);
        const QVector<quint16> background = decodeU16(m_payloads[2], false);
        const QVector<float> gain = decodeFloat32Le(m_payloads[3]);
        const QVector<QPoint> badPoints = decodeBadPoints(m_payloads[4]);
        QVector<double> lowD, highD, deltaD, backgroundD, gainD;
        lowD.reserve(low.size()); highD.reserve(high.size());
        backgroundD.reserve(background.size()); gainD.reserve(gain.size());
        for (quint16 value : low) lowD.append(value);
        for (quint16 value : high) highD.append(value);
        for (quint16 value : background) backgroundD.append(value);
        for (float value : gain) gainD.append(value);
        const int deltaSize = std::min(low.size(), high.size());
        deltaD.reserve(deltaSize);
        for (int index = 0; index < deltaSize; ++index) {
            deltaD.append(double(high[index]) - double(low[index]));
        }
        m_lowImage->setFrame(mapToGray(lowD));
        m_highImage->setFrame(mapToGray(highD));
        m_deltaImage->setFrame(mapToGray(deltaD));
        m_backgroundImage->setFrame(mapToGray(backgroundD));
        m_gainImage->setFrame(mapToGray(gainD, 0.01, 0.99));
        if (m_badPointSummary) {
            m_badPointSummary->setText(tr("坏点数量：%1").arg(badPoints.size()));
        }
        appendProgress(
            scalarStats(tr("低温"), lowD) + QStringLiteral("  |  ")
            + scalarStats(tr("高温"), highD) + QStringLiteral("  |  ")
            + scalarStats(QStringLiteral("Gain"), gainD) + QStringLiteral("  |  ")
            + tr("Flash 校验 %1 · 坏点 %2")
                  .arg(low == background ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                  .arg(badPoints.size()));
    } else {
        const QVector<quint16> background = decodeU16(m_payloads[0], false);
        const QVector<float> gain = decodeFloat32Le(m_payloads[1]);
        const QVector<QPoint> badPoints = decodeBadPoints(m_payloads[2]);
        QVector<double> backgroundD, gainD;
        backgroundD.reserve(background.size()); gainD.reserve(gain.size());
        for (quint16 value : background) backgroundD.append(value);
        for (float value : gain) gainD.append(value);
        m_backgroundImage->setFrame(mapToGray(backgroundD));
        m_gainImage->setFrame(mapToGray(gainD, 0.01, 0.99));
        if (m_badPointSummary) {
            m_badPointSummary->setText(tr("坏点数量：%1").arg(badPoints.size()));
        }
        appendProgress(
            scalarStats(tr("背景"), backgroundD) + QStringLiteral("  |  ")
            + scalarStats(QStringLiteral("Gain"), gainD) + QStringLiteral("  |  ")
            + tr("坏点 %1").arg(badPoints.size()));
    }
}

} // namespace serein::scope::tn160ncm
