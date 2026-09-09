#pragma once

#include <serein/scope/IDataEngine.h>

#include <QtCore/QList>

namespace serein::scope::tn160 {

inline constexpr auto Tn160RawFrameSource = "tn160.raw-frame.v1";
inline constexpr auto Tn160WriteReplySource = "tn160.write-reply.v1";
inline constexpr auto Tn160CalibrationStreamSource =
    "tn160.calibration-stream.v1";

class Tn160DiscardEngine final : public IDataEngine {
public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    void reset() override;
    [[nodiscard]] ParseResult feed(QByteArrayView buffer) override;
};

class Tn160RawFrameEngine final : public IDataEngine {
public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    void reset() override;
    [[nodiscard]] ParseResult feed(QByteArrayView buffer) override;

    [[nodiscard]] static bool plausible(QByteArrayView frame);
};

class Tn160WriteReplyEngine final : public IDataEngine {
public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    void reset() override;
    [[nodiscard]] ParseResult feed(QByteArrayView buffer) override;
};

class Tn160CalibrationStreamEngine final : public IDataEngine {
public:
    explicit Tn160CalibrationStreamEngine(QList<int> payloadSizes);

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    void reset() override;
    [[nodiscard]] ParseResult feed(QByteArrayView buffer) override;

private:
    QList<int> m_payloadSizes;
    int m_payloadIndex = 0;
};

} // namespace serein::scope::tn160
