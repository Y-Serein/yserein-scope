#pragma once

#include <QtCore/QQueue>
#include <QtWidgets/QWidget>

namespace serein::scope::tn160 {

struct TrendSample {
    double seconds = 0;
    double center = 0;
    double ntc = 0;
    double centerAdu = 0;
    double vtemp = 0;
};

class TrendWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TrendWidget(QWidget *parent = nullptr);
    void addSample(const TrendSample &sample);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void drawPanel(QPainter &painter, const QRectF &rect, const QString &title,
                   const QColor &firstColor, const QColor &secondColor,
                   double TrendSample::*first, double TrendSample::*second,
                   const QString &firstName, const QString &secondName);
    QQueue<TrendSample> m_samples;
};

} // namespace serein::scope::tn160
