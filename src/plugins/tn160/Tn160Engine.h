#pragma once

#include <serein/scope/IDataEngine.h>

namespace serein::scope::tn160 {

inline constexpr int FrameWidth = 160;
inline constexpr int FrameHeight = 120;
inline constexpr int FrameSize = 19255;
inline constexpr auto Tn160MonitorSource = "tn160.monitor.v1";

enum MonitorSampleIndex : qsizetype {
    MonitorCenterRoiC = 0,
    MonitorNtcC,
    MonitorCenterRoiAdu,
    MonitorVtemp,
    MonitorCenterRawC,
    MonitorCenterRawAdu,
    MonitorNtcAdu,
    MonitorNtcRefAdu,
    MonitorTLowC,
    MonitorTHighC,
    MonitorSmoothLowAdu,
    MonitorSmoothHighAdu,
    MonitorMeanDiff,
    MonitorSmoothVtempAdu,
    MonitorNucDecision,
    MonitorNucCount,
    MonitorNucDg,
    MonitorNucDv,
    MonitorNucDn,
    MonitorBootNtcAdu,
    MonitorBootDeltaAdu,
    MonitorWarmupComp,
    MonitorNormalComp,
    MonitorTotalComp,
    MonitorAnchor,
    MonitorEffectiveAnchor,
    MonitorSampleCount,
};

struct MonitorTelemetry {
    quint16 vtemp = 0;
    qint16 tLowX10 = 0x7fff;
    qint16 tHighX10 = 0x7fff;
    qint32 anchor = 0;
    quint16 smoothLow = 0;
    quint16 smoothHigh = 0;
    float meanDiff = 0.0f;
    quint16 ntcRef = 0;
    quint16 ntc = 0;
    quint8 nucDecision = 0xff;
    quint8 nucCount = 0;
    qint16 nucDg = 0;
    qint16 nucDv = 0;
    qint16 nucDn = 0;
    quint16 bootNtc = 0;
    quint16 bootDelta = 0xffff;
    qint16 warmupComp = 0;
    qint16 normalComp = 0;
    qint16 totalComp = 0;
    qint32 effectiveAnchor = 0;
    quint16 smoothVtemp = 0;
    qint16 centerRawX10 = 0x7fff;
    qint16 centerRoiX10 = 0x7fff;
    quint16 centerRawAdu = 0;
    quint16 centerRoiAdu = 0;
};

class Tn160Engine final : public IDataEngine {
public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    void reset() override;
    [[nodiscard]] ParseResult feed(QByteArrayView buffer) override;

    [[nodiscard]] static bool plausible(QByteArrayView frame);
    [[nodiscard]] static bool decodeTelemetry(QByteArrayView frame,
                                              MonitorTelemetry &telemetry);
    [[nodiscard]] static double ntcCelsius(quint16 adc);
};

} // namespace serein::scope::tn160
