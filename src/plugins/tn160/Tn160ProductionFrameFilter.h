#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace serein::scope::tn160 {

class Tn160ProductionFrameFilter final {
public:
    enum class Decision {
        Accept,
        StartupDiscard,
        MeanSpanOutlier,
        SpatialOutlier,
        InvalidFrame,
    };

    struct Result {
        Decision decision = Decision::InvalidFrame;
        double meanSpanAdu = 0.0;
        double spatialRmseAdu = std::numeric_limits<double>::quiet_NaN();
        double spatialLimitAdu = std::numeric_limits<double>::quiet_NaN();
    };

    static constexpr int StartupDiscardFrames = 1;
    static constexpr double MeanSpanLimitAdu = 1000.0;
    static constexpr std::size_t SpatialHistoryFrames = 16;
    // These dimensionless scales follow the capture's own accepted temporal
    // noise. The only absolute floor is one raw-ADU quantization step.
    static constexpr double SpatialMedianScale = 6.0;
    static constexpr double SpatialMadScale = 8.0;
    static constexpr double SpatialQuantizationFloorAdu = 1.0;

    explicit Tn160ProductionFrameFilter(std::size_t pixelCount = 160u * 120u);

    void reset();
    [[nodiscard]] Result evaluate(std::span<const std::uint16_t> pixels,
                                  double meanAdu);

private:
    void accept(std::span<const std::uint16_t> pixels, double meanAdu,
                double spatialRmseAdu);
    [[nodiscard]] double spatialLimit() const;

    std::size_t m_pixelCount = 0;
    int m_receivedFrames = 0;
    bool m_hasAcceptedFrame = false;
    double m_acceptedMeanMin = 0.0;
    double m_acceptedMeanMax = 0.0;
    double m_previousMean = 0.0;
    std::vector<std::uint16_t> m_previousPixels;
    std::vector<double> m_spatialHistory;
};

} // namespace serein::scope::tn160
