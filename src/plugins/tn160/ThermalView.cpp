#include "ThermalView.h"

#include <QtGui/QGradient>
#include <QtGui/QPainter>

#include <algorithm>

namespace serein::scope::tn160 {

ThermalView::ThermalView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(220, 160);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

QVector<QRgb> ThermalView::colorTable(const PaletteMode mode)
{
    struct Stop { int index; QColor color; };
    if (mode == PaletteMode::Grayscale) {
        QVector<QRgb> grayscale(256);
        for (int index = 0; index < grayscale.size(); ++index) {
            grayscale[index] = qRgb(index, index, index);
        }
        return grayscale;
    }
    const QVector<Stop> stops = mode == PaletteMode::Gain
        ? QVector<Stop>{
            {0, QColor(QStringLiteral("#183a8f"))},
            {64, QColor(QStringLiteral("#167dcc"))},
            {128, QColor(QStringLiteral("#2fb47c"))},
            {192, QColor(QStringLiteral("#f1cf45"))},
            {255, QColor(QStringLiteral("#d9342b"))},
        }
        : QVector<Stop>{
            {0, QColor(QStringLiteral("#080411"))},
            {48, QColor(QStringLiteral("#3b0f70"))},
            {96, QColor(QStringLiteral("#8c2981"))},
            {144, QColor(QStringLiteral("#de4968"))},
            {196, QColor(QStringLiteral("#fe9f6d"))},
            {255, QColor(QStringLiteral("#fcfdbf"))},
        };
    QVector<QRgb> palette(256);
    for (int segment = 0; segment + 1 < stops.size(); ++segment) {
        const Stop &left = stops[segment];
        const Stop &right = stops[segment + 1];
        for (int index = left.index; index <= right.index; ++index) {
            const double t = double(index - left.index) / (right.index - left.index);
            const int red = int(left.color.red() + t * (right.color.red() - left.color.red()));
            const int green = int(left.color.green() + t * (right.color.green() - left.color.green()));
            const int blue = int(left.color.blue() + t * (right.color.blue() - left.color.blue()));
            palette[index] = qRgb(red, green, blue);
        }
    }
    return palette;
}

void ThermalView::setFrame(const QByteArray &gray, const int width, const int height)
{
    if (gray.size() != width * height) {
        return;
    }
    m_gray = gray;
    m_frameWidth = width;
    m_frameHeight = height;
    QImage image(reinterpret_cast<const uchar *>(gray.constData()), width, height,
                 width, QImage::Format_Indexed8);
    image.setColorTable(colorTable(m_paletteMode));
    m_image = image.copy();
    update();
}

void ThermalView::setPaletteMode(const PaletteMode mode)
{
    if (m_paletteMode == mode) return;
    m_paletteMode = mode;
    if (!m_gray.isEmpty()) {
        const QByteArray frame = m_gray;
        setFrame(frame, m_frameWidth, m_frameHeight);
    }
}

void ThermalView::setBadPoints(const QVector<QPoint> &points)
{
    m_badPoints = points;
    update();
}

void ThermalView::setCrosshairVisible(const bool visible)
{
    m_crosshairVisible = visible;
    update();
}

void ThermalView::clear()
{
    m_image = {};
    m_gray.clear();
    m_badPoints.clear();
    m_frameWidth = 0;
    m_frameHeight = 0;
    update();
}

void ThermalView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    if (m_image.isNull()) {
        const bool dark = property("darkMode").toBool();
        painter.fillRect(rect(), dark ? QColor(QStringLiteral("#181c20"))
                                      : QColor(QStringLiteral("#fbfcfd")));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(dark ? QColor(QStringLiteral("#aab5bf"))
                            : QColor(QStringLiteral("#7a8699")));
        QFont titleFont = painter.font();
        titleFont.setPointSizeF(11.0);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.drawText(rect().adjusted(14, 0, -14, 0),
                         Qt::AlignHCenter | Qt::AlignVCenter, tr("等待图像流"));
        QFont hintFont = painter.font();
        hintFont.setPointSizeF(8.5);
        hintFont.setWeight(QFont::Normal);
        painter.setFont(hintFont);
        painter.setPen(dark ? QColor(QStringLiteral("#87939e"))
                            : QColor(QStringLiteral("#98a2b3")));
        painter.drawText(QRectF(14, height() / 2.0 + 14, width() - 28, 18),
                         Qt::AlignHCenter | Qt::AlignTop,
                         QStringLiteral("Center 5×5  /  —.-- °C"));
        return;
    }
    painter.fillRect(rect(), QColor(QStringLiteral("#0b1018")));
    QSize size = m_image.size();
    size.scale(rect().size() - QSize(24, 24), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, m_image);

    if (m_crosshairVisible) {
        const QPoint center = target.center();
        painter.setPen(QPen(QColor(QStringLiteral("#171a1f")), 3));
        painter.drawLine(center.x() - 8, center.y(), center.x() + 8, center.y());
        painter.drawLine(center.x(), center.y() - 8, center.x(), center.y() + 8);
        painter.setPen(QPen(QColor(QStringLiteral("#ffffff")), 1));
        painter.drawLine(center.x() - 8, center.y(), center.x() + 8, center.y());
        painter.drawLine(center.x(), center.y() - 8, center.x(), center.y() + 8);
    }
    if (m_frameWidth > 0 && m_frameHeight > 0 && !m_badPoints.isEmpty()) {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#d92d20")), 2));
        painter.setBrush(Qt::NoBrush);
        for (const QPoint &point : m_badPoints) {
            const qreal x = target.left() + (point.x() + 0.5) * target.width() / m_frameWidth;
            const qreal y = target.top() + (point.y() + 0.5) * target.height() / m_frameHeight;
            painter.drawEllipse(QPointF(x, y), 4.0, 4.0);
        }
    }
    painter.setPen(QPen(QColor(QStringLiteral("#283548")), 1));
    painter.drawRect(target.adjusted(-1, -1, 1, 1));
}

} // namespace serein::scope::tn160
