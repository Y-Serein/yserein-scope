#include "NumericDisplayControl.h"

#include <QtCore/QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

namespace serein::scope::yserein_hmi {
namespace {

const QRegularExpression IdentifierPattern(
    QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
const QRegularExpression BindingPattern(
    QStringLiteral("^[A-Za-z_][A-Za-z0-9_.]*$"));

} // namespace

NumericDisplayControl::NumericDisplayControl() = default;

NumericDisplayControl::NumericDisplayControl(State state)
    : m_state(std::move(state))
{
    m_state.decimals = std::clamp(m_state.decimals, 0, 6);
    m_state.unit = m_state.unit.left(12);
    m_state.fontPixelSize = std::clamp(m_state.fontPixelSize, 12, 72);
    m_state.alignmentIndex = std::clamp(m_state.alignmentIndex, 0, 2);
    if (!std::isfinite(m_state.previewValue)) m_state.previewValue = 0.0;
}

const NumericDisplayControl::State &NumericDisplayControl::state() const noexcept
{
    return m_state;
}

QString NumericDisplayControl::formattedValue() const
{
    QString result = QString::number(m_state.previewValue, 'f', m_state.decimals);
    if (!m_state.unit.isEmpty()) result += QLatin1Char(' ') + m_state.unit;
    return result;
}

Qt::Alignment NumericDisplayControl::alignment() const noexcept
{
    switch (m_state.alignmentIndex) {
    case 1: return Qt::AlignHCenter | Qt::AlignVCenter;
    case 2: return Qt::AlignRight | Qt::AlignVCenter;
    default: return Qt::AlignLeft | Qt::AlignVCenter;
    }
}

bool NumericDisplayControl::setId(const QString &id)
{
    const QString candidate = id.trimmed();
    if (!IdentifierPattern.match(candidate).hasMatch()) return false;
    m_state.id = candidate;
    return true;
}

bool NumericDisplayControl::setBinding(const QString &binding)
{
    const QString candidate = binding.trimmed();
    if (!BindingPattern.match(candidate).hasMatch()) return false;
    m_state.binding = candidate;
    return true;
}

bool NumericDisplayControl::setPreviewValue(const double value)
{
    if (!std::isfinite(value)) return false;
    m_state.previewValue = value;
    return true;
}

void NumericDisplayControl::setDecimals(const int decimals)
{
    m_state.decimals = std::clamp(decimals, 0, 6);
}

void NumericDisplayControl::setUnit(const QString &unit)
{
    m_state.unit = unit.trimmed().left(12);
}

void NumericDisplayControl::setFontPixelSize(const int pixelSize)
{
    m_state.fontPixelSize = std::clamp(pixelSize, 12, 72);
}

void NumericDisplayControl::setAlignmentIndex(const int alignmentIndex)
{
    m_state.alignmentIndex = std::clamp(alignmentIndex, 0, 2);
}

} // namespace serein::scope::yserein_hmi
