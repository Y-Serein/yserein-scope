#pragma once

#include <cmath>
#include <cstdlib>

namespace serein::scope::tn160common {

enum class StateBGateDecision {
    Ready,
    Waiting,
    PhysicalReady,
    AdvisoryOverride,
    MissingReference,
    MissingTelemetry,
    ContinuityLost,
    CoverageInsufficient,
};

struct StateBGateInput {
    bool referenceValid = false;
    bool telemetryValid = false;
    bool continuityValid = false;

    bool useCelsiusOrElapsed = false;
    bool useObservedCoverageC = false;
    bool readyLatched = false;
    bool elapsedReady = false;
    double referenceNtcC = 0.0;
    double currentNtcC = 0.0;
    double minimumDeltaNtcC = 3.0;
    double observedCoverageC = 0.0;
    double minimumCoverageC = 10.0;

    bool coverageLatched = false;
    bool advisoryElapsed = false;
    int referenceNtcAdu = 0;
    int currentNtcAdu = 0;
    int referenceNucCount = 0;
    int currentNucCount = 0;
    int minimumDeltaNtcAdu = 0;
    int minimumFfcDelta = 0;
};

struct StateBGateResult {
    StateBGateDecision decision = StateBGateDecision::MissingReference;
    double deltaNtcC = 0.0;
    int deltaNtcAdu = 0;
    int deltaFfc = -1;

    [[nodiscard]] bool canCapture() const noexcept
    {
        return decision == StateBGateDecision::Ready
            || decision == StateBGateDecision::PhysicalReady
            || decision == StateBGateDecision::AdvisoryOverride;
    }

    [[nodiscard]] bool physicalReady() const noexcept
    {
        return decision == StateBGateDecision::Ready
            || decision == StateBGateDecision::PhysicalReady;
    }
};

[[nodiscard]] inline StateBGateResult evaluateStateBGate(
    const StateBGateInput &input) noexcept
{
    StateBGateResult result;
    if (!input.referenceValid) {
        result.decision = StateBGateDecision::MissingReference;
        return result;
    }
    if (!input.telemetryValid) {
        result.decision = StateBGateDecision::MissingTelemetry;
        return result;
    }

    result.deltaNtcC = std::abs(input.currentNtcC - input.referenceNtcC);
    result.deltaNtcAdu = std::abs(input.currentNtcAdu
                                  - input.referenceNtcAdu);
    if (input.currentNucCount >= input.referenceNucCount) {
        result.deltaFfc = input.currentNucCount - input.referenceNucCount;
    }
    if (!input.continuityValid) {
        result.decision = StateBGateDecision::ContinuityLost;
        return result;
    }

    if (input.useObservedCoverageC) {
        result.deltaNtcC = input.observedCoverageC;
        result.decision = input.coverageLatched
                || (input.observedCoverageC >= input.minimumCoverageC
                    && result.deltaFfc >= input.minimumFfcDelta)
            ? StateBGateDecision::PhysicalReady
            : StateBGateDecision::CoverageInsufficient;
        return result;
    }

    if (input.useCelsiusOrElapsed) {
        result.decision = input.readyLatched || input.elapsedReady
                || result.deltaNtcC >= input.minimumDeltaNtcC
            ? StateBGateDecision::Ready
            : StateBGateDecision::Waiting;
        return result;
    }

    if (input.coverageLatched
        || (result.deltaNtcAdu >= input.minimumDeltaNtcAdu
            && result.deltaFfc >= input.minimumFfcDelta)) {
        result.decision = StateBGateDecision::PhysicalReady;
        return result;
    }
    result.decision = input.advisoryElapsed
        ? StateBGateDecision::AdvisoryOverride
        : StateBGateDecision::CoverageInsufficient;
    return result;
}

} // namespace serein::scope::tn160common
