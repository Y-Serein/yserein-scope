#pragma once

#include "T384CalibrationProtocol.h"

#include <QVector>

#include <cmath>

namespace serein::scope::t384 {

enum class ProductionState {
    Idle, CapturingLow, CapturingHigh, ReadyToWrite, Writing, Verifying,
    WriteUncertain, Complete, Failed
};

/** Pure state machine. The UI/transport owns HTTP and calls the result methods;
 * a qualified high-point capture produces a pending packet automatically. */
class ProductionCalibrationWorkflow final {
public:
    static constexpr int kSamplesPerPoint = 30;

    bool start(const CalibrationManifest &manifest, const quint16 lowC = 0,
               const quint16 highC = 50, ProtocolError *error = nullptr)
    {
        if (manifest.identity.size() != 32 || manifest.calibrationId == 0 || highC <= lowC) {
            if (error) error->message = QStringLiteral("invalid production calibration context");
            return false;
        }
        m_manifest = manifest; m_lowC = lowC; m_highC = highC;
        m_low.clear(); m_high.clear(); m_packet.clear(); m_error.clear();
        m_state = ProductionState::CapturingLow;
        return true;
    }

    bool ingestRawSample(const quint32 raw)
    {
        if (m_state != ProductionState::CapturingLow && m_state != ProductionState::CapturingHigh)
            return false;
        auto &bucket = m_state == ProductionState::CapturingLow ? m_low : m_high;
        bucket.append(raw);
        if (bucket.size() != kSamplesPerPoint) return true;
        if (m_state == ProductionState::CapturingLow) {
            m_state = ProductionState::CapturingHigh;
            return true;
        }
        return finishCapture();
    }

    [[nodiscard]] ProductionState state() const { return m_state; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] int acceptedLowSamples() const { return m_low.size(); }
    [[nodiscard]] int acceptedHighSamples() const { return m_high.size(); }
    [[nodiscard]] QByteArray pendingPacket() const { return m_packet; }

    // The caller invokes this immediately when ReadyToWrite: no operator
    // confirmation state exists. A timeout/disconnect must use writeUncertain.
    bool beginAutomaticWrite()
    {
        if (m_state != ProductionState::ReadyToWrite || m_packet.isEmpty()) return false;
        m_state = ProductionState::Writing;
        return true;
    }
    void writeSucceeded() { if (m_state == ProductionState::Writing) m_state = ProductionState::Verifying; }
    void writeFailed(const QString &reason)
    { fail(ProductionState::Failed, reason.isEmpty() ? QStringLiteral("device rejected calibration write") : reason); }
    void writeUncertain(const QString &reason)
    { fail(ProductionState::WriteUncertain, reason.isEmpty() ? QStringLiteral("write result unknown") : reason); }
    void verificationSucceeded() { if (m_state == ProductionState::Verifying) m_state = ProductionState::Complete; }
    void verificationFailed(const QString &reason)
    { fail(ProductionState::Failed, reason.isEmpty() ? QStringLiteral("readback verification failed") : reason); }
    // Can be called after any terminal state, including WriteUncertain.
    bool recalibrate(ProtocolError *error = nullptr) { return start(m_manifest, m_lowC, m_highC, error); }

private:
    bool finishCapture()
    {
        const auto low = mean(m_low), high = mean(m_high);
        if (!low || !high || *high <= *low) {
            fail(ProductionState::Failed, QStringLiteral("two-point response is not positive"));
            return false;
        }
        CalibrationPacket packet;
        packet.manifest = m_manifest;
        packet.payload = {m_lowC, m_highC, quint32(std::llround(*low)), quint32(std::llround(*high)),
                          qint32(std::llround((*high - *low) * 1000.0 / (m_highC - m_lowC)))};
        ProtocolError error;
        const auto wire = encodeCalibrationPacket(packet, &error);
        if (!wire) { fail(ProductionState::Failed, error.message); return false; }
        m_packet = *wire;
        m_state = ProductionState::ReadyToWrite;
        return true;
    }
    static std::optional<double> mean(const QVector<quint32> &values)
    {
        if (values.size() != kSamplesPerPoint) return std::nullopt;
        quint64 sum = 0; for (const auto value : values) sum += value;
        return double(sum) / values.size();
    }
    void fail(const ProductionState state, const QString &reason) { m_state = state; m_error = reason; }

    ProductionState m_state = ProductionState::Idle;
    CalibrationManifest m_manifest;
    quint16 m_lowC = 0, m_highC = 50;
    QVector<quint32> m_low, m_high;
    QByteArray m_packet;
    QString m_error;
};

} // namespace serein::scope::t384
