#include "TrendWidget.h"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace serein::scope::tn160 {

TrendWidget::TrendWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(120);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void TrendWidget::addSample(const TrendSample &sample)
{
    m_samples.enqueue(sample);
    while (m_samples.size() > 900) {
        m_samples.dequeue();
    }
    update();
}

void TrendWidget::clear()
{
    m_samples.clear();
    update();
}

void TrendWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = property("darkMode").toBool();
    painter.fillRect(rect(), QColor(dark ? QStringLiteral("#252b31")
                                       : QStringLiteral("#ffffff")));
    const QRectF area = QRectF(rect()).adjusted(18, 14, -18, -14);
    const qreal gap = 16;
    const qreal height = (area.height() - gap) / 2.0;
    drawPanel(painter, QRectF(area.left(), area.top(), area.width(), height),
              tr("温度 / °C"), QColor(QStringLiteral("#246bce")),
              QColor(QStringLiteral("#e07a1f")), &TrendSample::center,
              &TrendSample::ntc, QStringLiteral("CENTER"), QStringLiteral("NTC"));
    drawPanel(painter, QRectF(area.left(), area.top() + height + gap, area.width(), height),
              tr("原始量 / ADU"), QColor(QStringLiteral("#008c7a")),
              QColor(QStringLiteral("#8b5cf6")), &TrendSample::centerAdu,
              &TrendSample::vtemp, QStringLiteral("CENTER ADU"), QStringLiteral("VTEMP"));
}

void TrendWidget::drawPanel(QPainter &painter, const QRectF &rect, const QString &title,
                            const QColor &firstColor, const QColor &secondColor,
                            double TrendSample::*first, double TrendSample::*second,
                            const QString &firstName, const QString &secondName)
{
    const bool dark = property("darkMode").toBool();
    QFont titleFont = painter.font();
    titleFont.setPointSizeF(9.0);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor(dark ? QStringLiteral("#d8dde3")
                               : QStringLiteral("#334155")));
    painter.drawText(rect.adjusted(2, 0, -2, 0), Qt::AlignTop | Qt::AlignLeft, title);

    QFont legendFont = painter.font();
    legendFont.setPointSizeF(7.5);
    legendFont.setWeight(QFont::Medium);
    painter.setFont(legendFont);
    const qreal legendY = rect.top() + 8.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(firstColor);
    painter.drawEllipse(QPointF(rect.right() - 176.0, legendY), 3.0, 3.0);
    painter.setPen(firstColor);
    painter.drawText(QRectF(rect.right() - 168.0, rect.top(), 78.0, 18.0),
                     Qt::AlignVCenter | Qt::AlignLeft, firstName);
    painter.setPen(Qt::NoPen);
    painter.setBrush(secondColor);
    painter.drawEllipse(QPointF(rect.right() - 82.0, legendY), 3.0, 3.0);
    painter.setPen(secondColor);
    painter.drawText(QRectF(rect.right() - 74.0, rect.top(), 72.0, 18.0),
                     Qt::AlignVCenter | Qt::AlignLeft, secondName);

    const QRectF plot = rect.adjusted(48, 24, -6, -14);
    painter.fillRect(plot, QColor(dark ? QStringLiteral("#1c2228")
                                      : QStringLiteral("#fbfcfe")));
    painter.setPen(QPen(QColor(dark ? QStringLiteral("#34404a")
                                   : QStringLiteral("#e8edf3")), 1));
    for (int step = 0; step <= 4; ++step) {
        const qreal y = plot.top() + step * plot.height() / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    for (int step = 0; step <= 6; ++step) {
        const qreal x = plot.left() + step * plot.width() / 6.0;
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    if (m_samples.size() < 2) {
        QFont emptyFont = painter.font();
        emptyFont.setPointSizeF(8.0);
        emptyFont.setWeight(QFont::Normal);
        painter.setFont(emptyFont);
        painter.setPen(QColor(dark ? QStringLiteral("#9ba6b1")
                                   : QStringLiteral("#94a3b8")));
        painter.drawText(plot, Qt::AlignCenter, tr("等待数据"));
        return;
    }

    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    for (const TrendSample &sample : m_samples) {
        for (const double value : {sample.*first, sample.*second}) {
            if (std::isfinite(value)) {
                low = std::min(low, value);
                high = std::max(high, value);
            }
        }
    }
    if (!std::isfinite(low)) {
        return;
    }
    if (qFuzzyCompare(low, high)) {
        low -= 1;
        high += 1;
    } else {
        const double margin = (high - low) * 0.08;
        low -= margin;
        high += margin;
    }
    QFont axisFont = painter.font();
    axisFont.setPointSizeF(7.5);
    axisFont.setWeight(QFont::Normal);
    painter.setFont(axisFont);
    painter.setPen(QColor(dark ? QStringLiteral("#aab5bf")
                               : QStringLiteral("#7c899b")));
    painter.drawText(QRectF(rect.left() + 2, plot.top() - 7, 42, 18), Qt::AlignRight,
                     QString::number(high, 'g', 4));
    painter.drawText(QRectF(rect.left() + 2, plot.bottom() - 9, 42, 18), Qt::AlignRight,
                     QString::number(low, 'g', 4));

    const auto drawSeries = [&](double TrendSample::*member, const QColor &color) {
        QPainterPath path;
        bool started = false;
        for (int index = 0; index < m_samples.size(); ++index) {
            const double value = m_samples[index].*member;
            if (!std::isfinite(value)) {
                started = false;
                continue;
            }
            const qreal x = plot.left() + index * plot.width() / (m_samples.size() - 1.0);
            const qreal y = plot.bottom() - (value - low) * plot.height() / (high - low);
            if (started) path.lineTo(x, y); else { path.moveTo(x, y); started = true; }
        }
        painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    };
    drawSeries(first, firstColor);
    drawSeries(second, secondColor);
}

} // namespace serein::scope::tn160
