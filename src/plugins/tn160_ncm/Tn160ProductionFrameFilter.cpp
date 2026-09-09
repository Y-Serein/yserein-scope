#include "Tn160ProductionFrameFilter.h"

#include <algorithm>
#include <cmath>

namespace serein::scope::tn160ncm {
namespace {

double median(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    const auto middle = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    const double upper = *middle;
    if (values.size() % 2 != 0) return upper;
    const auto lower = std::max_element(values.begin(), middle);
    return (*lower + upper) * 0.5;
}

} // namespace

Tn160ProductionFrameFilter::Tn160ProductionFrameFilter(
    const std::size_t pixelCount)
    : m_pixelCount(pixelCount)
{
}

void Tn160ProductionFrameFilter::reset()
{
    m_receivedFrames = 0;
    m_hasAcceptedFrame = false;
    m_acceptedMeanMin = 0.0;
    m_acceptedMeanMax = 0.0;
    m_previousMean = 0.0;
    m_previousPixels.clear();
    m_spatialHistory.clear();
}

Tn160ProductionFrameFilter::Result
Tn160ProductionFrameFilter::evaluate(
    const std::span<const std::uint16_t> pixels, const double meanAdu)
{
    Result result;
    if (pixels.size() != m_pixelCount || !std::isfinite(meanAdu)) {
        return result;
    }

    ++m_receivedFrames;
    if (m_receivedFrames <= StartupDiscardFrames) {
        result.decision = Decision::StartupDiscard;
        return result;
    }

    const double candidateMin = m_hasAcceptedFrame
        ? std::min(m_acceptedMeanMin, meanAdu) : meanAdu;
    const double candidateMax = m_hasAcceptedFrame
        ? std::max(m_acceptedMeanMax, meanAdu) : meanAdu;
    result.meanSpanAdu = candidateMax - candidateMin;
    if (result.meanSpanAdu > MeanSpanLimitAdu) {
        result.decision = Decision::MeanSpanOutlier;
        return result;
    }

    if (!m_hasAcceptedFrame) {
        accept(pixels, meanAdu, std::numeric_limits<double>::quiet_NaN());
        result.decision = Decision::Accept;
        return result;
    }

    long double squareSum = 0.0L;
    for (std::size_t pixel = 0; pixel < pixels.size(); ++pixel) {
        const double centeredDifference =
            (double(pixels[pixel]) - meanAdu)
            - (double(m_previousPixels[pixel]) - m_previousMean);
        squareSum += centeredDifference * centeredDifference;
    }
    result.spatialRmseAdu = std::sqrt(
        double(squareSum / static_cast<long double>(pixels.size())));
    if (!m_spatialHistory.empty()) {
        result.spatialLimitAdu = spatialLimit();
        if (result.spatialRmseAdu > result.spatialLimitAdu) {
            result.decision = Decision::SpatialOutlier;
            return result;
        }
    }

    accept(pixels, meanAdu, result.spatialRmseAdu);
    result.decision = Decision::Accept;
    return result;
}

void Tn160ProductionFrameFilter::accept(
    const std::span<const std::uint16_t> pixels, const double meanAdu,
    const double spatialRmseAdu)
{
    if (!m_hasAcceptedFrame) {
        m_acceptedMeanMin = meanAdu;
        m_acceptedMeanMax = meanAdu;
        m_hasAcceptedFrame = true;
    } else {
        m_acceptedMeanMin = std::min(m_acceptedMeanMin, meanAdu);
        m_acceptedMeanMax = std::max(m_acceptedMeanMax, meanAdu);
    }
    m_previousMean = meanAdu;
    m_previousPixels.assign(pixels.begin(), pixels.end());
    if (!std::isfinite(spatialRmseAdu)) return;
    if (m_spatialHistory.size() >= SpatialHistoryFrames) {
        m_spatialHistory.erase(m_spatialHistory.begin());
    }
    m_spatialHistory.push_back(spatialRmseAdu);
}

double Tn160ProductionFrameFilter::spatialLimit() const
{
    const double center = median(m_spatialHistory);
    std::vector<double> deviations;
    deviations.reserve(m_spatialHistory.size());
    for (const double value : m_spatialHistory) {
        deviations.push_back(std::abs(value - center));
    }
    const double mad = median(std::move(deviations));
    const double relativeLimit =
        std::max(center, SpatialQuantizationFloorAdu) * SpatialMedianScale;
    const double robustLimit = center + SpatialMadScale * 1.4826 * mad;
    return std::max(relativeLimit, robustLimit);
}

} // namespace serein::scope::tn160ncm
