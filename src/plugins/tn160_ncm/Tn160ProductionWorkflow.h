#pragma once

namespace serein::scope::tn160ncm {

struct ProductionWorkflowFacts {
    bool idle = false;
    bool finalizing = false;
    bool transportActive = false;
    bool transportConnecting = false;
    bool monitorReady = false;
    bool calibrationStation = false;
    bool lowComplete = false;
    bool highComplete = false;
    bool calibrationValid = false;
    bool productionPass = false;
    bool testWriteAvailable = false;
    bool writeRecoveryAvailable = false;
    bool hasRunData = false;
};

struct ProductionWorkflowActions {
    bool connection = false;
    bool scan = false;
    bool editEndpoint = false;
    bool captureLow = false;
    bool captureHigh = false;
    bool restartRun = false;
    bool writeTest = false;
    bool writeProduction = false;
    bool retryVerification = false;
};

[[nodiscard]] inline ProductionWorkflowActions evaluateProductionWorkflow(
    const ProductionWorkflowFacts &facts) noexcept
{
    ProductionWorkflowActions actions;
    actions.connection = facts.idle && !facts.finalizing
                         && !facts.writeRecoveryAvailable;
    actions.scan = facts.idle && !facts.finalizing
                   && !facts.transportActive
                   && !facts.writeRecoveryAvailable;
    actions.editEndpoint = actions.scan && !facts.transportConnecting;
    const bool captureReady = facts.idle && !facts.finalizing
                              && !facts.writeRecoveryAvailable
                              && facts.transportActive && facts.monitorReady
                              && facts.calibrationStation;
    actions.captureLow = captureReady;
    actions.captureHigh = captureReady && facts.lowComplete;
    actions.restartRun = facts.idle && !facts.finalizing
                        && !facts.writeRecoveryAvailable
                        && facts.calibrationStation && facts.hasRunData;
    actions.writeTest = captureReady && facts.testWriteAvailable
                        && !facts.productionPass;
    actions.writeProduction = captureReady && facts.productionPass;
    actions.retryVerification = facts.idle && !facts.finalizing
                                && facts.writeRecoveryAvailable;
    return actions;
}

} // namespace serein::scope::tn160ncm
