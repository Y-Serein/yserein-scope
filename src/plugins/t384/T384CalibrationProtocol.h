#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace serein::scope::t384 {

inline constexpr quint32 kCalibrationMagic = 0x54334331u; // "T3C1", little endian
inline constexpr quint32 kCalibrationSchema = 1u;
inline constexpr qsizetype kCalibrationManifestBytes = 112;
inline constexpr qsizetype kEmpirical2PointBytes = 16;
inline constexpr qsizetype kCalibrationPacketBytes =
    kCalibrationManifestBytes + kEmpirical2PointBytes;
inline constexpr qsizetype kHeaderCrc32Offset = 20;

enum class ThermalProfile { T256, T384, T640 };

[[nodiscard]] QString profileName(ThermalProfile profile);
[[nodiscard]] std::optional<ThermalProfile> profileFromName(const QByteArray &name);
[[nodiscard]] QByteArray calibrationIdentity(const QByteArray &pn, const QByteArray &sn,
                                             const QByteArray &fw);
[[nodiscard]] quint32 crc32(const QByteArray &bytes);
[[nodiscard]] bool passesTwoPointQualityGate(double lowMean, double highMean,
                                             double lowStdDev,
                                             double highStdDev) noexcept;

struct Empirical2Point {
    quint16 lowC = 0;
    quint16 highC = 50;
    quint32 lowRaw = 0;
    quint32 highRaw = 0;
    qint32 countsPerCX1000 = 0;
};

struct CalibrationManifest {
    quint32 generation = 0; // Firmware owns the committed generation.
    quint32 calibrationId = 0;
    QByteArray model = QByteArrayLiteral("t384-empirical-2point-v1");
    ThermalProfile profile = ThermalProfile::T384;
    QByteArray identity; // Exactly SHA-256(PN + ':' + SN + ':' + FW).
    quint8 gain = 0xFF;  // 1 / 2 when known, 0xFF when unverified.
};

struct CalibrationPacket {
    CalibrationManifest manifest;
    Empirical2Point payload;
};

struct ProtocolError {
    QString message;
    [[nodiscard]] explicit operator bool() const { return !message.isEmpty(); }
};

/** Serializes the firmware's packed 112-byte manifest followed by its 16-byte
 * empirical two-point payload.  The returned packet is always 128 bytes. */
[[nodiscard]] std::optional<QByteArray> encodeCalibrationPacket(
    const CalibrationPacket &packet, ProtocolError *error = nullptr);

/** Strictly decodes exactly one firmware calibration packet.  CRCs, all fixed
 * fields, strings, supported profiles, gain, and physical two-point ordering
 * are checked. expectedIdentity, when supplied, must be the 32-byte SHA-256. */
[[nodiscard]] std::optional<CalibrationPacket> decodeCalibrationPacket(
    const QByteArray &wire, const QByteArray &expectedIdentity = {},
    ProtocolError *error = nullptr);

} // namespace serein::scope::t384
