#include "T384CalibrationProtocol.h"
#include "T384ProductionWorkflow.h"

#include <QCoreApplication>
#include <QDebug>

using namespace serein::scope::t384;

static bool require(const bool value, const char *message)
{
    if (!value) qCritical() << message;
    return value;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    CalibrationPacket packet;
    packet.manifest.calibrationId = 1;
    packet.manifest.identity = calibrationIdentity("PN", "SN", "FW");
    packet.payload = {0, 50, 10000, 15000, 100000};
    ProtocolError error;
    std::optional<QByteArray> wire;
    for (const auto profile : {ThermalProfile::T256, ThermalProfile::T384,
                               ThermalProfile::T640}) {
        packet.manifest.profile = profile;
        wire = encodeCalibrationPacket(packet, &error);
        if (!require(wire && wire->size() == 128, "encode failed")) return 1;
        if (!require(decodeCalibrationPacket(*wire, packet.manifest.identity, &error).has_value(), "decode failed")) return 1;
    }
    QByteArray corrupt = *wire; corrupt[20] ^= 1;
    if (!require(!decodeCalibrationPacket(corrupt, {}, &error), "corrupt header accepted")) return 1;
    CalibrationPacket invalidIdentity = packet;
    invalidIdentity.manifest.identity = QByteArray(32, char(0xff));
    if (!require(!encodeCalibrationPacket(invalidIdentity, &error),
                 "all-FF identity accepted")) return 1;
    if (!require(passesTwoPointQualityGate(10000.0, 15000.0, 5.0, 6.0),
                 "valid two-point response rejected")) return 1;
    if (!require(!passesTwoPointQualityGate(10000.0, 10020.0, 4.0, 4.0),
                 "noisy two-point response accepted")) return 1;

    ProductionCalibrationWorkflow workflow;
    if (!require(workflow.start(packet.manifest, 0, 50, &error), "start failed")) return 1;
    for (int i = 0; i < 30; ++i) workflow.ingestRawSample(10000 + (i % 2));
    for (int i = 0; i < 30; ++i) workflow.ingestRawSample(15000 + (i % 2));
    if (!require(workflow.state() == ProductionState::ReadyToWrite, "not ready to auto-write")) return 1;
    if (!require(workflow.beginAutomaticWrite(), "automatic write not started")) return 1;
    workflow.writeUncertain(QStringLiteral("timeout"));
    if (!require(workflow.state() == ProductionState::WriteUncertain, "write uncertainty lost")) return 1;
    if (!require(workflow.recalibrate(&error), "recalibration failed")) return 1;
    return 0;
}
