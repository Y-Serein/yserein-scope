#include "Tn160Engine.h"

#include <QtCore/QDateTime>
#include <QtCore/QtEndian>

#include <cmath>
#include <cstring>
#include <limits>

namespace serein::scope::tn160 {
namespace {

template<typename T>
T readBe(const char *data)
{
    return qFromBigEndian<T>(reinterpret_cast<const uchar *>(data));
}

} // namespace

QString Tn160Engine::id() const
{
    return QStringLiteral("tn160.monitor.v1");
}

QString Tn160Engine::displayName() const
{
    return QStringLiteral("TN160 monitor frame");
}

void Tn160Engine::reset()
{
}

bool Tn160Engine::plausible(const QByteArrayView frame)
{
    if (frame.size() != FrameSize || quint8(frame.at(0)) != 0xff) {
        return false;
    }
    const char *raw = frame.data();
    const int base = 1 + FrameWidth * FrameHeight;
    const quint16 rawVtemp = readBe<quint16>(raw + base);
    if ((rawVtemp & 0xc000u) != 0) {
        return false;
    }
    const qint16 low = readBe<qint16>(raw + base + 2);
    const qint16 high = readBe<qint16>(raw + base + 4);
    if (!(low == 0x7fff && high == 0x7fff)) {
        if (low < -1000 || low > 3200 || high < -1000 || high > 3200 || high < low) {
            return false;
        }
    }
    const quint16 smoothLow = readBe<quint16>(raw + base + 10);
    const quint16 smoothHigh = readBe<quint16>(raw + base + 12);
    const quint16 ntcRef = readBe<quint16>(raw + base + 18);
    const quint16 ntc = readBe<quint16>(raw + base + 20);
    const quint8 nucDecision = quint8(raw[base + 22]);
    const quint16 smoothVtemp = readBe<quint16>(raw + base + 44);
    const qint16 centerRaw = readBe<qint16>(raw + base + 46);
    const qint16 centerRoi = readBe<qint16>(raw + base + 48);
    const quint16 centerRawAdu = readBe<quint16>(raw + base + 50);
    const quint16 centerRoiAdu = readBe<quint16>(raw + base + 52);
    if (smoothLow > 0x3fff || smoothHigh > 0x3fff || smoothHigh < smoothLow
        || smoothVtemp > 0x3fff || centerRawAdu > 0x3fff || centerRoiAdu > 0x3fff) {
        return false;
    }
    if ((ntcRef != 0 && ntcRef != 0xffff && ntcRef > 4095)
        || (ntc != 0 && ntc != 0xffff && ntc > 4095)) {
        return false;
    }
    if (!(nucDecision <= 4 || nucDecision == 0xff)) {
        return false;
    }
    for (const qint16 center : {centerRaw, centerRoi}) {
        if (center != 0x7fff && (center < -1000 || center > 3200)) {
            return false;
        }
    }
    return true;
}

bool Tn160Engine::decodeTelemetry(const QByteArrayView frame,
                                  MonitorTelemetry &telemetry)
{
    if (!plausible(frame)) return false;
    const char *raw = frame.data();
    const int base = 1 + FrameWidth * FrameHeight;
    telemetry.vtemp = readBe<quint16>(raw + base) & 0x3fffu;
    telemetry.tLowX10 = readBe<qint16>(raw + base + 2);
    telemetry.tHighX10 = readBe<qint16>(raw + base + 4);
    telemetry.anchor = readBe<qint32>(raw + base + 6);
    telemetry.smoothLow = readBe<quint16>(raw + base + 10);
    telemetry.smoothHigh = readBe<quint16>(raw + base + 12);
    const quint32 meanDiffBits = readBe<quint32>(raw + base + 14);
    std::memcpy(&telemetry.meanDiff, &meanDiffBits, sizeof(telemetry.meanDiff));
    telemetry.ntcRef = readBe<quint16>(raw + base + 18);
    telemetry.ntc = readBe<quint16>(raw + base + 20);
    telemetry.nucDecision = quint8(raw[base + 22]);
    telemetry.nucCount = quint8(raw[base + 23]);
    telemetry.nucDg = readBe<qint16>(raw + base + 24);
    telemetry.nucDv = readBe<qint16>(raw + base + 26);
    telemetry.nucDn = readBe<qint16>(raw + base + 28);
    telemetry.bootNtc = readBe<quint16>(raw + base + 30);
    telemetry.bootDelta = readBe<quint16>(raw + base + 32);
    telemetry.warmupComp = readBe<qint16>(raw + base + 34);
    telemetry.normalComp = readBe<qint16>(raw + base + 36);
    telemetry.totalComp = readBe<qint16>(raw + base + 38);
    telemetry.effectiveAnchor = readBe<qint32>(raw + base + 40);
    telemetry.smoothVtemp = readBe<quint16>(raw + base + 44);
    telemetry.centerRawX10 = readBe<qint16>(raw + base + 46);
    telemetry.centerRoiX10 = readBe<qint16>(raw + base + 48);
    telemetry.centerRawAdu = readBe<quint16>(raw + base + 50);
    telemetry.centerRoiAdu = readBe<quint16>(raw + base + 52);
    return true;
}

double Tn160Engine::ntcCelsius(const quint16 adc)
{
    if (adc == 0 || adc >= 4095) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    constexpr double pullup = 10000.0;
    constexpr double r0 = 10000.0;
    constexpr double beta = 3435.0;
    constexpr double t0 = 298.15;
    const double resistance = pullup * adc / (4095.0 - adc);
    const double inverse = 1.0 / t0 + std::log(resistance / r0) / beta;
    return 1.0 / inverse - 273.15;
}

ParseResult Tn160Engine::feed(const QByteArrayView buffer)
{
    qsizetype start = buffer.indexOf(char(0xff));
    if (start < 0) {
        return {.consumed = buffer.size(), .packets = {}, .needMore = false};
    }
    if (buffer.size() - start < FrameSize) {
        return {.consumed = start, .packets = {}, .needMore = true};
    }
    while (start >= 0 && buffer.size() - start >= FrameSize) {
        const QByteArrayView candidate(buffer.data() + start, FrameSize);
        MonitorTelemetry telemetry;
        if (decodeTelemetry(candidate, telemetry)) {
            const char *raw = candidate.data();
            const qint64 now = QDateTime::currentMSecsSinceEpoch() * 1000;
            const double center = telemetry.centerRoiX10 == 0x7fff
                ? std::numeric_limits<double>::quiet_NaN()
                : telemetry.centerRoiX10 / 10.0;
            const double centerRaw = telemetry.centerRawX10 == 0x7fff
                ? std::numeric_limits<double>::quiet_NaN()
                : telemetry.centerRawX10 / 10.0;
            PacketList packets;
            packets.append(ImagePacket{
                now,
                FrameWidth,
                FrameHeight,
                QByteArray(raw + 1, FrameWidth * FrameHeight),
                QStringLiteral("gray8"),
                id(),
            });
            packets.append(SamplePacket{
                now,
                {center, ntcCelsius(telemetry.ntc),
                 double(telemetry.centerRoiAdu), double(telemetry.vtemp),
                 centerRaw, double(telemetry.centerRawAdu),
                 double(telemetry.ntc), double(telemetry.ntcRef),
                 double(telemetry.tLowX10) / 10.0,
                 double(telemetry.tHighX10) / 10.0,
                 double(telemetry.smoothLow), double(telemetry.smoothHigh),
                 double(telemetry.meanDiff), double(telemetry.smoothVtemp),
                 double(telemetry.nucDecision), double(telemetry.nucCount),
                 double(telemetry.nucDg), double(telemetry.nucDv),
                 double(telemetry.nucDn), double(telemetry.bootNtc),
                 double(telemetry.bootDelta), double(telemetry.warmupComp),
                 double(telemetry.normalComp), double(telemetry.totalComp),
                 double(telemetry.anchor), double(telemetry.effectiveAnchor)},
                {QStringLiteral("Center 5×5 °C"), QStringLiteral("NTC °C"),
                 QStringLiteral("Center 5×5 ADU"), QStringLiteral("VTEMP ADU"),
                 QStringLiteral("Center raw °C"), QStringLiteral("Center raw ADU"),
                 QStringLiteral("NTC ADU"), QStringLiteral("NTC ref ADU"),
                 QStringLiteral("T low °C"), QStringLiteral("T high °C"),
                 QStringLiteral("Smooth low ADU"), QStringLiteral("Smooth high ADU"),
                 QStringLiteral("Mean diff"), QStringLiteral("Smooth VTEMP ADU"),
                 QStringLiteral("NUC decision"), QStringLiteral("NUC count"),
                 QStringLiteral("NUC dG"), QStringLiteral("NUC dV"),
                 QStringLiteral("NUC dN"), QStringLiteral("Boot NTC ADU"),
                 QStringLiteral("Boot delta ADU"), QStringLiteral("Warmup comp"),
                 QStringLiteral("Normal comp"), QStringLiteral("Total comp"),
                 QStringLiteral("Anchor"), QStringLiteral("Effective anchor")},
                id(),
            });
            return {.consumed = start + FrameSize, .packets = packets, .needMore = false};
        }
        start = buffer.indexOf(char(0xff), start + 1);
    }
    if (start >= 0) {
        return {.consumed = start, .packets = {}, .needMore = true};
    }
    return {.consumed = buffer.size(), .packets = {}, .needMore = false};
}

} // namespace serein::scope::tn160
