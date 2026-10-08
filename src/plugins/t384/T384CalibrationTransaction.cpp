#include "T384CalibrationTransaction.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QStringList>
#include <QtCore/QUrlQuery>
#include <QtCore/QVariantMap>

namespace serein::scope::t384 {
namespace {
constexpr int RequestTimeoutMs = 5000;
constexpr auto CalibrationBase = "/api/v1/calibration/v1/";

bool sameStoredManifest(const QJsonObject &left, const QJsonObject &right)
{
    static const QStringList keys = {
        QStringLiteral("schema"), QStringLiteral("generation"),
        QStringLiteral("payload_len"), QStringLiteral("payload_crc32"),
        QStringLiteral("header_crc32"), QStringLiteral("calibration_id"),
        QStringLiteral("model"), QStringLiteral("profile"),
        QStringLiteral("identity"), QStringLiteral("gain")};
    for (const QString &key : keys)
        if (left.value(key) != right.value(key)) return false;
    return true;
}
} // namespace

T384CalibrationTransaction::T384CalibrationTransaction(HttpRequestClient *http,
                                                       QObject *parent)
    : QObject(parent), m_http(http)
{
    Q_ASSERT(m_http);
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this,
            &T384CalibrationTransaction::onRequestTimeout);
    connect(m_http, &HttpRequestClient::completed, this,
            &T384CalibrationTransaction::onHttpCompleted);
}

T384CalibrationTransaction::~T384CalibrationTransaction() { shutdown(); }

bool T384CalibrationTransaction::active() const noexcept
{
    return m_phase != Phase::Idle && m_phase != Phase::Done;
}

bool T384CalibrationTransaction::startWrite(const ExpectedDevice &expected,
                                             const QByteArray &packet)
{
    return begin(expected, packet, false);
}

bool T384CalibrationTransaction::startVerify(const ExpectedDevice &expected,
                                              const QByteArray &packet)
{
    return begin(expected, packet, true);
}

bool T384CalibrationTransaction::begin(const ExpectedDevice &expected,
                                       const QByteArray &packet,
                                       const bool verifyOnly)
{
    if (!m_http || active() || expected.endpoint.scheme() != QStringLiteral("http")
        || expected.endpoint.host().isEmpty() || expected.pn.isEmpty()
        || expected.sn.isEmpty() || expected.fwHex.isEmpty()
        || !profileFromName(expected.profile.toLatin1())) return false;

    ProtocolError protocolError;
    const QByteArray identity = calibrationIdentity(expected.pn.toUtf8(),
                                                    expected.sn.toUtf8(),
                                                    expected.fwHex.toLatin1());
    const auto decoded = decodeCalibrationPacket(packet, identity, &protocolError);
    if (!decoded || profileName(decoded->manifest.profile) != expected.profile) {
        m_result = {{QStringLiteral("stage"), QStringLiteral("input")},
                    {QStringLiteral("error"), decoded
                        ? QStringLiteral("packet profile mismatch")
                        : protocolError.message}};
        return false;
    }

    m_expected = expected;
    m_packet = packet;
    m_decoded = *decoded;
    m_payload = packet.mid(kCalibrationManifestBytes);
    m_previousPacket.clear();
    m_backupPayload.clear();
    m_backupManifest = {};
    m_verifyManifest = {};
    m_result = {{QStringLiteral("stage"), QStringLiteral("preflight")},
                {QStringLiteral("write_attempted"), false},
                {QStringLiteral("stored"), QJsonValue::Null},
                {QStringLiteral("applied"), false},
                {QStringLiteral("verification_only"), verifyOnly},
                {QStringLiteral("cold_boot_verified"), false},
                {QStringLiteral("independent_accuracy_verified"), false},
                {QStringLiteral("production_pass"), false},
                {QStringLiteral("oem_radiometry_ready"), false}};
    m_verifyOnly = verifyOnly;
    m_stagingMayExist = false;
    m_commitDispatched = false;
    m_shutdown = false;
    requestGet(Phase::Diag, QStringLiteral("/diag"));
    return true;
}

void T384CalibrationTransaction::cancel()
{
    if (!active()) return;
    m_timeout.stop();
    if (m_requestId) m_http->cancel(m_requestId);
    m_requestId = 0;
    if (m_commitDispatched) {
        finish(Outcome::WriteUncertain,
               QStringLiteral("commit已发出，取消不能证明Flash未改变；只能只读核验"));
        return;
    }
    bestEffortAbortStaging();
    finish(Outcome::Cancelled, QStringLiteral("提交前已取消；暂存区已请求清理"));
}

void T384CalibrationTransaction::shutdown()
{
    if (m_shutdown) return;
    m_shutdown = true;
    cancel();
}

void T384CalibrationTransaction::requestGet(const Phase phase,
                                             const QString &path)
{
    m_phase = phase;
    emit progress(phase, path);
    m_requestId = m_http->get(api(path), {{"Accept", "application/json"}});
    m_timeout.start(RequestTimeoutMs);
}

void T384CalibrationTransaction::requestPut(const Phase phase,
                                             const QString &path,
                                             const QByteArray &body)
{
    m_phase = phase;
    emit progress(phase, path);
    m_stagingMayExist = true;
    m_requestId = m_http->put(api(path), body,
        {{"Content-Type", "application/octet-stream"},
         {"Accept", "application/json"}});
    m_timeout.start(RequestTimeoutMs);
}

void T384CalibrationTransaction::requestPost(const Phase phase,
                                              const QString &path)
{
    m_phase = phase;
    emit progress(phase, path);
    if (phase == Phase::Commit) m_commitDispatched = true;
    m_requestId = m_http->post(api(path), {}, {{"Accept", "application/json"}});
    m_timeout.start(RequestTimeoutMs);
}

void T384CalibrationTransaction::onHttpCompleted(
    const HttpRequestClient::RequestId requestId, const int status,
    const QByteArray &body, const QString &networkError)
{
    if (requestId != m_requestId || !active()) return;
    m_timeout.stop();
    m_requestId = 0;
    if (!networkError.isEmpty() || status == 0) {
        transportFailure(networkError.isEmpty()
            ? QStringLiteral("HTTP未返回状态") : networkError);
        return;
    }
    advance(status, body);
}

void T384CalibrationTransaction::onRequestTimeout()
{
    if (!active()) return;
    if (m_requestId) m_http->cancel(m_requestId);
    m_requestId = 0;
    transportFailure(QStringLiteral("HTTP请求超过%1 ms").arg(RequestTimeoutMs));
}

void T384CalibrationTransaction::advance(const int status,
                                         const QByteArray &body)
{
    QString why;
    switch (m_phase) {
    case Phase::Diag:
        if (status != 200 || !validateDiag(body, &why))
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("/diag HTTP %1").arg(status) : why);
        requestGet(Phase::Status,
                   QString::fromLatin1(CalibrationBase) + QStringLiteral("status"));
        return;
    case Phase::Status: {
        const QJsonObject object = parseObject(body, &why);
        if (status != 200 || !validateStorageStatus(object, false, &why))
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("storage status HTTP %1").arg(status) : why);
        requestGet(m_verifyOnly ? Phase::VerifyManifest1 : Phase::BackupManifest,
                   QString::fromLatin1(CalibrationBase) + QStringLiteral("manifest"));
        return;
    }
    case Phase::BackupManifest:
        if (status == 404) {
            emit previousPacketReady({});
            requestPut(Phase::Upload,
                       QString::fromLatin1(CalibrationBase) + QStringLiteral("data"),
                       m_packet);
            return;
        }
        m_backupManifest = parseObject(body, &why);
        if (status != 200 || !validateManifest(m_backupManifest, false, &why))
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("backup manifest HTTP %1").arg(status) : why);
        requestGet(Phase::BackupData,
                   QString::fromLatin1(CalibrationBase) + QStringLiteral("data"));
        return;
    case Phase::BackupData:
        if (status != 200
            || quint32(body.size())
                   != jsonUInt(m_backupManifest, QStringLiteral("payload_len"))
            || crc32(body)
                   != jsonUInt(m_backupManifest, QStringLiteral("payload_crc32")))
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("backup data HTTP %1").arg(status) : why);
        m_backupPayload = body;
        requestGet(Phase::BackupManifest2,
                   QString::fromLatin1(CalibrationBase) + QStringLiteral("manifest"));
        return;
    case Phase::BackupManifest2: {
        const QJsonObject second = parseObject(body, &why);
        if (status != 200 || !validateManifest(second, false, &why)
            || !sameStoredManifest(m_backupManifest, second)
            || !reconstructPreviousPacket(m_backupPayload, &why))
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("旧标定在备份期间发生变化") : why);
        emit previousPacketReady(m_previousPacket);
        requestPut(Phase::Upload,
                   QString::fromLatin1(CalibrationBase) + QStringLiteral("data"),
                   m_packet);
        return;
    }
    case Phase::Upload: {
        const QJsonObject object = parseObject(body, &why);
        if (status != 200 || !object.value(QStringLiteral("staged")).toBool())
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("上传被拒绝：HTTP %1").arg(status) : why);
        m_result[QStringLiteral("write_attempted")] = true;
        m_result[QStringLiteral("stage")] = QStringLiteral("commit");
        requestPost(Phase::Commit,
                    QString::fromLatin1(CalibrationBase) + QStringLiteral("commit"));
        return;
    }
    case Phase::Commit: {
        const QJsonObject object = parseObject(body, &why);
        const bool committed = object.value(QStringLiteral("committed")).toBool();
        if (status == 200 && committed)
            m_result[QStringLiteral("stored")] = true;
        if (status != 200 || !committed
            || !object.value(QStringLiteral("applied")).toBool())
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("commit明确拒绝或运行时未加载") : why);
        m_stagingMayExist = false;
        m_result[QStringLiteral("stored")] = true;
        m_result[QStringLiteral("stage")] = QStringLiteral("verify-readback");
        requestGet(Phase::VerifyManifest1,
                   QString::fromLatin1(CalibrationBase) + QStringLiteral("manifest"));
        return;
    }
    case Phase::VerifyManifest1:
        m_verifyManifest = parseObject(body, &why);
        if (status != 200 || !validateManifest(m_verifyManifest, true, &why))
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("verify manifest HTTP %1").arg(status) : why);
        requestGet(Phase::VerifyData,
                   QString::fromLatin1(CalibrationBase) + QStringLiteral("data"));
        return;
    case Phase::VerifyData:
        if (status != 200 || body != m_payload)
            return definitiveFailure(QStringLiteral("回读payload与本次模型不一致"));
        requestGet(Phase::VerifyManifest2,
                   QString::fromLatin1(CalibrationBase) + QStringLiteral("manifest"));
        return;
    case Phase::VerifyManifest2: {
        const QJsonObject second = parseObject(body, &why);
        if (status != 200 || !validateManifest(second, true, &why)
            || !sameStoredManifest(m_verifyManifest, second))
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("两次manifest之间发生变化") : why);
        requestGet(Phase::VerifyStatus,
                   QString::fromLatin1(CalibrationBase) + QStringLiteral("status"));
        return;
    }
    case Phase::VerifyStatus: {
        const QJsonObject object = parseObject(body, &why);
        if (status != 200 || !validateStorageStatus(object, true, &why))
            return definitiveFailure(why.isEmpty()
                ? QStringLiteral("verify status HTTP %1").arg(status) : why);
        m_result[QStringLiteral("stage")] = QStringLiteral("complete");
        m_result[QStringLiteral("stored")] = true;
        m_result[QStringLiteral("applied")] = true;
        m_result[QStringLiteral("generation")] =
            qint64(jsonUInt(m_verifyManifest, QStringLiteral("generation")));
        m_result[QStringLiteral("calibration_id")] =
            qint64(m_decoded.manifest.calibrationId);
        finish(Outcome::Succeeded,
               m_verifyOnly ? QStringLiteral("只读核验通过；未执行写入")
                            : QStringLiteral("标定已写入、双manifest回读一致且运行时已加载"));
        return;
    }
    default:
        return;
    }
}

void T384CalibrationTransaction::transportFailure(const QString &detail)
{
    if (m_commitDispatched || m_verifyOnly) {
        m_result[QStringLiteral("stage")] = QStringLiteral("write-uncertain");
        m_result[QStringLiteral("error")] = detail;
        finish(Outcome::WriteUncertain,
               QStringLiteral("写入结果无法证明：%1；禁止重写，只能只读核验")
                   .arg(detail));
        return;
    }
    definitiveFailure(detail);
}

void T384CalibrationTransaction::definitiveFailure(const QString &detail)
{
    if (!m_commitDispatched) bestEffortAbortStaging();
    m_result[QStringLiteral("stage")] = QStringLiteral("failed");
    m_result[QStringLiteral("error")] = detail;
    finish(Outcome::Failed, detail);
}

void T384CalibrationTransaction::finish(const Outcome outcome,
                                        const QString &detail)
{
    if (m_phase == Phase::Idle || m_phase == Phase::Done) return;
    m_timeout.stop();
    m_phase = Phase::Done;
    m_requestId = 0;
    emit complete(outcome, detail, m_result);
}

void T384CalibrationTransaction::bestEffortAbortStaging()
{
    if (!m_http || !m_stagingMayExist || m_commitDispatched) return;
    (void)m_http->post(api(QString::fromLatin1(CalibrationBase)
                           + QStringLiteral("abort")), {},
                       {{"Accept", "application/json"}});
    m_stagingMayExist = false;
}

QUrl T384CalibrationTransaction::api(const QString &path) const
{
    QUrl result = m_expected.endpoint;
    result.setPath(path);
    result.setQuery(QUrlQuery{});
    result.setFragment({});
    return result;
}

bool T384CalibrationTransaction::validateDiag(const QByteArray &body,
                                              QString *why) const
{
    const QVariantMap diag = parseDiag(body);
    const QStringList dimensions = m_expected.profile.split(QLatin1Char('x'));
    if (dimensions.size() != 2
        || diag.value(QStringLiteral("mini2.pn")).toString() != m_expected.pn
        || diag.value(QStringLiteral("mini2.sn")).toString() != m_expected.sn
        || diag.value(QStringLiteral("mini2.firmware_version")).toString()
                   .toLatin1().toHex()
               != m_expected.fwHex.toLatin1()
        || diag.value(QStringLiteral("dvp.expected_width")).toString()
               != dimensions[0]
        || diag.value(QStringLiteral("dvp.expected_height")).toString()
               != dimensions[1]
        || diag.value(QStringLiteral("source.pixel_format")).toString()
               != QStringLiteral("2")
        || diag.value(QStringLiteral("source.stream_ready")).toString()
               != QStringLiteral("1")
        || diag.value(QStringLiteral("source.synthetic")).toString()
               != QStringLiteral("0")
        || diag.value(QStringLiteral("stream.active")).toString()
               != QStringLiteral("0")) {
        *why = QStringLiteral("实时设备身份/profile/Y16/停流状态与采集证据不匹配");
        return false;
    }
    return true;
}

bool T384CalibrationTransaction::validateStorageStatus(
    const QJsonObject &status, const bool final, QString *why) const
{
    if (status.value(QStringLiteral("init_status")).toString()
            != QStringLiteral("ok")
        || jsonUInt(status, QStringLiteral("slot0_addr")) != 0x50000u
        || jsonUInt(status, QStringLiteral("slot1_addr")) != 0x52000u
        || jsonUInt(status, QStringLiteral("slot_size")) != 0x1000u) {
        *why = QStringLiteral("设备标定A/B布局或初始化状态不匹配");
        return false;
    }
    if (!final) return true;
    const qint64 countsX100 =
        (qint64(m_decoded.payload.countsPerCX1000) + 5) / 10;
    const qint64 zeroX100 = qint64(m_decoded.payload.lowRaw) * 100
        - qint64(m_decoded.payload.lowC) * countsX100;
    if (jsonUInt(status, QStringLiteral("has_valid_slot")) == 0u
        || jsonUInt(status, QStringLiteral("generation"))
               != jsonUInt(m_verifyManifest, QStringLiteral("generation"))
        || !status.value(QStringLiteral("runtime_model_available")).toBool()
        || status.value(QStringLiteral("runtime_model")).toString()
               != QStringLiteral("t384-empirical-2point-v1")
        || status.value(QStringLiteral("runtime_zero_c_x100")).toVariant()
                   .toLongLong()
               != zeroX100
        || status.value(QStringLiteral("runtime_counts_per_c_x100")).toVariant()
                   .toLongLong()
               != countsX100) {
        *why = QStringLiteral("Flash可读但运行时模型/generation/参数不匹配");
        return false;
    }
    return true;
}

bool T384CalibrationTransaction::validateManifest(const QJsonObject &manifest,
                                                  const bool expected,
                                                  QString *why) const
{
    if (jsonUInt(manifest, QStringLiteral("schema")) != kCalibrationSchema
        || jsonUInt(manifest, QStringLiteral("payload_len")) > 2048u
        || manifest.value(QStringLiteral("identity")).toString().size() != 64) {
        *why = QStringLiteral("manifest结构非法");
        return false;
    }
    if (!expected) return true;
    if (manifest.value(QStringLiteral("model")).toString()
            != QStringLiteral("t384-empirical-2point-v1")
        || manifest.value(QStringLiteral("profile")).toString()
               != m_expected.profile
        || QByteArray::fromHex(manifest.value(QStringLiteral("identity"))
                                   .toString().toLatin1())
               != m_decoded.manifest.identity
        || jsonUInt(manifest, QStringLiteral("gain"))
               != m_decoded.manifest.gain
        || jsonUInt(manifest, QStringLiteral("payload_len"))
               != quint32(m_payload.size())
        || jsonUInt(manifest, QStringLiteral("payload_crc32"))
               != crc32(m_payload)
        || jsonUInt(manifest, QStringLiteral("calibration_id"))
               != m_decoded.manifest.calibrationId
        || !manifest.value(QStringLiteral("applied")).toBool()) {
        *why = QStringLiteral("manifest与本次身份/profile/payload/应用状态不匹配");
        return false;
    }
    const quint32 generation = jsonUInt(manifest, QStringLiteral("generation"));
    if (generation == 0u
        || (!m_verifyOnly && !m_backupManifest.isEmpty()
            && generation
                <= jsonUInt(m_backupManifest, QStringLiteral("generation")))) {
        *why = QStringLiteral("标定generation未有效递增");
        return false;
    }
    return true;
}

bool T384CalibrationTransaction::reconstructPreviousPacket(
    const QByteArray &payload, QString *why)
{
    if (quint32(payload.size())
            != jsonUInt(m_backupManifest, QStringLiteral("payload_len"))
        || crc32(payload)
            != jsonUInt(m_backupManifest, QStringLiteral("payload_crc32"))) {
        *why = QStringLiteral("旧标定payload长度或CRC不匹配");
        return false;
    }
    const auto profile = profileFromName(
        m_backupManifest.value(QStringLiteral("profile")).toString().toLatin1());
    if (!profile || payload.size() != kEmpirical2PointBytes) {
        *why = QStringLiteral("旧标定profile或payload版本不支持备份");
        return false;
    }
    CalibrationPacket previous;
    previous.manifest.generation =
        jsonUInt(m_backupManifest, QStringLiteral("generation"));
    previous.manifest.calibrationId =
        jsonUInt(m_backupManifest, QStringLiteral("calibration_id"));
    previous.manifest.model =
        m_backupManifest.value(QStringLiteral("model")).toString().toLatin1();
    previous.manifest.profile = *profile;
    previous.manifest.identity = QByteArray::fromHex(
        m_backupManifest.value(QStringLiteral("identity")).toString().toLatin1());
    previous.manifest.gain =
        quint8(jsonUInt(m_backupManifest, QStringLiteral("gain")));
    previous.payload.lowC = quint16(quint8(payload[0]))
        | quint16(quint8(payload[1])) << 8;
    previous.payload.highC = quint16(quint8(payload[2]))
        | quint16(quint8(payload[3])) << 8;
    const auto le32 = [&payload](const int offset) {
        return quint32(quint8(payload[offset]))
            | quint32(quint8(payload[offset + 1])) << 8
            | quint32(quint8(payload[offset + 2])) << 16
            | quint32(quint8(payload[offset + 3])) << 24;
    };
    previous.payload.lowRaw = le32(4);
    previous.payload.highRaw = le32(8);
    previous.payload.countsPerCX1000 = qint32(le32(12));
    ProtocolError error;
    const auto encoded = encodeCalibrationPacket(previous, &error);
    if (!encoded
        || jsonUInt(m_backupManifest, QStringLiteral("header_crc32"))
               != quint32(quint8((*encoded)[20]))
                    + (quint32(quint8((*encoded)[21])) << 8)
                    + (quint32(quint8((*encoded)[22])) << 16)
                    + (quint32(quint8((*encoded)[23])) << 24)) {
        *why = error.message.isEmpty()
            ? QStringLiteral("旧标定header CRC无法重建") : error.message;
        return false;
    }
    m_previousPacket = *encoded;
    return true;
}

QVariantMap T384CalibrationTransaction::parseDiag(const QByteArray &body)
{
    QVariantMap result;
    for (const QByteArray &line : body.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        const qsizetype separator = trimmed.indexOf('=');
        if (separator <= 0) continue;
        result.insert(QString::fromUtf8(trimmed.first(separator)).trimmed(),
                      QString::fromUtf8(trimmed.sliced(separator + 1)).trimmed());
    }
    return result;
}

QJsonObject T384CalibrationTransaction::parseObject(const QByteArray &body,
                                                     QString *why)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        *why = QStringLiteral("JSON响应非法：%1").arg(error.errorString());
        return {};
    }
    return document.object();
}

quint32 T384CalibrationTransaction::jsonUInt(const QJsonObject &object,
                                             const QString &key)
{
    return object.value(key).toVariant().toUInt();
}

} // namespace serein::scope::t384
