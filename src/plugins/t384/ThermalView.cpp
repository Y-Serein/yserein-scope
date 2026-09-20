#include "ThermalView.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QSizePolicy>

#include <algorithm>
#include <cmath>

namespace serein::scope::t384 {
namespace {

void drawHollowCross(QPainter &painter, const QPointF &center, const QColor &color)
{
    constexpr qreal InnerRadius = 3.5;
    constexpr qreal OuterRadius = 11.0;
    const QLineF arms[] = {
        {center + QPointF(-OuterRadius, 0), center + QPointF(-InnerRadius, 0)},
        {center + QPointF(InnerRadius, 0), center + QPointF(OuterRadius, 0)},
        {center + QPointF(0, -OuterRadius), center + QPointF(0, -InnerRadius)},
        {center + QPointF(0, InnerRadius), center + QPointF(0, OuterRadius)},
    };
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(5, 9, 13, 210), 4.0,
                        Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    painter.drawLines(arms, 4);
    painter.setPen(QPen(color, 2.0, Qt::SolidLine,
                        Qt::FlatCap, Qt::MiterJoin));
    painter.drawLines(arms, 4);
    painter.restore();
}

} // namespace

ThermalView::ThermalView(QWidget *parent)
    : QWidget(parent)
{
    // The canvas owns the normal editing minimum. The renderer itself must
    // remain shrinkable so a constrained dock never clips the 4:3 image.
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::ArrowCursor);
}

const QVector<QRgb> &ThermalView::colorTable(const PaletteMode mode)
{
    struct Stop { int index; QColor color; };
    static const QVector<QRgb> grayscale = [] {
        QVector<QRgb> grayscale(256);
        for (int index = 0; index < grayscale.size(); ++index) {
            grayscale[index] = qRgb(index, index, index);
        }
        return grayscale;
    }();
    const auto makePalette = [](const QVector<Stop> &stops) {
        QVector<QRgb> palette(256);
        for (int segment = 0; segment + 1 < stops.size(); ++segment) {
            const Stop &left = stops[segment];
            const Stop &right = stops[segment + 1];
            for (int index = left.index; index <= right.index; ++index) {
                const double t = double(index - left.index) / (right.index - left.index);
                const int red = int(left.color.red()
                                    + t * (right.color.red() - left.color.red()));
                const int green = int(left.color.green()
                                      + t * (right.color.green() - left.color.green()));
                const int blue = int(left.color.blue()
                                     + t * (right.color.blue() - left.color.blue()));
                palette[index] = qRgb(red, green, blue);
            }
        }
        return palette;
    };
    static const QVector<QRgb> gain = makePalette({
            {0, QColor(QStringLiteral("#183a8f"))},
            {64, QColor(QStringLiteral("#167dcc"))},
            {128, QColor(QStringLiteral("#2fb47c"))},
            {192, QColor(QStringLiteral("#f1cf45"))},
            {255, QColor(QStringLiteral("#d9342b"))},
        });
    static const QVector<QRgb> thermal = makePalette({
            {0, QColor(QStringLiteral("#080411"))},
            {48, QColor(QStringLiteral("#3b0f70"))},
            {96, QColor(QStringLiteral("#8c2981"))},
            {144, QColor(QStringLiteral("#de4968"))},
            {196, QColor(QStringLiteral("#fe9f6d"))},
            {255, QColor(QStringLiteral("#fcfdbf"))},
        });
    if (mode == PaletteMode::Grayscale) return grayscale;
    return mode == PaletteMode::Gain ? gain : thermal;
}

void ThermalView::setFrame(const QByteArray &gray, const int width, const int height)
{
    if (gray.size() != width * height) return;
    m_gray = gray;
    m_frameWidth = width;
    m_frameHeight = height;
    if (m_selectedX < 0 || m_selectedX >= width
        || m_selectedY < 0 || m_selectedY >= height) {
        m_selectedX = width / 2;
        m_selectedY = height / 2;
    }
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
    setCursor(visible ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void ThermalView::setExtremaVisible(const bool visible)
{
    m_extremaVisible = visible;
    update();
}

void ThermalView::setFrameInfo(const ThermalFrameInfo &info)
{
    m_frameInfo = info;
    update();
}

void ThermalView::clear()
{
    m_image = {};
    m_gray.clear();
    m_badPoints.clear();
    m_frameInfo = {};
    m_imageTarget = {};
    m_frameWidth = 0;
    m_frameHeight = 0;
    m_selectedX = -1;
    m_selectedY = -1;
    update();
}

void ThermalView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = property("darkMode").toBool();
    if (m_image.isNull()) {
        painter.fillRect(rect(), dark ? QColor(QStringLiteral("#181c20"))
                                      : QColor(QStringLiteral("#fbfcfd")));
        painter.setPen(dark ? QColor(QStringLiteral("#d8dde3"))
                            : QColor(QStringLiteral("#475467")));
        QFont titleFont = painter.font();
        titleFont.setPointSizeF(13.0);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.drawText(rect().adjusted(14, 0, -14, 0),
                         Qt::AlignHCenter | Qt::AlignVCenter, tr("等待图像流"));
        return;
    }

    painter.fillRect(rect(), dark ? QColor(QStringLiteral("#181c20"))
                                  : QColor(QStringLiteral("#fbfcfd")));
    const bool calibrated = std::isfinite(m_frameInfo.tLowC)
        && std::isfinite(m_frameInfo.tHighC)
        && m_frameInfo.tHighC > m_frameInfo.tLowC;

    QRect content = rect().adjusted(12, 12, -12, -12);
    const int colorBarReserve = calibrated && content.width() >= 340 ? 72 : 0;
    const QSize imageAreaSize(std::max(1, content.width() - colorBarReserve),
                              content.height());
    QSize size = m_image.size();
    size.scale(imageAreaSize, Qt::KeepAspectRatio);
    const int groupWidth = size.width() + colorBarReserve;
    m_imageTarget = QRect(QPoint(content.center().x() - groupWidth / 2,
                                content.center().y() - size.height() / 2), size);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(m_imageTarget, m_image);

    if (calibrated && colorBarReserve > 0) {
        const QRect bar(m_imageTarget.right() + 18, m_imageTarget.top(),
                        14, m_imageTarget.height());
        const QVector<QRgb> &palette = colorTable(PaletteMode::Thermal);
        for (int y = 0; y < bar.height(); ++y) {
            const int index = std::clamp(255 - y * 255 / std::max(1, bar.height() - 1),
                                         0, 255);
            painter.setPen(QColor::fromRgb(palette.at(index)));
            painter.drawLine(bar.left(), bar.top() + y, bar.right(), bar.top() + y);
        }
        painter.setPen(dark ? QColor(QStringLiteral("#52616e"))
                            : QColor(QStringLiteral("#98a2b3")));
        painter.drawRect(bar.adjusted(-1, -1, 1, 1));
        QFont scaleFont = painter.font();
        scaleFont.setPointSizeF(9.0);
        painter.setFont(scaleFont);
        painter.setPen(dark ? QColor(QStringLiteral("#c5ced6"))
                            : QColor(QStringLiteral("#475467")));
        painter.drawText(QRectF(bar.right() + 7, bar.top() - 8, 42, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1°C").arg(m_frameInfo.tHighC, 0, 'f', 1));
        painter.drawText(QRectF(bar.right() + 7, bar.bottom() - 9, 42, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1°C").arg(m_frameInfo.tLowC, 0, 'f', 1));
    }

    const auto mappedPoint = [this](const int index) {
        const int x = index % m_frameWidth;
        const int y = index / m_frameWidth;
        return QPointF(m_imageTarget.left()
                           + (x + 0.5) * m_imageTarget.width() / m_frameWidth,
                       m_imageTarget.top()
                           + (y + 0.5) * m_imageTarget.height() / m_frameHeight);
    };

    if (m_extremaVisible && !m_gray.isEmpty()) {
        int minIndex = 0;
        int maxIndex = 0;
        int minLevel = 255;
        int maxLevel = 0;
        for (int index = 0; index < m_gray.size(); ++index) {
            const int level = quint8(m_gray.at(index));
            if (level < minLevel) {
                minLevel = level;
                minIndex = index;
            }
            if (level > maxLevel) {
                maxLevel = level;
                maxIndex = index;
            }
        }
        drawHollowCross(painter, mappedPoint(maxIndex),
                        QColor(QStringLiteral("#ff453a")));
        drawHollowCross(painter, mappedPoint(minIndex),
                        QColor(QStringLiteral("#3b82f6")));
    }

    if (m_crosshairVisible && m_selectedX >= 0 && m_selectedY >= 0) {
        drawHollowCross(painter,
                        mappedPoint(m_selectedY * m_frameWidth + m_selectedX),
                        QColor(QStringLiteral("#ffffff")));
    }

    if (m_frameWidth > 0 && m_frameHeight > 0 && !m_badPoints.isEmpty()) {
        painter.setPen(QPen(QColor(QStringLiteral("#ff544d")), 2));
        painter.setBrush(Qt::NoBrush);
        for (const QPoint &point : m_badPoints) {
            const qreal x = m_imageTarget.left()
                + (point.x() + 0.5) * m_imageTarget.width() / m_frameWidth;
            const qreal y = m_imageTarget.top()
                + (point.y() + 0.5) * m_imageTarget.height() / m_frameHeight;
            painter.drawEllipse(QPointF(x, y), 4.0, 4.0);
        }
    }
    painter.setPen(QPen(dark ? QColor(QStringLiteral("#3c4955"))
                             : QColor(QStringLiteral("#98a2b3")), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(m_imageTarget.adjusted(-1, -1, 1, 1));
}

void ThermalView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_imageTarget.contains(event->position().toPoint())
        && m_frameWidth > 0 && m_frameHeight > 0) {
        m_selectedX = std::clamp(
            int((event->position().x() - m_imageTarget.left()) * m_frameWidth
                / std::max(1, m_imageTarget.width())), 0, m_frameWidth - 1);
        m_selectedY = std::clamp(
            int((event->position().y() - m_imageTarget.top()) * m_frameHeight
                / std::max(1, m_imageTarget.height())), 0, m_frameHeight - 1);
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

} // namespace serein::scope::t384
