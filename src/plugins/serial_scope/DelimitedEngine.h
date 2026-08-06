#pragma once

#include <serein/scope/IDataEngine.h>

namespace serein::scope::serial {

class DelimitedEngine final : public IDataEngine {
public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    void reset() override;
    [[nodiscard]] ParseResult feed(QByteArrayView buffer) override;
};

class RawTextEngine final : public IDataEngine {
public:
    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString displayName() const override;
    void reset() override;
    [[nodiscard]] ParseResult feed(QByteArrayView buffer) override;
};

} // namespace serein::scope::serial
