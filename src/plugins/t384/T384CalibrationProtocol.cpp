#include "T384CalibrationProtocol.h"

#include <QCryptographicHash>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace serein::scope::t384 {
namespace {

void setError(ProtocolError *error, const QString &message)
{
    if (error) error->message = message;
}

void put16(QByteArray &out, const qsizetype offset, const quint16 value)
{
    out[offset] = char(value & 0xffu);
    out[offset + 1] = char((value >> 8u) & 0xffu);
}

void put32(QByteArray &out, const qsizetype offset, const quint32 value)
{
    for (qsizetype i = 0; i < 4; ++i)
        out[offset + i] = char((value >> (i * 8)) & 0xffu);
}

quint16 get16(const QByteArray &in, const qsizetype offset)
{
    return quint16(quint8(in[offset])) | (quint16(quint8(in[offset + 1])) << 8u);
}

quint32 get32(const QByteArray &in, const qsizetype offset)
{
    quint32 value = 0;
    for (qsizetype i = 0; i < 4; ++i)
        value |= quint32(quint8(in[offset + i])) << (i * 8);
    return value;
}

bool isNulTerminated(const QByteArray &in, const qsizetype offset, const qsizetype length)
{
    return in.indexOf('\0', offset) >= offset && in.indexOf('\0', offset) < offset + length;
}

QByteArray field(const QByteArray &in, const qsizetype offset, const qsizetype length)
{
    const QByteArray value = in.mid(offset, length);
    const auto nul = value.indexOf('\0');
    return value.left(nul);
}

bool validPayload(const Empirical2Point &payload, ProtocolError *error)
{
    if (payload.highC <= payload.lowC) {
        setError(error, QStringLiteral("high temperature must exceed low temperature"));
        return false;
    }
    if (payload.highRaw <= payload.lowRaw) {
        setError(error, QStringLiteral("high raw value must exceed low raw value"));
        return false;
    }
    if (payload.countsPerCX1000 <= 0) {
        setError(error, QStringLiteral("countsPerCX1000 must be positive"));
        return false;
    }
    const qint64 expected = (qint64(payload.highRaw) - payload.lowRaw) * 1000 /
                             (payload.highC - payload.lowC);
    // Permit integer rounding only; a mismatch identifies a corrupted or
    // independently edited payload before it reaches device flash.
    if (std::llabs(qint64(payload.countsPerCX1000) - expected) > 1) {
        setError(error, QStringLiteral("countsPerCX1000 does not match two-point values"));
        return false;
    }
    const qint64 countsX100 = (qint64(payload.countsPerCX1000) + 5) / 10;
    const qint64 zeroX100 = qint64(payload.lowRaw) * 100
        - qint64(payload.lowC) * countsX100;
    if (zeroX100 < 0 || zeroX100 > 6553500) {
        setError(error, QStringLiteral("model zero point is outside firmware range"));
        return false;
    }
    return true;
}

} // namespace

QString profileName(const ThermalProfile profile)
{
    switch (profile) {
    case ThermalProfile::T256: return QStringLiteral("256x192");
    case ThermalProfile::T384: return QStringLiteral("384x288");
    case ThermalProfile::T640: return QStringLiteral("640x512");
    }
    return {};
}

std::optional<ThermalProfile> profileFromName(const QByteArray &name)
{
    if (name == "256x192") return ThermalProfile::T256;
    if (name == "384x288") return ThermalProfile::T384;
    if (name == "640x512") return ThermalProfile::T640;
    return std::nullopt;
}

QByteArray calibrationIdentity(const QByteArray &pn, const QByteArray &sn, const QByteArray &fw)
{
    return QCryptographicHash::hash(pn + ':' + sn + ':' + fw,
                                    QCryptographicHash::Sha256);
}

quint32 crc32(const QByteArray &bytes)
{
    quint32 crc = 0xffffffffu;
    for (const auto byte : bytes) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & quint32(-qint32(crc & 1u)));
    }
    return ~crc;
}

bool passesTwoPointQualityGate(const double lowMean, const double highMean,
                               const double lowStdDev,
                               const double highStdDev) noexcept
{
    if (!std::isfinite(lowMean) || !std::isfinite(highMean)
        || !std::isfinite(lowStdDev) || !std::isfinite(highStdDev)
        || lowStdDev < 0.0 || highStdDev < 0.0) return false;
    const double delta = highMean - lowMean;
    return delta > 0.0 && delta > 6.0 * std::max(lowStdDev, highStdDev);
}

std::optional<QByteArray> encodeCalibrationPacket(const CalibrationPacket &packet,
                                                   ProtocolError *error)
{
    if (error) error->message.clear();
    if (packet.manifest.calibrationId == 0u) {
        setError(error, QStringLiteral("calibrationId must be non-zero"));
        return std::nullopt;
    }
    if (packet.manifest.model != QByteArrayLiteral("t384-empirical-2point-v1")) {
        setError(error, QStringLiteral("unsupported calibration model"));
        return std::nullopt;
    }
    if (packet.manifest.identity.size() != 32
        || packet.manifest.identity == QByteArray(32, '\0')
        || packet.manifest.identity == QByteArray(32, char(0xff))) {
        setError(error, QStringLiteral("identity must be a valid 32-byte SHA-256"));
        return std::nullopt;
    }
    if (packet.manifest.gain != 1u && packet.manifest.gain != 2u && packet.manifest.gain != 0xffu) {
        setError(error, QStringLiteral("gain must be 1, 2, or 0xFF"));
        return std::nullopt;
    }
    if (!validPayload(packet.payload, error)) return std::nullopt;

    QByteArray wire(kCalibrationPacketBytes, '\0');
    put32(wire, 0, kCalibrationMagic);
    put32(wire, 4, kCalibrationSchema);
    put32(wire, 8, packet.manifest.generation);
    put32(wire, 12, kEmpirical2PointBytes);
    put32(wire, 24, packet.manifest.calibrationId);
    std::memcpy(wire.data() + 28, packet.manifest.model.constData(), packet.manifest.model.size());
    const auto profile = profileName(packet.manifest.profile).toLatin1();
    std::memcpy(wire.data() + 60, profile.constData(), profile.size());
    std::memcpy(wire.data() + 76, packet.manifest.identity.constData(), 32);
    wire[108] = char(packet.manifest.gain);

    put16(wire, 112, packet.payload.lowC);
    put16(wire, 114, packet.payload.highC);
    put32(wire, 116, packet.payload.lowRaw);
    put32(wire, 120, packet.payload.highRaw);
    put32(wire, 124, quint32(packet.payload.countsPerCX1000));
    put32(wire, 16, crc32(wire.mid(112, kEmpirical2PointBytes)));
    put32(wire, kHeaderCrc32Offset, crc32(wire.left(kCalibrationManifestBytes)));
    return wire;
}

std::optional<CalibrationPacket> decodeCalibrationPacket(const QByteArray &wire,
                                                          const QByteArray &expectedIdentity,
                                                          ProtocolError *error)
{
    if (error) error->message.clear();
    if (wire.size() != kCalibrationPacketBytes) {
        setError(error, QStringLiteral("packet must be exactly 128 bytes"));
        return std::nullopt;
    }
    if (!expectedIdentity.isEmpty() && expectedIdentity.size() != 32) {
        setError(error, QStringLiteral("expected identity must be empty or 32 bytes"));
        return std::nullopt;
    }
    if (get32(wire, 0) != kCalibrationMagic || get32(wire, 4) != kCalibrationSchema) {
        setError(error, QStringLiteral("unsupported packet magic or schema"));
        return std::nullopt;
    }
    if (get32(wire, 12) != kEmpirical2PointBytes) {
        setError(error, QStringLiteral("unexpected payload length"));
        return std::nullopt;
    }
    QByteArray header = wire.left(kCalibrationManifestBytes);
    const auto suppliedHeaderCrc = get32(header, kHeaderCrc32Offset);
    put32(header, kHeaderCrc32Offset, 0);
    if (crc32(header) != suppliedHeaderCrc ||
        crc32(wire.mid(112, kEmpirical2PointBytes)) != get32(wire, 16)) {
        setError(error, QStringLiteral("header or payload CRC32 mismatch"));
        return std::nullopt;
    }
    if (!isNulTerminated(wire, 28, 32) || !isNulTerminated(wire, 60, 16)) {
        setError(error, QStringLiteral("model or profile string is not NUL terminated"));
        return std::nullopt;
    }
    CalibrationPacket packet;
    packet.manifest.generation = get32(wire, 8);
    packet.manifest.calibrationId = get32(wire, 24);
    packet.manifest.model = field(wire, 28, 32);
    const auto profile = profileFromName(field(wire, 60, 16));
    packet.manifest.identity = wire.mid(76, 32);
    packet.manifest.gain = quint8(wire[108]);
    packet.payload.lowC = get16(wire, 112);
    packet.payload.highC = get16(wire, 114);
    packet.payload.lowRaw = get32(wire, 116);
    packet.payload.highRaw = get32(wire, 120);
    packet.payload.countsPerCX1000 = qint32(get32(wire, 124));
    if (!profile || packet.manifest.model != QByteArrayLiteral("t384-empirical-2point-v1") ||
        packet.manifest.calibrationId == 0 ||
        (packet.manifest.gain != 1u && packet.manifest.gain != 2u && packet.manifest.gain != 0xffu) ||
        packet.manifest.identity == QByteArray(32, '\0') ||
        packet.manifest.identity == QByteArray(32, char(0xff)) ||
        (!expectedIdentity.isEmpty() && packet.manifest.identity != expectedIdentity)) {
        setError(error, QStringLiteral("invalid packet identity, profile, model, or gain"));
        return std::nullopt;
    }
    packet.manifest.profile = *profile;
    if (!validPayload(packet.payload, error)) return std::nullopt;
    return packet;
}

} // namespace serein::scope::t384
