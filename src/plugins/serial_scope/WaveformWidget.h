#pragma once

#include <QtCore/QQueue>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

namespace serein::scope::serial {

class WaveformWidget final : public QWidget {
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void addSample(const QVector<double> &values, const QStringList &labels);
    void setChannelVisible(int index, bool visible);
    void clear();
    [[nodiscard]] QColor channelColor(int index) const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    static constexpr int Capacity = 1200;
    QVector<QQueue<double>> m_series;
    QStringList m_labels;
    QVector<bool> m_visible;
};

} // namespace serein::scope::serial
