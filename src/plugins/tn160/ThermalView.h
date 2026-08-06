#pragma once

#include <QtCore/QPoint>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

namespace serein::scope::tn160 {

class ThermalView final : public QWidget {
    Q_OBJECT

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
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static QVector<QRgb> colorTable(PaletteMode mode);
    QImage m_image;
    QByteArray m_gray;
    QVector<QPoint> m_badPoints;
    PaletteMode m_paletteMode = PaletteMode::Thermal;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    bool m_crosshairVisible = true;
};

} // namespace serein::scope::tn160
