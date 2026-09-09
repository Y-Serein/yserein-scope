#pragma once

#include <QtCore/QByteArrayView>
#include <QtCore/QString>
#include <QtCore/QtEndian>

#include "Tn160CalibrationValidation.h"

#include <cstdlib>

namespace serein::scope::tn160ncm::detail {

[[nodiscard]] inline QString normalizeEndpoint(QString endpoint)
{
    endpoint = endpoint.trimmed();
    if (endpoint.isEmpty()) endpoint = QStringLiteral("192.168.17.1");
    if (!endpoint.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !endpoint.startsWith(QStringLiteral("https://"),
                                Qt::CaseInsensitive)) {
        endpoint.prepend(QStringLiteral("http://"));
    }
    while (endpoint.endsWith(QLatin1Char('/'))) endpoint.chop(1);
    return endpoint;
}

[[nodiscard]] inline quint32 crc32(const QByteArrayView bytes) noexcept
{
    quint32 crc = 0xffffffffu;
    for (const char byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (crc & 1u ? 0xedb88320u : 0u);
        }
    }
    return ~crc;
}

enum class NtcModelHeaderStatus {
    Disabled,
    Valid,
    ColdStart,
    RowReference,
    Invalid,
};

struct NtcModelHeader {
    NtcModelHeaderStatus status = NtcModelHeaderStatus::Invalid;
    quint16 referenceAdu = 0;
    qint16 kQ8 = 0;
    quint16 limitAdu = 0;
    quint16 coldNtcThresholdAdu = 0;
    qint16 startupAmplitudeAdu = 0;
    quint16 startupTauDs = 0;
};

[[nodiscard]] inline NtcModelHeader parseNtcModelHeader(
    const QByteArrayView header) noexcept
{
    NtcModelHeader result;
    if (header.size() < 60) return result;
    const auto *bytes = reinterpret_cast<const uchar *>(header.data());
    const quint16 tag = qFromLittleEndian<quint16>(bytes + 52);
    result.referenceAdu = qFromLittleEndian<quint16>(bytes + 54);
    result.kQ8 = qFromLittleEndian<qint16>(bytes + 56);
    result.limitAdu = qFromLittleEndian<quint16>(bytes + 58);
    if (tag == 0 && result.referenceAdu == 0 && result.kQ8 == 0
        && result.limitAdu == 0) {
        result.status = NtcModelHeaderStatus::Disabled;
        return result;
    }
    if (tag == 0x4e54u && result.referenceAdu > 8u
        && result.referenceAdu < 4088u
        && std::abs(int(result.kQ8)) <= NtcModelKQ8Limit
        && result.limitAdu > 0u
        && result.limitAdu <= NtcModelAnchorLimitAdu) {
        result.status = NtcModelHeaderStatus::Valid;
    } else if (tag == 0x4354u && result.referenceAdu > 8u
               && result.referenceAdu < 4088u
               && std::abs(int(result.kQ8)) <= ColdStartAmplitudeMaxAdu
               && result.limitAdu >= ColdStartTauDsMin
               && result.limitAdu <= ColdStartTauDsMax) {
        result.status = NtcModelHeaderStatus::ColdStart;
        result.coldNtcThresholdAdu = result.referenceAdu;
        result.startupAmplitudeAdu = result.kQ8;
        result.startupTauDs = result.limitAdu;
    } else if (tag == 0x5252u
               && result.referenceAdu >= RowRefReferenceMinAdu
               && result.referenceAdu <= RowRefReferenceMaxAdu
               && result.kQ8 > 0
               && result.kQ8 <= RowRefModelKQ8 * 2
               && result.limitAdu > 0u
               && result.limitAdu <= NtcModelAnchorLimitAdu) {
        result.status = NtcModelHeaderStatus::RowReference;
    }
    return result;
}

} // namespace serein::scope::tn160ncm::detail
