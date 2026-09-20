#pragma once

#include <QtCore/QString>
#include <QtCore/Qt>

namespace serein::scope::yserein_hmi {

class NumericDisplayControl final {
public:
    struct State {
        QString id = QStringLiteral("number_value");
        QString binding = QStringLiteral("telemetry.temperature");
        double previewValue = 0.0;
        int decimals = 1;
        QString unit;
        int fontPixelSize = 32;
        int alignmentIndex = 0;
    };

    NumericDisplayControl();
    explicit NumericDisplayControl(State state);

    [[nodiscard]] const State &state() const noexcept;
    [[nodiscard]] QString formattedValue() const;
    [[nodiscard]] Qt::Alignment alignment() const noexcept;

    bool setId(const QString &id);
    bool setBinding(const QString &binding);
    bool setPreviewValue(double value);
    void setDecimals(int decimals);
    void setUnit(const QString &unit);
    void setFontPixelSize(int pixelSize);
    void setAlignmentIndex(int alignmentIndex);

private:
    State m_state;
};

} // namespace serein::scope::yserein_hmi
