#include "T384CalibrationPage.h"
#include "T384CalibrationProtocol.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace serein::scope::t384 {
namespace {
constexpr double kLowC = 0.0, kHighC = 50.0;
QString profileLabel(int w, int h) {
    if (w == 256 && h == 192) return QStringLiteral("T256 256x192");
    if (w == 384 && h == 288) return QStringLiteral("T384 384x288");
    if (w == 640 && h == 512) return QStringLiteral("T640 640x512");
    return QStringLiteral("unsupported");
}
std::optional<ThermalProfile> thermalProfile(int w, int h) {
    if (w == 256 && h == 192) return ThermalProfile::T256;
    if (w == 384 && h == 288) return ThermalProfile::T384;
    if (w == 640 && h == 512) return ThermalProfile::T640;
    return std::nullopt;
}
QJsonArray arrayOf(const QVector<double> &v) {
    QJsonArray a; for (double x : v) a.append(x); return a;
}
} // namespace

T384CalibrationPage::T384CalibrationPage(QWidget *parent) : QWidget(parent) {
    m_captureTimer.setSingleShot(true);
    connect(&m_captureTimer, &QTimer::timeout, this, [this] {
        if (m_state != State::CapturingZero && m_state != State::CapturingHigh)
            return;
        m_qualityReason = tr("等待完整Y16BE帧超时，请检查流连接后重新标定");
        m_state = State::QualityFailed;
        m_lastEvidence = buildEvidenceJson(QStringLiteral("capture_timeout"));
        emit evidenceCaptured(m_lastEvidence);
        updateUi();
    });
    m_contextPanel = new QWidget(this);
    auto *layout = new QVBoxLayout(m_contextPanel);
    layout->setContentsMargins(8, 8, 8, 8); layout->setSpacing(8);
    auto *title = new QLabel(tr("T256/T384/T640 产测双点标定"), m_contextPanel);
    title->setObjectName(QStringLiteral("pageTitle")); layout->addWidget(title);
    auto *warning = new QLabel(
        tr("按顺序操作：黑体稳定并覆盖中心ROI后采集0°C，再采集50°C；"
           "两点质量门通过后自动停流、写入A/B槽并回读验证。"
           "这是experimental / non-OEM模型。"),
        m_contextPanel);
    warning->setObjectName(QStringLiteral("pageSubtitle")); warning->setWordWrap(true); layout->addWidget(warning);
    auto *buttons = new QGridLayout;
    m_zeroButton = new QPushButton(m_contextPanel);
    m_highButton = new QPushButton(m_contextPanel);
    m_restartButton = new QPushButton(tr("重新标定"), m_contextPanel);
    m_importButton = new QPushButton(tr("导入 JSON"), m_contextPanel);
    m_exportButton = new QPushButton(tr("导出证据 JSON"), m_contextPanel);
    m_verifyButton = new QPushButton(tr("只读核验已保存标定"), m_contextPanel);
    buttons->addWidget(m_zeroButton, 0, 0);
    buttons->addWidget(m_highButton, 0, 1);
    buttons->addWidget(m_restartButton, 1, 0, 1, 2);
    buttons->addWidget(m_verifyButton, 2, 0, 1, 2);
    layout->addLayout(buttons);
    m_status = new QLabel(m_contextPanel); m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addStretch();

    m_resultPanel = new QWidget(this);
    m_resultPanel->setObjectName(QStringLiteral("t384CalibrationResults"));
    auto *resultLayout = new QVBoxLayout(m_resultPanel);
    resultLayout->setContentsMargins(8, 8, 8, 8);
    resultLayout->setSpacing(8);
    auto *resultActions = new QGridLayout;
    resultActions->setContentsMargins(0, 0, 0, 0);
    resultActions->setHorizontalSpacing(6);
    resultActions->addWidget(m_importButton, 0, 0);
    resultActions->addWidget(m_exportButton, 0, 1);
    resultActions->setColumnStretch(0, 1);
    resultActions->setColumnStretch(1, 1);
    resultLayout->addLayout(resultActions);
    m_live = new QLabel(tr("实时温度：—"), m_resultPanel);
    m_live->setObjectName(QStringLiteral("statusPill"));
    m_stats = new QLabel(m_resultPanel);
    m_stats->setWordWrap(true);
    m_stats->setTextInteractionFlags(Qt::TextSelectableByMouse
                                     | Qt::TextSelectableByKeyboard);
    m_boundarySummary = new QLabel(m_resultPanel);
    m_boundarySummary->setWordWrap(true);
    m_boundarySummary->setObjectName(QStringLiteral("pageSubtitle"));
    resultLayout->addWidget(m_live);
    resultLayout->addWidget(m_stats);
    resultLayout->addWidget(m_boundarySummary);
    resultLayout->addStretch(1);
    connect(m_zeroButton, &QPushButton::clicked, this, &T384CalibrationPage::startZeroCapture);
    connect(m_highButton, &QPushButton::clicked, this, &T384CalibrationPage::startHighCapture);
    connect(m_restartButton, &QPushButton::clicked, this, &T384CalibrationPage::restartCalibration);
    connect(m_importButton, &QPushButton::clicked, this, &T384CalibrationPage::importModel);
    connect(m_exportButton, &QPushButton::clicked, this, &T384CalibrationPage::exportModel);
    connect(m_verifyButton, &QPushButton::clicked, this, &T384CalibrationPage::verifySaved);
    updateUi();
}

void T384CalibrationPage::setDeviceIdentity(const QByteArray &identitySha256) {
    // Protocol boundary: never truncate PN/SN/FW text to pretend it is an ID.
    m_deviceIdentity = identitySha256.size() == 32 ? identitySha256 : QByteArray();
    updateUi();
}
bool T384CalibrationPage::supportedProfile(int w, int h) const {
    return (w == 256 && h == 192) || (w == 384 && h == 288) || (w == 640 && h == 512);
}
double T384CalibrationPage::roiMean(const QByteArray &frame, int w, int h) {
    if (frame.size() != w * h * 2) return 0.0;
    const int x0 = std::max(0, w / 2 - 8), y0 = std::max(0, h / 2 - 8);
    quint64 sum = 0; int n = 0;
    for (int y = y0; y < std::min(h, y0 + 16); ++y) for (int x = x0; x < std::min(w, x0 + 16); ++x) {
        const auto *p = reinterpret_cast<const uchar *>(frame.constData() + 2 * (y * w + x));
        sum += (quint16(p[0]) << 8) | quint16(p[1]); ++n;
    }
    return n ? double(sum) / n : 0.0;
}
double T384CalibrationPage::sampleStdDev(const QVector<double> &v) {
    if (v.size() < 2) return 0.0;
    double mean = 0; for (double x : v) mean += x; mean /= v.size();
    double sq = 0; for (double x : v) sq += (x - mean) * (x - mean);
    return std::sqrt(sq / (v.size() - 1));
}

void T384CalibrationPage::ingestFrame(const QByteArray &frame, int w, int h,
                                      quint32 sequence, quint32 captureMs) {
    if (!supportedProfile(w, h) || frame.size() != w * h * 2) {
        m_qualityReason = tr("忽略非完整或不支持的 Y16BE 帧"); updateUi(); return;
    }
    if ((m_state == State::CapturingZero || m_state == State::CapturingHigh) &&
        (w != m_frameWidth || h != m_frameHeight)) {
        m_qualityReason = tr("采集中分辨率变化，已丢弃本次采集"); resetCapture(); updateUi(); return;
    }
    QSet<quint32> *sequences = m_state == State::CapturingZero ? &m_zeroSequences :
                               m_state == State::CapturingHigh ? &m_highSequences : nullptr;
    if (sequences && sequence != 0) {
        if (sequences->contains(sequence)) {
            m_qualityReason = tr("忽略重复完整帧 sequence=%1").arg(sequence);
            updateUi();
            return;
        }
        if (m_lastSequence != 0 && sequence < m_lastSequence) {
            m_qualityReason = tr("检测到逆序帧，当前采集失败");
            m_state = State::QualityFailed;
            updateUi();
            return;
        }
        if (m_lastSequence != 0 && sequence > m_lastSequence + 1)
            m_sequenceGaps += sequence - m_lastSequence - 1;
        sequences->insert(sequence);
        m_lastSequence = sequence;
    }
    m_lastCaptureMs = captureMs;
    m_frameWidth = w; m_frameHeight = h; m_lastRaw = roiMean(frame, w, h);
    QVector<double> *samples = m_state == State::CapturingZero ? &m_zeroSamples :
                               m_state == State::CapturingHigh ? &m_highSamples : nullptr;
    if (samples && samples->size() < kSamplesPerPoint) {
        samples->append(m_lastRaw);
        m_captureTimer.start(15000);
        if (samples->size() == kSamplesPerPoint) {
            m_captureTimer.stop();
            if (m_state == State::CapturingZero) {
                m_state = State::AwaitingZeroAfter;
                emit boundaryStateRequested(QStringLiteral("low-after"));
            } else {
                m_state = State::AwaitingHighAfter;
                emit boundaryStateRequested(QStringLiteral("high-after"));
            }
        }
    }
    updateUi();
}
void T384CalibrationPage::ingestFrame(const QByteArray &frame) {
    const int n = frame.size();
    if (n == 256 * 192 * 2) ingestFrame(frame, 256, 192);
    else if (n == 384 * 288 * 2) ingestFrame(frame, 384, 288);
    else if (n == 640 * 512 * 2) ingestFrame(frame, 640, 512);
    else { m_qualityReason = tr("忽略未知尺寸帧"); updateUi(); }
}
void T384CalibrationPage::startZeroCapture() {
    if (m_state == State::Applying || m_state == State::WriteUncertain) return;
    emit calibrationRunStarted();
    m_zeroSamples.clear(); m_highSamples.clear(); m_zeroSequences.clear();
    m_highSequences.clear(); m_sequenceGaps = 0; m_lastSequence = 0;
    m_pendingPacket.clear(); m_modelEnabled = false; m_qualityReason.clear();
    m_zeroBefore = {}; m_zeroAfter = {}; m_highBefore = {}; m_highAfter = {};
    m_state = State::AwaitingZeroBefore;
    emit boundaryStateRequested(QStringLiteral("low-before"));
    updateUi();
}
void T384CalibrationPage::startHighCapture() {
    if (m_state != State::Idle || m_zeroSamples.size() != kSamplesPerPoint) return;
    m_highSamples.clear(); m_highSequences.clear(); m_lastSequence = 0;
    m_qualityReason.clear(); m_state = State::AwaitingHighBefore;
    emit boundaryStateRequested(QStringLiteral("high-before"));
    updateUi();
}

void T384CalibrationPage::onBoundaryStateReady(
    const QString &tag, const bool success, const QVariantMap &snapshot,
    const QString &message)
{
    if (!success) {
        m_qualityReason = tr("%1状态快照失败：%2").arg(tag, message);
        m_state = State::QualityFailed;
        m_lastEvidence = buildEvidenceJson(QStringLiteral("boundary_state_failed"));
        emit evidenceCaptured(m_lastEvidence);
        updateUi();
        return;
    }
    const QJsonObject state = QJsonObject::fromVariantMap(snapshot);
    if (tag == QStringLiteral("low-before") && m_state == State::AwaitingZeroBefore) {
        m_zeroBefore = state; m_state = State::CapturingZero;
        m_captureTimer.start(15000);
    } else if (tag == QStringLiteral("low-after") && m_state == State::AwaitingZeroAfter) {
        m_zeroAfter = state;
        QString reason;
        if (!validateBoundaryPair(m_zeroBefore, m_zeroAfter, &reason)) {
            m_qualityReason = reason; m_state = State::QualityFailed;
            m_lastEvidence = buildEvidenceJson(QStringLiteral("low_boundary_failed"));
            emit evidenceCaptured(m_lastEvidence);
        } else {
            m_state = State::Idle;
        }
    } else if (tag == QStringLiteral("high-before") && m_state == State::AwaitingHighBefore) {
        m_highBefore = state; m_state = State::CapturingHigh;
        m_captureTimer.start(15000);
    } else if (tag == QStringLiteral("high-after") && m_state == State::AwaitingHighAfter) {
        m_highAfter = state;
        QString reason;
        if (!validateBoundaryPair(m_highBefore, m_highAfter, &reason)) {
            m_qualityReason = reason; m_state = State::QualityFailed;
            m_lastEvidence = buildEvidenceJson(QStringLiteral("high_boundary_failed"));
            emit evidenceCaptured(m_lastEvidence);
        } else {
            m_state = State::Idle;
            evaluateAndAutoApply();
        }
    } else {
        return;
    }
    updateUi();
}

void T384CalibrationPage::onTransportInterrupted(const QString &message)
{
    if (m_state != State::CapturingZero && m_state != State::CapturingHigh)
        return;
    m_captureTimer.stop();
    m_qualityReason = tr("采集期间数据流中断：%1；本温点作废，请重新标定")
                          .arg(message);
    m_state = State::QualityFailed;
    m_lastEvidence = buildEvidenceJson(QStringLiteral("stream_interrupted"));
    emit evidenceCaptured(m_lastEvidence);
    updateUi();
}

void T384CalibrationPage::setRuntimeModel(const bool enabled,
                                          const double zeroRaw,
                                          const double countsPerC)
{
    m_modelEnabled = enabled && std::isfinite(zeroRaw)
        && std::isfinite(countsPerC) && countsPerC > 0.0;
    if (m_modelEnabled) {
        m_zeroRaw = zeroRaw;
        m_countsPerC = countsPerC;
    }
    emit modelChanged(m_modelEnabled, m_zeroRaw, m_countsPerC);
    updateUi();
}

bool T384CalibrationPage::validateBoundaryPair(const QJsonObject &before,
                                               const QJsonObject &after,
                                               QString *reason) const
{
    if (before.isEmpty() || after.isEmpty()
        || before.value(QStringLiteral("identity_sha256_hex"))
               != after.value(QStringLiteral("identity_sha256_hex"))
        || before.value(QStringLiteral("identity_sha256_hex")).toString().toLatin1()
               != m_deviceIdentity.toHex()) {
        *reason = tr("采集前后设备身份不一致"); return false;
    }
    const QString beforeGain = before.value(QStringLiteral("gain")).toString();
    const QString afterGain = after.value(QStringLiteral("gain")).toString();
    if (beforeGain != afterGain
        || (beforeGain != QStringLiteral("unknown")
            && beforeGain != QStringLiteral("high")
            && beforeGain != QStringLiteral("low"))) {
        *reason = tr("采集前后gain变化或不支持"); return false;
    }
    if (before.value(QStringLiteral("auto_ffc_enabled"))
            != after.value(QStringLiteral("auto_ffc_enabled"))
        || before.value(QStringLiteral("shutter_status")).toInt() != 1
        || after.value(QStringLiteral("shutter_status")).toInt() != 1) {
        *reason = tr("采集边界FFC配置变化或快门状态无效"); return false;
    }
    return true;
}

bool T384CalibrationPage::qualityGate(QString *reason) {
    if (m_zeroSamples.size() != kSamplesPerPoint || m_highSamples.size() != kSamplesPerPoint) {
        *reason = tr("两点各需30完整帧"); return false;
    }
    auto mean = [](const QVector<double> &v) { double s = 0; for (double x : v) s += x; return s / v.size(); };
    m_zeroRaw = mean(m_zeroSamples); m_highRaw = mean(m_highSamples);
    m_zeroStdDev = sampleStdDev(m_zeroSamples); m_highStdDev = sampleStdDev(m_highSamples);
    const double delta = m_highRaw - m_zeroRaw;
    if (!std::isfinite(delta) || delta <= 0) { *reason = tr("两点响应必须为正"); return false; }
    if (!passesTwoPointQualityGate(m_zeroRaw, m_highRaw,
                                   m_zeroStdDev, m_highStdDev)) {
        *reason = tr("两点差值未超过最大帧间标准差的6倍"); return false;
    }
    const QString lowGain = m_zeroBefore.value(QStringLiteral("gain")).toString();
    const QString highGain = m_highBefore.value(QStringLiteral("gain")).toString();
    if (lowGain.isEmpty() || lowGain != highGain
        || m_zeroBefore.value(QStringLiteral("auto_ffc_enabled"))
               != m_highBefore.value(QStringLiteral("auto_ffc_enabled"))) {
        *reason = tr("0°C与50°C的gain或自动FFC配置不一致"); return false;
    }
    m_countsPerC = delta / (kHighC - kLowC);
    if (!std::isfinite(m_countsPerC) || m_countsPerC <= 0) { *reason = tr("拟合系数无效"); return false; }
    return true;
}
QByteArray T384CalibrationPage::buildModelPacket() const {
    const auto profile = thermalProfile(m_frameWidth, m_frameHeight);
    if (!profile || m_deviceIdentity.size() != 32) return {};
    CalibrationPacket packet;
    packet.manifest.calibrationId = quint32(QDateTime::currentSecsSinceEpoch()) | 1u;
    packet.manifest.profile = *profile;
    packet.manifest.identity = m_deviceIdentity;
    const QString gain = m_zeroBefore.value(QStringLiteral("gain")).toString();
    packet.manifest.gain = gain == QStringLiteral("high") ? 1u
        : gain == QStringLiteral("low") ? 2u : 0xffu;
    packet.payload.lowC = quint16(kLowC);
    packet.payload.highC = quint16(kHighC);
    packet.payload.lowRaw = quint32(std::llround(m_zeroRaw));
    packet.payload.highRaw = quint32(std::llround(m_highRaw));
    packet.payload.countsPerCX1000 = qint32(std::llround(
        (double(packet.payload.highRaw) - packet.payload.lowRaw) * 1000.0 /
        (packet.payload.highC - packet.payload.lowC)));
    return encodeCalibrationPacket(packet).value_or(QByteArray{});
}
QByteArray T384CalibrationPage::buildEvidenceJson(const QString &outcome) const {
    const QString gain = m_zeroBefore.value(QStringLiteral("gain")).toString(
        QStringLiteral("unknown"));
    const bool gainVerified = gain == QStringLiteral("high")
        || gain == QStringLiteral("low");
    QJsonObject o{{"schema","t384-production-test-evidence-v1"},
        {"created_utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"outcome",outcome},{"experimental",true},{"oem_radiometry",false},
        {"profile",thermalProfile(m_frameWidth,m_frameHeight)
                       ? profileName(*thermalProfile(m_frameWidth,m_frameHeight))
                       : QStringLiteral("unsupported")},
        {"identity_sha256_hex",QString::fromLatin1(m_deviceIdentity.toHex())},
        {"roi","center-16x16"},{"distance_m",0.01},{"emissivity",0.98},
        {"gain",gain},{"gain_verified",gainVerified},
        {"low_c",kLowC},{"high_c",kHighC},
        {"zero_samples",arrayOf(m_zeroSamples)},{"high_samples",arrayOf(m_highSamples)},
        {"zero_mean",m_zeroRaw},{"high_mean",m_highRaw},
        {"zero_stddev",m_zeroStdDev},{"high_stddev",m_highStdDev},
        {"counts_per_c",m_countsPerC},{"sequence_gaps",qint64(m_sequenceGaps)},
        {"last_capture_ms",int(m_lastCaptureMs)},
        {"module_states",QJsonObject{{"low_before",m_zeroBefore},
                                      {"low_after",m_zeroAfter},
                                      {"high_before",m_highBefore},
                                      {"high_after",m_highAfter}}},
        {"quality_reason",m_qualityReason}};
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}
void T384CalibrationPage::evaluateAndAutoApply() {
    QString reason;
    if (!qualityGate(&reason)) {
        m_qualityReason = reason; m_state = State::QualityFailed;
        m_lastEvidence = buildEvidenceJson(QStringLiteral("quality_failed")); emit evidenceCaptured(m_lastEvidence); return;
    }
    if (m_deviceIdentity.size() != 32) {
        m_qualityReason = tr("未获得32字节 SHA-256 设备身份，禁止写入"); m_state = State::QualityFailed;
        m_lastEvidence = buildEvidenceJson(QStringLiteral("identity_failed")); emit evidenceCaptured(m_lastEvidence); return;
    }
    m_pendingPacket = buildModelPacket();
    if (m_pendingPacket.size() != kCalibrationPacketBytes) {
        m_qualityReason = tr("无法生成严格标定包，禁止写入");
        m_state = State::QualityFailed;
        m_lastEvidence = buildEvidenceJson(QStringLiteral("packet_failed"));
        emit evidenceCaptured(m_lastEvidence);
        return;
    }
    m_state = State::Applying; m_lastEvidence = buildEvidenceJson(QStringLiteral("auto_apply_requested"));
    emit evidenceCaptured(m_lastEvidence); emit autoApplyRequested(m_pendingPacket, m_lastEvidence);
}
void T384CalibrationPage::onAutoApplyFinished(bool success, bool uncertain, const QString &message) {
    if (m_state != State::Applying && m_state != State::WriteUncertain) return;
    m_qualityReason = message;
    m_state = uncertain ? State::WriteUncertain : success ? State::Applied : State::QualityFailed;
    m_modelEnabled = success;
    emit modelChanged(m_modelEnabled, m_zeroRaw, m_countsPerC);
    m_lastEvidence = buildEvidenceJson(uncertain ? QStringLiteral("write_uncertain") :
        success ? QStringLiteral("applied_verified") : QStringLiteral("apply_failed"));
    emit evidenceCaptured(m_lastEvidence); updateUi();
}
void T384CalibrationPage::resetCapture() {
    m_captureTimer.stop();
    m_zeroSamples.clear(); m_highSamples.clear(); m_zeroRaw=m_highRaw=m_countsPerC=m_zeroStdDev=m_highStdDev=0;
    m_zeroSequences.clear(); m_highSequences.clear(); m_sequenceGaps=0;
    m_lastSequence=0; m_lastCaptureMs=0; m_pendingPacket.clear();
    m_zeroBefore={}; m_zeroAfter={}; m_highBefore={}; m_highAfter={};
    m_modelEnabled=false; m_state=State::Idle;
}
void T384CalibrationPage::restartCalibration() { resetCapture(); m_qualityReason.clear(); updateUi(); }

void T384CalibrationPage::updateUi() {
    m_zeroButton->setText(tr("1、对准 0°C（%1/%2）")
                              .arg(m_zeroSamples.size()).arg(kSamplesPerPoint));
    m_highButton->setText(tr("2、对准 50°C（%1/%2）")
                              .arg(m_highSamples.size()).arg(kSamplesPerPoint));
    const bool busy = m_state == State::AwaitingZeroBefore
        || m_state == State::CapturingZero || m_state == State::AwaitingZeroAfter
        || m_state == State::AwaitingHighBefore || m_state == State::CapturingHigh
        || m_state == State::AwaitingHighAfter || m_state == State::Applying;
    m_zeroButton->setEnabled(!busy && m_state != State::WriteUncertain
                             && m_deviceIdentity.size() == 32
                             && supportedProfile(m_frameWidth, m_frameHeight));
    m_highButton->setEnabled(!busy && m_zeroSamples.size() == kSamplesPerPoint && m_state != State::WriteUncertain);
    m_importButton->setEnabled(!busy); m_exportButton->setEnabled(!busy);
    m_restartButton->setEnabled(m_state != State::Applying && m_state != State::WriteUncertain);
    m_verifyButton->setEnabled(m_state == State::WriteUncertain && !m_pendingPacket.isEmpty());
    QString status;
    if (m_state == State::AwaitingZeroBefore) status=tr("正在核验0°C采集前gain/FFC/Vtemp/快门状态");
    else if (m_state == State::CapturingZero) status=tr("采集中：0°C，等待30个完整Y16BE帧");
    else if (m_state == State::AwaitingZeroAfter) status=tr("正在核验0°C采集后状态");
    else if (m_state == State::AwaitingHighBefore) status=tr("正在核验50°C采集前gain/FFC/Vtemp/快门状态");
    else if (m_state == State::CapturingHigh) status=tr("采集中：50°C，等待30个完整Y16BE帧");
    else if (m_state == State::AwaitingHighAfter) status=tr("正在核验50°C采集后状态");
    else if (m_state == State::Applying) status=tr("质量门通过，正在自动写入并验证；所有冲突操作已禁用");
    else if (m_state == State::Applied) status=tr("写入并回读验证通过（experimental / non-OEM）");
    else if (m_state == State::WriteUncertain) status=tr("WRITE UNCERTAIN：禁止重写；请重新连接后只读验证");
    else if (m_state == State::QualityFailed) status=tr("未通过：%1；请重新标定").arg(m_qualityReason);
    else if (m_deviceIdentity.size() != 32 || !supportedProfile(m_frameWidth, m_frameHeight))
        status=tr("等待真实T256/T384/T640 Y16设备身份与完整帧");
    else status=tr("就绪：先采集0°C，再采集50°C；通过后自动写入");
    m_status->setText(status);
    m_stats->setText(tr("%1 · ROI中心16×16 · low=%2±%3 high=%4±%5 counts/°C=%6 · 整帧跳号=%7")
        .arg(profileLabel(m_frameWidth,m_frameHeight)).arg(m_zeroRaw,0,'f',3).arg(m_zeroStdDev,0,'f',3)
        .arg(m_highRaw,0,'f',3).arg(m_highStdDev,0,'f',3).arg(m_countsPerC,0,'f',3)
        .arg(m_sequenceGaps));
    m_live->setText(m_modelEnabled && m_countsPerC>0 ?
        tr("实时温度：%1 °C（实验模型）").arg((m_lastRaw-m_zeroRaw)/m_countsPerC,0,'f',2) : tr("实时温度：—"));
    const auto stateText = [](const QJsonObject &state) {
        if (state.isEmpty()) return QStringLiteral("—");
        return QStringLiteral("gain=%1 · Vtemp=%2 · FFC=%3 · shutter=%4")
            .arg(state.value(QStringLiteral("gain")).toString(QStringLiteral("unknown")))
            .arg(state.value(QStringLiteral("vtemp_raw")).toInt())
            .arg(state.value(QStringLiteral("auto_ffc_enabled")).toBool()
                     ? QStringLiteral("on") : QStringLiteral("off"))
            .arg(state.value(QStringLiteral("shutter_status")).toInt());
    };
    m_boundarySummary->setText(
        tr("0°C 前：%1\n0°C 后：%2\n50°C 前：%3\n50°C 后：%4")
            .arg(stateText(m_zeroBefore), stateText(m_zeroAfter),
                 stateText(m_highBefore), stateText(m_highAfter)));
}
QByteArray T384CalibrationPage::captureEvidenceJson() const { return m_lastEvidence; }
bool T384CalibrationPage::saveJson(const QString &path) const {
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(m_lastEvidence) >= 0;
}
bool T384CalibrationPage::loadJson(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o=QJsonDocument::fromJson(f.readAll()).object();
    // Import may be inspected/exported but never enters Applying or writes.
    if (o.value("schema").toString()!=QStringLiteral("t384-production-test-evidence-v1") || o.value("oem_radiometry").toBool(true)) return false;
    m_lastEvidence=QJsonDocument(o).toJson(QJsonDocument::Indented);
    m_qualityReason=tr("已导入证据，仅供查看；不会自动写入设备"); return true;
}
void T384CalibrationPage::importModel() {
    const QString p=QFileDialog::getOpenFileName(this,tr("导入产测证据"),{},tr("JSON (*.json)")); if (p.isEmpty()) return;
    if (!loadJson(p))
        m_qualityReason=tr("导入失败：需要experimental产测证据JSON");
    updateUi();
}
void T384CalibrationPage::exportModel() {
    const QString p=QFileDialog::getSaveFileName(this,tr("导出产测证据"),QStringLiteral("t384-production-test-evidence.json"),tr("JSON (*.json)"));
    if (!p.isEmpty() && !saveJson(p)) { m_qualityReason=tr("导出失败"); updateUi(); }
}
void T384CalibrationPage::verifySaved() {
    if (m_state != State::WriteUncertain || m_pendingPacket.isEmpty()) return;
    m_status->setText(tr("正在执行只读核验；不会再次写入"));
    emit verifySavedRequested(m_pendingPacket, m_lastEvidence);
}
} // namespace serein::scope::t384
