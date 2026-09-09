#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>

namespace serein::scope::tn160ncm {

inline constexpr int NtcModelKQ8Limit = 8192;
inline constexpr int NtcModelAnchorLimitAdu = 1500;
inline constexpr std::size_t MinimumColdStartSamples = 20u;
inline constexpr std::uint32_t ColdStartLatestFirstSampleMs = 10000u;
inline constexpr std::uint32_t ColdStartCaptureEndMs = 60000u;
inline constexpr int ColdStartAmplitudeMaxAdu = 600;
inline constexpr std::uint16_t ColdStartTauDsMin = 50u;
inline constexpr std::uint16_t ColdStartTauDsMax = 600u;
inline constexpr std::uint16_t ColdStartNtcGateMarginAdu = 64u;

struct NtcAnchorModelResult {
    bool valid = false;
    std::uint16_t referenceAdu = 0;
    std::int16_t kQ8 = 0;
    std::uint16_t limitAdu = NtcModelAnchorLimitAdu;
};

inline constexpr std::uint16_t RowRefReferenceMinAdu = 512u;
inline constexpr std::uint16_t RowRefReferenceMaxAdu = 15872u;
inline constexpr std::uint16_t RowRefSpanMaxAdu = 2048u;
inline constexpr std::int16_t RowRefModelKQ8 = 256;
inline constexpr std::uint16_t RowRefModelLimitAdu = 600u;

struct RowRefModelResult {
    bool valid = false;
    std::uint16_t referenceAdu = 0;
    std::int16_t kQ8 = RowRefModelKQ8;
    std::uint16_t limitAdu = RowRefModelLimitAdu;
};

inline constexpr double RowRefValidationMinimumC = 49.0;
inline constexpr double RowRefValidationMaximumC = 51.0;
inline constexpr double RowRefValidationPeakToPeakMaximumC = 1.5;

struct RowRefTemperatureValidation {
    bool valid = false;
    bool passed = false;
    double meanC = std::numeric_limits<double>::quiet_NaN();
    double peakToPeakC = std::numeric_limits<double>::quiet_NaN();
};

[[nodiscard]] inline RowRefTemperatureValidation validateRowRefTemperature(
    const std::span<const double> temperatures) noexcept
{
    RowRefTemperatureValidation result;
    if (temperatures.empty()) return result;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (const double temperature : temperatures) {
        if (!std::isfinite(temperature)) return result;
        minimum = std::min(minimum, temperature);
        maximum = std::max(maximum, temperature);
        sum += temperature;
    }
    result.valid = true;
    result.meanC = sum / double(temperatures.size());
    result.peakToPeakC = maximum - minimum;
    result.passed = result.meanC >= RowRefValidationMinimumC
                    && result.meanC <= RowRefValidationMaximumC
                    && result.peakToPeakC
                           <= RowRefValidationPeakToPeakMaximumC;
    return result;
}

[[nodiscard]] constexpr bool validRowRefSample(
    const std::uint16_t meanAdu, const std::uint16_t spanAdu) noexcept
{
    return meanAdu >= RowRefReferenceMinAdu
           && meanAdu <= RowRefReferenceMaxAdu
           && spanAdu > 0u && spanAdu <= RowRefSpanMaxAdu;
}

[[nodiscard]] constexpr RowRefModelResult makeRowRefModel(
    const std::uint16_t beforeMeanAdu, const std::uint16_t beforeSpanAdu,
    const std::uint16_t afterMeanAdu, const std::uint16_t afterSpanAdu) noexcept
{
    RowRefModelResult result;
    if (!validRowRefSample(beforeMeanAdu, beforeSpanAdu)
        || !validRowRefSample(afterMeanAdu, afterSpanAdu)) {
        return result;
    }
    result.referenceAdu = std::uint16_t(
        (std::uint32_t(beforeMeanAdu) + std::uint32_t(afterMeanAdu) + 1u)
        / 2u);
    result.valid = validRowRefSample(result.referenceAdu, 1u);
    return result;
}

static_assert(makeRowRefModel(4940u, 520u, 4956u, 560u).referenceAdu
                  == 4948u,
              "RR production reference must use the high-capture midpoint");
static_assert(!makeRowRefModel(4940u, 2049u, 4956u, 560u).valid,
              "RR production model must reject an invalid row span");

enum class ColdStartModelFailure {
    None,
    InvalidInput,
    LateFirstSample,
    InsufficientSamples,
    IncompleteWindow,
    FitFailed,
    AmplitudeOutOfRange,
};

struct ColdStartSample {
    std::uint32_t elapsedMs = 0;
    double requiredAnchorAdu = std::numeric_limits<double>::quiet_NaN();
};

struct ColdStartModelResult {
    bool valid = false;
    ColdStartModelFailure failure = ColdStartModelFailure::InvalidInput;
    std::uint16_t coldNtcThresholdAdu = 0;
    std::int16_t amplitudeAdu = 0;
    std::uint16_t tauDs = 0;
    std::size_t sampleCount = 0;
    std::uint32_t firstSampleMs = 0;
    std::uint32_t lastSampleMs = 0;
    double fittedOffsetAdu = std::numeric_limits<double>::quiet_NaN();
    double residualRmseAdu = std::numeric_limits<double>::quiet_NaN();
    double residualPeakToPeakAdu = std::numeric_limits<double>::quiet_NaN();
};

[[nodiscard]] inline int coldStartCompensationAdu(
    const std::int16_t amplitudeAdu, const std::uint16_t tauDs,
    const std::uint32_t elapsedMs) noexcept
{
    if (std::abs(int(amplitudeAdu)) > ColdStartAmplitudeMaxAdu
        || tauDs < ColdStartTauDsMin
        || tauDs > ColdStartTauDsMax) {
        return 0;
    }
    const double tauMs = double(tauDs) * 100.0;
    if (elapsedMs >= std::uint32_t(tauMs * 6.0)) return 0;
    return int(std::lround(double(amplitudeAdu)
                           * std::exp(-double(elapsedMs) / tauMs)));
}

[[nodiscard]] inline ColdStartModelResult fitColdStartModel(
    const std::span<const ColdStartSample> samples,
    const std::uint16_t bootNtcAdu) noexcept
{
    ColdStartModelResult result;
    result.sampleCount = samples.size();
    if (bootNtcAdu <= 8u || bootNtcAdu >= 4088u || samples.empty()) {
        return result;
    }
    result.firstSampleMs = samples.front().elapsedMs;
    result.lastSampleMs = samples.back().elapsedMs;
    if (result.firstSampleMs > ColdStartLatestFirstSampleMs) {
        result.failure = ColdStartModelFailure::LateFirstSample;
        return result;
    }
    if (samples.size() < MinimumColdStartSamples) {
        result.failure = ColdStartModelFailure::InsufficientSamples;
        return result;
    }
    if (result.lastSampleMs < ColdStartCaptureEndMs - 2000u) {
        result.failure = ColdStartModelFailure::IncompleteWindow;
        return result;
    }
    for (const ColdStartSample &sample : samples) {
        if (!std::isfinite(sample.requiredAnchorAdu)
            || sample.elapsedMs > ColdStartCaptureEndMs + 2000u) {
            return result;
        }
    }

    double bestSse = std::numeric_limits<double>::infinity();
    double bestAmplitude = 0.0;
    double bestOffset = 0.0;
    std::uint16_t bestTauDs = 0;
    /* The fitted offset is a nuisance intercept: it absorbs the fixed target
     * temperature and the absolute V1 anchor.  Only the transient amplitude
     * and tau are written because firmware adds that relative term to its
     * independently calibrated base anchor. */
    for (std::uint16_t tauDs = ColdStartTauDsMin;
         tauDs <= ColdStartTauDsMax; ++tauDs) {
        const double tauMs = double(tauDs) * 100.0;
        double sumX = 0.0;
        double sumY = 0.0;
        double sumXX = 0.0;
        double sumXY = 0.0;
        for (const ColdStartSample &sample : samples) {
            const double x = std::exp(-double(sample.elapsedMs) / tauMs);
            sumX += x;
            sumY += sample.requiredAnchorAdu;
            sumXX += x * x;
            sumXY += x * sample.requiredAnchorAdu;
        }
        const double count = double(samples.size());
        const double denominator = count * sumXX - sumX * sumX;
        if (!std::isfinite(denominator) || denominator <= 1e-9) continue;
        const double amplitude =
            (count * sumXY - sumX * sumY) / denominator;
        const double offset = (sumY - amplitude * sumX) / count;
        double sse = 0.0;
        for (const ColdStartSample &sample : samples) {
            const double x = std::exp(-double(sample.elapsedMs) / tauMs);
            const double residual = sample.requiredAnchorAdu
                                    - (offset + amplitude * x);
            sse += residual * residual;
        }
        if (std::isfinite(sse) && sse < bestSse) {
            bestSse = sse;
            bestAmplitude = amplitude;
            bestOffset = offset;
            bestTauDs = tauDs;
        }
    }
    if (!std::isfinite(bestSse) || bestTauDs == 0u) {
        result.failure = ColdStartModelFailure::FitFailed;
        return result;
    }
    const long amplitude = std::lround(bestAmplitude);
    if (std::abs(amplitude) > ColdStartAmplitudeMaxAdu) {
        result.failure = ColdStartModelFailure::AmplitudeOutOfRange;
        return result;
    }

    double residualMinimum = std::numeric_limits<double>::infinity();
    double residualMaximum = -std::numeric_limits<double>::infinity();
    double residualSse = 0.0;
    for (const ColdStartSample &sample : samples) {
        const double predicted = bestOffset
            + double(amplitude)
                  * std::exp(-double(sample.elapsedMs)
                             / (double(bestTauDs) * 100.0));
        const double residual = sample.requiredAnchorAdu - predicted;
        residualMinimum = std::min(residualMinimum, residual);
        residualMaximum = std::max(residualMaximum, residual);
        residualSse += residual * residual;
    }
    result.coldNtcThresholdAdu = bootNtcAdu > ColdStartNtcGateMarginAdu + 8u
        ? std::uint16_t(bootNtcAdu - ColdStartNtcGateMarginAdu) : 9u;
    result.amplitudeAdu = std::int16_t(amplitude);
    result.tauDs = bestTauDs;
    result.fittedOffsetAdu = bestOffset;
    result.residualRmseAdu =
        std::sqrt(residualSse / double(samples.size()));
    result.residualPeakToPeakAdu = residualMaximum - residualMinimum;
    result.failure = ColdStartModelFailure::None;
    result.valid = true;
    return result;
}

} // namespace serein::scope::tn160ncm
