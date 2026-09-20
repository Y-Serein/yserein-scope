#pragma once

#include <serein/scope/IDataEngine.h>

#include <QtCore/QByteArray>

namespace serein::scope::t384 {

inline constexpr auto T384Raw16Source = "t384.raw16.v1";
inline constexpr auto T384MonitorSource = "t384.monitor.v1";

class T384Engine final : public IDataEngine {
public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    void reset() override;
    [[nodiscard]] ParseResult feed(QByteArrayView buffer) override;

    // This is intentionally an experimental host-side two-point model for
    // the supported T256/T384/T640 geometries.
    // zeroRaw is the mean Y16 value measured at 0 °C; nothing is written to a device.
    void setExperimentalModel(bool enabled, double zeroRaw, double countsPerC);

private:
    struct PendingFrame {
        quint32 sequence = 0;
        quint32 captureMs = 0;
        quint32 frameSize = 0;
        quint16 width = 0;
        quint16 height = 0;
        quint16 flags = 0;
        quint16 pixelFormat = 0;
        quint32 nextOffset = 0;
        QByteArray y16be;
    };

    void discardPendingFrame();
    [[nodiscard]] bool hasPendingFrame() const;

    bool m_experimentalModelEnabled = false;
    double m_zeroRaw = 0.0;
    double m_countsPerC = 0.0;
    PendingFrame m_pending;
};

} // namespace serein::scope::t384
