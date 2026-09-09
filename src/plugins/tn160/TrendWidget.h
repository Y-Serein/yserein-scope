#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QPointF>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

class QFrame;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QToolButton;

namespace serein::scope::tn160 {

struct TrendSample {
    double seconds = 0;
    double centerPoint = 0;
    double centerRoi = 0;
    double ntc = 0;
    double centerPointAdu = 0;
    double centerRoiAdu = 0;
    double vtempRaw = 0;
    double vtempSmooth = 0;
    int nucDecision = 255;
    int nucCount = 0;
    int nucDg = 0;
    int nucDv = 0;
    int nucDn = 0;
};

class TrendWidget final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double temperatureAxisMinimum READ temperatureAxisMinimum)
    Q_PROPERTY(double temperatureAxisMaximum READ temperatureAxisMaximum)
    Q_PROPERTY(double temperatureAxisTick READ temperatureAxisTick CONSTANT)

public:
    explicit TrendWidget(QWidget *parent = nullptr);
    void addSample(const TrendSample &sample);
    void clear();
    [[nodiscard]] int historySampleCount() const noexcept;
    [[nodiscard]] QImage renderHistoryImage(const QSize &size = QSize(1600, 900)) const;
    [[nodiscard]] double temperatureAxisMinimum() const noexcept
    {
        return m_temperatureLow;
    }
    [[nodiscard]] double temperatureAxisMaximum() const noexcept
    {
        return m_temperatureHigh;
    }
    [[nodiscard]] double temperatureAxisTick() const noexcept { return 5.0; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    enum class Mode {
        Temperature,
        Raw,
    };

    struct Series {
        double TrendSample::*member = nullptr;
        QString name;
        QString unit;
        QColor color;
        Qt::PenStyle style = Qt::SolidLine;
        qreal width = 1.5;
    };

    struct AxisRange {
        double low = 0.0;
        double high = 1.0;
        double tick = 1.0;
    };

    void setMode(Mode mode);
    void setPaused(bool paused);
    void setFollowing(bool following);
    void resetView();
    void updateSeriesControls();
    void updateTemperatureRange(const TrendSample &sample);
    [[nodiscard]] QVector<Series> activeSeries(bool dark) const;
    [[nodiscard]] QVector<TrendSample> samplesForWindow(const QVector<TrendSample> &samples,
                                                        double startSeconds,
                                                        double endSeconds) const;
    [[nodiscard]] AxisRange axisRange(const QVector<TrendSample> &samples,
                                      const QVector<Series> &series) const;
    void drawChart(QPainter &painter, const QRectF &bounds,
                   const QVector<TrendSample> &samples, Mode mode,
                   bool fullHistory, bool dark,
                   const QPointF *hoverPosition = nullptr) const;

    QVector<TrendSample> m_samples;
    QVector<TrendSample> m_history;
    QTimer m_redrawTimer;
    QTimer m_stateTimer;
    QElapsedTimer m_lastSampleAge;
    QFrame *m_toolbar = nullptr;
    QToolButton *m_temperatureMode = nullptr;
    QToolButton *m_rawMode = nullptr;
    QToolButton *m_primarySeries = nullptr;
    QToolButton *m_secondarySeries = nullptr;
    QToolButton *m_tertiarySeries = nullptr;
    QToolButton *m_follow = nullptr;
    QToolButton *m_pause = nullptr;
    QToolButton *m_reset = nullptr;
    Mode m_mode = Mode::Temperature;
    bool m_temperatureSeriesVisible[3]{true, true, false};
    bool m_rawSeriesVisible[3]{true, true, false};
    bool m_paused = false;
    bool m_following = true;
    double m_frozenEndSeconds = 0.0;
    double m_temperatureLow = 15.0;
    double m_temperatureHigh = 40.0;
    QPointF m_hoverPosition;
    bool m_hovering = false;
};

} // namespace serein::scope::tn160
