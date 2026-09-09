#pragma once

#include <serein/scope/IDataEngine.h>

namespace serein::scope::tn160ncm {

inline constexpr auto Tn160RawFrameSource = "tn160.raw-frame.v1";

class Tn160RawFrameEngine final : public IDataEngine {
public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    void reset() override;
    [[nodiscard]] ParseResult feed(QByteArrayView buffer) override;

    [[nodiscard]] static bool plausible(QByteArrayView frame);
};

} // namespace serein::scope::tn160ncm
