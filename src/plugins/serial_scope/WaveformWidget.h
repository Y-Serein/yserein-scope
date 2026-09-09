#pragma once

#include <QtCore/QQueue>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

namespace serein::scope::serial {

class WaveformWidget final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal lineWidth READ lineWidth WRITE setLineWidth)

public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void addSample(const QVector<double> &values, const QStringList &labels,
                   bool repaint = true);
    void refreshDisplay();
    void setChannelVisible(int index, bool visible);
    void clear();
    [[nodiscard]] QColor channelColor(int index) const;
    [[nodiscard]] qreal lineWidth() const noexcept;
    void setLineWidth(qreal width);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr int Capacity = 1200;
    QVector<QQueue<double>> m_series;
    QStringList m_labels;
    QVector<bool> m_visible;
    qreal m_lineWidth = 1.35;
};

} // namespace serein::scope::serial
