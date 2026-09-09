#include "WaveformWidget.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace serein::scope::serial {
namespace {

const QStringList Colors{
    QStringLiteral("#246bce"), QStringLiteral("#e07a1f"),
    QStringLiteral("#008c7a"), QStringLiteral("#8b5cf6"),
    QStringLiteral("#168653"), QStringLiteral("#d92d20"),
    QStringLiteral("#a86b00"), QStringLiteral("#1877a4"),
};

} // namespace

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(280);
}

void WaveformWidget::addSample(const QVector<double> &values,
                               const QStringList &labels,
                               const bool repaint)
{
    while (m_series.size() < values.size()) {
        const int index = m_series.size();
        m_series.append(QQueue<double>{});
        m_labels.append(index < labels.size() ? labels.at(index)
                                              : QStringLiteral("CH%1").arg(index + 1));
        m_visible.append(true);
    }
    for (int index = 0; index < values.size(); ++index) {
        QQueue<double> &series = m_series[index];
        series.enqueue(values[index]);
        while (series.size() > Capacity) {
            series.dequeue();
        }
    }
    if (repaint) update();
}

void WaveformWidget::refreshDisplay()
{
    update();
}

void WaveformWidget::setChannelVisible(const int index, const bool visible)
{
    if (index >= 0 && index < m_visible.size()) {
        m_visible[index] = visible;
        update();
    }
}

void WaveformWidget::clear()
{
    m_series.clear();
    m_labels.clear();
    m_visible.clear();
    update();
}

QColor WaveformWidget::channelColor(const int index) const
{
    return QColor(Colors.at(index % Colors.size()));
}

qreal WaveformWidget::lineWidth() const noexcept
{
    return m_lineWidth;
}

void WaveformWidget::setLineWidth(const qreal width)
{
    const qreal bounded = std::clamp(width, qreal(0.5), qreal(4.0));
    if (qFuzzyCompare(m_lineWidth, bounded)) return;
    m_lineWidth = bounded;
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = property("darkMode").toBool();
    const QColor surface(dark ? QStringLiteral("#252b31") : QStringLiteral("#ffffff"));
    const QColor grid(dark ? QStringLiteral("#34404a") : QStringLiteral("#e7e9ed"));
    const QColor axis(dark ? QStringLiteral("#aab5bf") : QStringLiteral("#667085"));
    painter.fillRect(rect(), surface);
    const QRectF plot = QRectF(rect()).adjusted(54, 22, -20, -38);
    if (plot.width() <= 0 || plot.height() <= 0) {
        return;
    }

    painter.setPen(QPen(grid, 1));
    for (int step = 0; step <= 5; ++step) {
        const qreal y = plot.top() + step * plot.height() / 5.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    for (int step = 0; step <= 10; ++step) {
        const qreal x = plot.left() + step * plot.width() / 10.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    int sampleCount = 0;
    for (int index = 0; index < m_series.size(); ++index) {
        if (!m_visible[index]) {
            continue;
        }
        sampleCount = std::max(sampleCount, int(m_series[index].size()));
        for (const double value : m_series[index]) {
            if (std::isfinite(value)) {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }
    }
    if (!std::isfinite(minimum)) {
        painter.setPen(dark ? QColor(QStringLiteral("#9ba6b1"))
                            : QColor(QStringLiteral("#98a1ae")));
        painter.drawText(plot, Qt::AlignCenter,
                         tr("等待数据 · 示例格式  1.2, 3.4, 5.6"));
        return;
    }
    if (qFuzzyCompare(minimum, maximum)) {
        const double margin = std::max(std::abs(minimum) * 0.05, 1.0);
        minimum -= margin;
        maximum += margin;
    } else {
        const double margin = (maximum - minimum) * 0.08;
        minimum -= margin;
        maximum += margin;
    }

    painter.setPen(axis);
    painter.drawText(QRectF(2, plot.top() - 8, 48, 20), Qt::AlignRight,
                     QString::number(maximum, 'g', 4));
    painter.drawText(QRectF(2, plot.bottom() - 10, 48, 20), Qt::AlignRight,
                     QString::number(minimum, 'g', 4));
    painter.drawText(QRectF(plot.left(), plot.bottom() + 10, plot.width(), 20),
                     Qt::AlignLeft, tr("%1 samples").arg(sampleCount));

    const double range = maximum - minimum;
    for (int index = 0; index < m_series.size(); ++index) {
        const QQueue<double> &series = m_series[index];
        if (!m_visible[index] || series.size() < 2) {
            continue;
        }
        QPainterPath path;
        bool started = false;
        for (int sample = 0; sample < series.size(); ++sample) {
            const double value = series[sample];
            if (!std::isfinite(value)) {
                started = false;
                continue;
            }
            const qreal x = plot.left() + sample * plot.width() / (series.size() - 1.0);
            const qreal y = plot.bottom() - (value - minimum) * plot.height() / range;
            if (started) {
                path.lineTo(x, y);
            } else {
                path.moveTo(x, y);
                started = true;
            }
        }
        painter.setPen(QPen(channelColor(index), m_lineWidth));
        painter.drawPath(path);
    }
}

} // namespace serein::scope::serial
