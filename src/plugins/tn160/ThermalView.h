#pragma once

#include <QtCore/QPoint>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

#include <limits>

class QMouseEvent;

namespace serein::scope::tn160 {

struct ThermalFrameInfo {
    double centerPointC = std::numeric_limits<double>::quiet_NaN();
    double centerRoiC = std::numeric_limits<double>::quiet_NaN();
    double centerPointAdu = std::numeric_limits<double>::quiet_NaN();
    double centerRoiAdu = std::numeric_limits<double>::quiet_NaN();
    double ntcC = std::numeric_limits<double>::quiet_NaN();
    double tLowC = std::numeric_limits<double>::quiet_NaN();
    double tHighC = std::numeric_limits<double>::quiet_NaN();
    double vtemp = std::numeric_limits<double>::quiet_NaN();
    double fps = std::numeric_limits<double>::quiet_NaN();
};

class ThermalView final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool crosshairVisible READ crosshairVisible)
    Q_PROPERTY(bool extremaVisible READ extremaVisible)
    Q_PROPERTY(QPoint selectedPoint READ selectedPoint)

public:
    enum class PaletteMode {
        Thermal,
        Grayscale,
        Gain,
    };

    explicit ThermalView(QWidget *parent = nullptr);
    void setFrame(const QByteArray &gray, int width = 160, int height = 120);
    void setPaletteMode(PaletteMode mode);
    void setBadPoints(const QVector<QPoint> &points);
    void setCrosshairVisible(bool visible);
    void setExtremaVisible(bool visible);
    void setFrameInfo(const ThermalFrameInfo &info);
    void clear();
    [[nodiscard]] bool crosshairVisible() const noexcept { return m_crosshairVisible; }
    [[nodiscard]] bool extremaVisible() const noexcept { return m_extremaVisible; }
    [[nodiscard]] QPoint selectedPoint() const noexcept
    {
        return QPoint(m_selectedX, m_selectedY);
    }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    static const QVector<QRgb> &colorTable(PaletteMode mode);
    QImage m_image;
    QByteArray m_gray;
    QVector<QPoint> m_badPoints;
    ThermalFrameInfo m_frameInfo;
    QRect m_imageTarget;
    PaletteMode m_paletteMode = PaletteMode::Thermal;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    int m_selectedX = -1;
    int m_selectedY = -1;
    bool m_crosshairVisible = false;
    bool m_extremaVisible = false;
};

} // namespace serein::scope::tn160
