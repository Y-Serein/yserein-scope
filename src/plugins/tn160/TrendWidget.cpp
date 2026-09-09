#include "TrendWidget.h"

#include <QtCore/QSignalBlocker>
#include <QtCore/QStringList>
#include <QtCore/QtMath>
#include <QtGui/QFontMetricsF>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QResizeEvent>
#include <QtGui/QShortcut>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace serein::scope::tn160 {
namespace {

constexpr int ToolbarHeight = 32;
constexpr int MaximumDisplaySamples = 36000;
constexpr double DisplayWindowSeconds = 180.0;
constexpr double DisplayRetentionSeconds = 600.0;
constexpr qint64 StaleThresholdMs = 2500;
constexpr qreal ChartTitlePointSize = 11.0;
constexpr qreal ChartTextPointSize = 10.0;

QString axisText(const double value, const bool raw)
{
    if (!std::isfinite(value)) return QStringLiteral("—");
    return QString::number(value, 'f', raw ? 0 : 1);
}

QString relativeTimeText(const double relativeSeconds)
{
    const int rounded = qRound(std::abs(relativeSeconds));
    const int minutes = rounded / 60;
    const int seconds = rounded % 60;
    return QStringLiteral("%1%2:%3")
        .arg(relativeSeconds < -0.5 ? QStringLiteral("-") : QString())
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString nucLabel(const int decision)
{
    switch (decision) {
    case 0: return QStringLiteral("FFC");
    case 1: return QStringLiteral("REJ VN");
    case 2: return QStringLiteral("REJ LIM");
    case 3: return QStringLiteral("REJ JMP");
    case 4: return QStringLiteral("NO FRAME");
    default: return QStringLiteral("EV %1").arg(decision);
    }
}

QColor eventColor(const int decision, const bool dark)
{
    if (decision == 0) {
        return QColor(dark ? QStringLiteral("#8996a2")
                           : QStringLiteral("#89919d"));
    }
    return QColor(dark ? QStringLiteral("#d7a85b")
                       : QStringLiteral("#a46a00"));
}

double niceStep(const double value)
{
    if (!std::isfinite(value) || value <= 0.0) return 1.0;
    const double exponent = std::floor(std::log10(value));
    const double magnitude = std::pow(10.0, exponent);
    const double normalized = value / magnitude;
    const double step = normalized <= 1.0 ? 1.0
        : normalized <= 2.0 ? 2.0
        : normalized <= 5.0 ? 5.0 : 10.0;
    return step * magnitude;
}

} // namespace

TrendWidget::TrendWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName(QStringLiteral("trendToolbar"));
    auto *toolbarLayout = new QHBoxLayout(m_toolbar);
    toolbarLayout->setContentsMargins(8, 3, 8, 3);
    toolbarLayout->setSpacing(4);

    auto *modeGroup = new QButtonGroup(m_toolbar);
    modeGroup->setExclusive(true);
    const auto makeModeButton = [this, toolbarLayout, modeGroup](const QString &text) {
        auto *button = new QToolButton(m_toolbar);
        button->setObjectName(QStringLiteral("trendModeButton"));
        button->setText(text);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        modeGroup->addButton(button);
        toolbarLayout->addWidget(button);
        return button;
    };
    m_temperatureMode = makeModeButton(tr("温度"));
    m_temperatureMode->setProperty("mode", QStringLiteral("temperature"));
    m_temperatureMode->setChecked(true);
    m_rawMode = makeModeButton(tr("原始量"));
    m_rawMode->setProperty("mode", QStringLiteral("raw"));

    auto *separator = new QFrame(m_toolbar);
    separator->setObjectName(QStringLiteral("trendToolbarSeparator"));
    separator->setFixedWidth(1);
    toolbarLayout->addSpacing(3);
    toolbarLayout->addWidget(separator);
    toolbarLayout->addSpacing(3);

    const auto makeSeriesButton = [this, toolbarLayout]() {
        auto *button = new QToolButton(m_toolbar);
        button->setObjectName(QStringLiteral("trendSeriesButton"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        toolbarLayout->addWidget(button);
        return button;
    };
    m_primarySeries = makeSeriesButton();
    m_secondarySeries = makeSeriesButton();
    m_tertiarySeries = makeSeriesButton();
    toolbarLayout->addStretch(1);

    const auto makeActionButton = [this, toolbarLayout](const QString &text,
                                                        const QString &name) {
        auto *button = new QToolButton(m_toolbar);
        button->setObjectName(name);
        button->setText(text);
        button->setCursor(Qt::PointingHandCursor);
        toolbarLayout->addWidget(button);
        return button;
    };
    m_follow = makeActionButton(tr("跟随"), QStringLiteral("trendFollowButton"));
    m_follow->setCheckable(true);
    m_follow->setChecked(true);
    m_follow->setToolTip(tr("跟随最新数据（F）"));
    m_pause = makeActionButton(tr("暂停"), QStringLiteral("trendPauseButton"));
    m_pause->setCheckable(true);
    m_pause->setToolTip(tr("暂停或继续视图（Space）"));
    m_reset = makeActionButton(tr("复位"), QStringLiteral("trendResetButton"));
    m_reset->setToolTip(tr("恢复默认温度量程与实时跟随（R）"));

    connect(m_temperatureMode, &QToolButton::clicked, this,
            [this] { setMode(Mode::Temperature); });
    connect(m_rawMode, &QToolButton::clicked, this,
            [this] { setMode(Mode::Raw); });
    const auto seriesToggled = [this](const int index, const bool visible) {
        bool *states = m_mode == Mode::Temperature
            ? m_temperatureSeriesVisible : m_rawSeriesVisible;
        states[index] = visible;
        update();
    };
    connect(m_primarySeries, &QToolButton::toggled, this,
            [seriesToggled](const bool checked) { seriesToggled(0, checked); });
    connect(m_secondarySeries, &QToolButton::toggled, this,
            [seriesToggled](const bool checked) { seriesToggled(1, checked); });
    connect(m_tertiarySeries, &QToolButton::toggled, this,
            [seriesToggled](const bool checked) { seriesToggled(2, checked); });
    connect(m_follow, &QToolButton::toggled, this, &TrendWidget::setFollowing);
    connect(m_pause, &QToolButton::toggled, this, &TrendWidget::setPaused);
    connect(m_reset, &QToolButton::clicked, this, &TrendWidget::resetView);

    auto *pauseShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    pauseShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pauseShortcut, &QShortcut::activated, this,
            [this] { setPaused(!m_paused); });
    auto *followShortcut = new QShortcut(QKeySequence(Qt::Key_F), this);
    followShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(followShortcut, &QShortcut::activated, this,
            [this] { setFollowing(true); });
    auto *resetShortcut = new QShortcut(QKeySequence(Qt::Key_R), this);
    resetShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(resetShortcut, &QShortcut::activated, this, &TrendWidget::resetView);

    m_redrawTimer.setSingleShot(true);
    m_redrawTimer.setInterval(100);
    connect(&m_redrawTimer, &QTimer::timeout, this, [this] { update(); });
    m_stateTimer.setInterval(500);
    connect(&m_stateTimer, &QTimer::timeout, this, [this] {
        if (!m_samples.isEmpty()) update();
    });
    m_stateTimer.start();
    updateSeriesControls();
}

void TrendWidget::addSample(const TrendSample &sample)
{
    updateTemperatureRange(sample);
    m_samples.append(sample);
    m_history.append(sample);
    const double retentionStart = sample.seconds - DisplayRetentionSeconds;
    int removeCount = 0;
    while (removeCount + 1 < m_samples.size()
           && m_samples.at(removeCount + 1).seconds < retentionStart) {
        ++removeCount;
    }
    if (removeCount > 0) m_samples.remove(0, removeCount);
    if (m_samples.size() > MaximumDisplaySamples) {
        m_samples.remove(0, m_samples.size() - MaximumDisplaySamples);
    }
    m_lastSampleAge.restart();
    if (m_following && !m_paused) m_frozenEndSeconds = sample.seconds;
    if (!m_redrawTimer.isActive()) m_redrawTimer.start();
}

void TrendWidget::clear()
{
    m_samples.clear();
    m_history.clear();
    m_redrawTimer.stop();
    m_lastSampleAge.invalidate();
    m_temperatureLow = 15.0;
    m_temperatureHigh = 40.0;
    m_frozenEndSeconds = 0.0;
    m_hovering = false;
    m_paused = false;
    m_following = true;
    {
        const QSignalBlocker pauseBlocker(m_pause);
        const QSignalBlocker followBlocker(m_follow);
        m_pause->setChecked(false);
        m_pause->setText(tr("暂停"));
        m_follow->setChecked(true);
    }
    update();
}

int TrendWidget::historySampleCount() const noexcept
{
    return m_history.size();
}

QImage TrendWidget::renderHistoryImage(const QSize &size) const
{
    const QSize bounded(std::max(size.width(), 800), std::max(size.height(), 520));
    QImage image(bounded, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    drawChart(painter, QRectF(QPointF(0, 0), QSizeF(bounded)), m_history,
              m_mode, true, property("darkMode").toBool());
    return image;
}

void TrendWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), property("darkMode").toBool()
                               ? QColor(QStringLiteral("#181e24"))
                               : QColor(QStringLiteral("#ffffff")));
    const QRectF chartBounds = QRectF(rect()).adjusted(0, ToolbarHeight, 0, 0);
    drawChart(painter, chartBounds, m_samples, m_mode, false,
              property("darkMode").toBool(),
              m_hovering ? &m_hoverPosition : nullptr);
}

void TrendWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_toolbar->setGeometry(0, 0, width(), ToolbarHeight);
    m_tertiarySeries->setVisible(width() >= 720);
    m_reset->setVisible(width() >= 520);
    m_secondarySeries->setVisible(width() >= 430);
}

void TrendWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_hoverPosition = event->position();
    m_hovering = event->position().y() >= ToolbarHeight;
    update();
    QWidget::mouseMoveEvent(event);
}

void TrendWidget::leaveEvent(QEvent *event)
{
    m_hovering = false;
    update();
    QWidget::leaveEvent(event);
}

void TrendWidget::setMode(const Mode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    m_hovering = false;
    updateSeriesControls();
    update();
}

void TrendWidget::setPaused(const bool paused)
{
    if (m_paused == paused) return;
    if (paused && !m_samples.isEmpty() && m_following) {
        m_frozenEndSeconds = m_samples.constLast().seconds;
    }
    m_paused = paused;
    const QSignalBlocker blocker(m_pause);
    m_pause->setChecked(paused);
    m_pause->setText(paused ? tr("继续") : tr("暂停"));
    if (!paused && m_following && !m_samples.isEmpty()) {
        m_frozenEndSeconds = m_samples.constLast().seconds;
    }
    update();
}

void TrendWidget::setFollowing(const bool following)
{
    if (m_following == following) return;
    if (!following && !m_samples.isEmpty()) {
        m_frozenEndSeconds = m_samples.constLast().seconds;
    }
    m_following = following;
    const QSignalBlocker blocker(m_follow);
    m_follow->setChecked(following);
    if (following && !m_paused && !m_samples.isEmpty()) {
        m_frozenEndSeconds = m_samples.constLast().seconds;
    }
    update();
}

void TrendWidget::resetView()
{
    m_temperatureLow = 15.0;
    m_temperatureHigh = 40.0;
    for (const TrendSample &sample : std::as_const(m_samples)) {
        updateTemperatureRange(sample);
    }
    setPaused(false);
    setFollowing(true);
    m_hovering = false;
    update();
}

void TrendWidget::updateSeriesControls()
{
    const bool temperature = m_mode == Mode::Temperature;
    {
        const QSignalBlocker temperatureBlocker(m_temperatureMode);
        const QSignalBlocker rawBlocker(m_rawMode);
        m_temperatureMode->setChecked(temperature);
        m_rawMode->setChecked(!temperature);
    }
    bool *states = temperature ? m_temperatureSeriesVisible : m_rawSeriesVisible;
    {
        const QSignalBlocker primaryBlocker(m_primarySeries);
        const QSignalBlocker secondaryBlocker(m_secondarySeries);
        const QSignalBlocker tertiaryBlocker(m_tertiarySeries);
        m_primarySeries->setText(temperature ? QStringLiteral("Center 5×5")
                                            : QStringLiteral("Center ADU"));
        m_secondarySeries->setText(temperature ? QStringLiteral("NTC")
                                              : QStringLiteral("VTEMP ADU"));
        m_tertiarySeries->setText(temperature ? tr("中心点")
                                             : tr("中心点 ADU"));
        m_primarySeries->setProperty("series", temperature
            ? QStringLiteral("center") : QStringLiteral("adu"));
        m_secondarySeries->setProperty("series", temperature
            ? QStringLiteral("ntc") : QStringLiteral("vtemp"));
        m_tertiarySeries->setProperty("series", temperature
            ? QStringLiteral("center") : QStringLiteral("adu"));
        m_primarySeries->setChecked(states[0]);
        m_secondarySeries->setChecked(states[1]);
        m_tertiarySeries->setChecked(states[2]);
        for (QToolButton *button : {m_primarySeries, m_secondarySeries,
                                    m_tertiarySeries}) {
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
    }
}

void TrendWidget::updateTemperatureRange(const TrendSample &sample)
{
    for (const double value : {sample.centerRoi, sample.ntc, sample.centerPoint}) {
        if (!std::isfinite(value)) continue;
        if (value < m_temperatureLow) {
            m_temperatureLow = std::floor(value / 10.0) * 10.0;
        }
        if (value > m_temperatureHigh) {
            m_temperatureHigh = std::ceil(value / 10.0) * 10.0;
        }
    }
}

QVector<TrendWidget::Series> TrendWidget::activeSeries(const bool dark) const
{
    QVector<Series> series;
    if (m_mode == Mode::Temperature) {
        if (m_temperatureSeriesVisible[0]) {
            series.append({&TrendSample::centerRoi, QStringLiteral("Center 5×5"),
                           QStringLiteral("°C"),
                           QColor(dark ? QStringLiteral("#65d6a3")
                                       : QStringLiteral("#2e9d71")),
                           Qt::SolidLine, 2.0});
        }
        if (m_temperatureSeriesVisible[1]) {
            series.append({&TrendSample::ntc, QStringLiteral("NTC"),
                           QStringLiteral("°C"),
                           QColor(dark ? QStringLiteral("#48c7d9")
                                       : QStringLiteral("#14839a")),
                           Qt::SolidLine, 1.5});
        }
        if (m_temperatureSeriesVisible[2]) {
            series.append({&TrendSample::centerPoint, tr("中心点"),
                           QStringLiteral("°C"),
                           QColor(dark ? QStringLiteral("#a2b0bb")
                                       : QStringLiteral("#7b8792")),
                           Qt::DashLine, 1.2});
        }
    } else {
        if (m_rawSeriesVisible[0]) {
            series.append({&TrendSample::centerRoiAdu, QStringLiteral("Center ADU"),
                           QStringLiteral("ADU"),
                           QColor(dark ? QStringLiteral("#f2b45f")
                                       : QStringLiteral("#d8872e")),
                           Qt::SolidLine, 2.0});
        }
        if (m_rawSeriesVisible[1]) {
            series.append({&TrendSample::vtempSmooth, QStringLiteral("VTEMP ADU"),
                           QStringLiteral("ADU"),
                           QColor(dark ? QStringLiteral("#b89af4")
                                       : QStringLiteral("#745dd6")),
                           Qt::SolidLine, 1.7});
        }
        if (m_rawSeriesVisible[2]) {
            series.append({&TrendSample::centerPointAdu, tr("中心点 ADU"),
                           QStringLiteral("ADU"),
                           QColor(dark ? QStringLiteral("#d6c08f")
                                       : QStringLiteral("#a56a24")),
                           Qt::DashLine, 1.2});
        }
    }
    return series;
}

QVector<TrendSample> TrendWidget::samplesForWindow(const QVector<TrendSample> &samples,
                                                   const double startSeconds,
                                                   const double endSeconds) const
{
    QVector<TrendSample> visible;
    if (samples.isEmpty()) return visible;
    const auto begin = std::lower_bound(samples.cbegin(), samples.cend(), startSeconds,
                                        [](const TrendSample &sample, const double seconds) {
                                            return sample.seconds < seconds;
                                        });
    const auto end = std::upper_bound(begin, samples.cend(), endSeconds,
                                      [](const double seconds, const TrendSample &sample) {
                                          return seconds < sample.seconds;
                                      });
    visible.reserve(int(std::distance(begin, end)));
    for (auto iterator = begin; iterator != end; ++iterator) visible.append(*iterator);
    return visible;
}

TrendWidget::AxisRange TrendWidget::axisRange(const QVector<TrendSample> &samples,
                                              const QVector<Series> &series) const
{
    if (m_mode == Mode::Temperature) {
        return {m_temperatureLow, m_temperatureHigh, temperatureAxisTick()};
    }
    double low = std::numeric_limits<double>::infinity();
    double high = -std::numeric_limits<double>::infinity();
    for (const TrendSample &sample : samples) {
        for (const Series &entry : series) {
            const double value = sample.*entry.member;
            if (!std::isfinite(value)) continue;
            low = std::min(low, value);
            high = std::max(high, value);
        }
    }
    if (!std::isfinite(low) || !std::isfinite(high)) return {0.0, 100.0, 20.0};
    if (std::abs(high - low) < 1e-9) {
        low -= 1.0;
        high += 1.0;
    }
    const double margin = std::max(1.0, (high - low) * 0.08);
    const double tick = niceStep((high - low + margin * 2.0) / 5.0);
    return {std::floor((low - margin) / tick) * tick,
            std::ceil((high + margin) / tick) * tick, tick};
}

void TrendWidget::drawChart(QPainter &painter, const QRectF &bounds,
                            const QVector<TrendSample> &samples, const Mode mode,
                            const bool fullHistory, const bool dark,
                            const QPointF *hoverPosition) const
{
    const QColor surface(dark ? QStringLiteral("#181e24")
                              : QStringLiteral("#ffffff"));
    const QColor plotSurface(dark ? QStringLiteral("#1d242a")
                                  : QStringLiteral("#fbfcfd"));
    const QColor textColor(dark ? QStringLiteral("#d8dde3")
                                : QStringLiteral("#1d1f23"));
    const QColor mutedColor(dark ? QStringLiteral("#95a2ae")
                                 : QStringLiteral("#7b838e"));
    const QColor gridColor(dark ? QStringLiteral("#303942")
                                : QStringLiteral("#e7eaee"));
    const QColor borderColor(dark ? QStringLiteral("#45515c")
                                  : QStringLiteral("#cfd4db"));
    painter.fillRect(bounds, surface);

    const QVector<Series> series = activeSeries(dark);
    double endSeconds = 0.0;
    double startSeconds = -DisplayWindowSeconds;
    if (!samples.isEmpty()) {
        if (fullHistory) {
            startSeconds = samples.constFirst().seconds;
            endSeconds = std::max(samples.constLast().seconds, startSeconds + 0.001);
        } else {
            endSeconds = (m_paused || !m_following)
                ? m_frozenEndSeconds : samples.constLast().seconds;
            startSeconds = endSeconds - DisplayWindowSeconds;
        }
    }
    const QVector<TrendSample> visible = samplesForWindow(samples, startSeconds, endSeconds);
    const AxisRange range = axisRange(visible, series);

    const QRectF area = bounds.adjusted(12, 7, -12, -8);
    QFont titleFont = painter.font();
    titleFont.setPointSizeF(ChartTitlePointSize);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(textColor);
    const QString title = mode == Mode::Temperature ? tr("温度趋势") : tr("原始量趋势");
    painter.drawText(QRectF(area.left(), area.top(), 74, 20),
                     Qt::AlignLeft | Qt::AlignVCenter, title);

    QFont legendFont = painter.font();
    legendFont.setPointSizeF(ChartTextPointSize);
    legendFont.setWeight(QFont::Medium);
    painter.setFont(legendFont);
    qreal legendX = area.left() + 78;
    const qreal legendLimit = area.right() - 104;
    for (const Series &entry : series) {
        const qreal labelWidth = QFontMetricsF(legendFont).horizontalAdvance(entry.name);
        if (legendX + 22 + labelWidth > legendLimit) break;
        painter.setPen(QPen(entry.color, entry.width, entry.style,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(legendX, area.top() + 10),
                         QPointF(legendX + 15, area.top() + 10));
        painter.setPen(textColor);
        painter.drawText(QRectF(legendX + 20, area.top(), labelWidth + 2, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, entry.name);
        legendX += labelWidth + 34;
    }

    QString stateText;
    QColor stateColor = mutedColor;
    if (samples.isEmpty()) {
        stateText = tr("等待首帧");
    } else if (m_paused && !fullHistory) {
        stateText = tr("视图已暂停");
        stateColor = QColor(dark ? QStringLiteral("#d7a85b")
                                 : QStringLiteral("#a46a00"));
    } else if (!fullHistory && m_lastSampleAge.isValid()
               && m_lastSampleAge.elapsed() > StaleThresholdMs) {
        stateText = tr("数据过期 %1 s").arg(m_lastSampleAge.elapsed() / 1000.0, 0, 'f', 1);
        stateColor = QColor(dark ? QStringLiteral("#d7a85b")
                                 : QStringLiteral("#a46a00"));
    } else if (!m_following && !fullHistory) {
        stateText = tr("视图已冻结");
    } else if (!fullHistory) {
        stateText = tr("跟随中");
    }
    painter.setPen(stateColor);
    painter.drawText(QRectF(area.right() - 102, area.top(), 102, 20),
                     Qt::AlignRight | Qt::AlignVCenter, stateText);

    const QRectF plot = area.adjusted(46, 26, -8, -28);
    if (plot.width() < 40.0 || plot.height() < 40.0) return;
    painter.fillRect(plot, plotSurface);

    QFont axisFont = painter.font();
    axisFont.setPointSizeF(ChartTextPointSize);
    axisFont.setWeight(QFont::Normal);
    painter.setFont(axisFont);
    const auto drawYTick = [&](const double value) {
        const qreal y = plot.bottom() - (value - range.low) * plot.height()
            / std::max(0.001, range.high - range.low);
        const bool zeroLine = std::abs(value) < range.tick * 0.01;
        painter.setPen(QPen(zeroLine ? borderColor : gridColor, zeroLine ? 1.2 : 1.0));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(mutedColor);
        painter.drawText(QRectF(area.left(), y - 9, 39, 18),
                         Qt::AlignRight | Qt::AlignVCenter,
                         axisText(value, mode == Mode::Raw));
    };
    const int regularTickCount = std::max(
        0, int(std::floor((range.high - range.low) / range.tick + 1e-9)));
    for (int step = 0; step <= regularTickCount; ++step) {
        drawYTick(range.low + step * range.tick);
    }
    const double lastRegularTick = range.low + regularTickCount * range.tick;
    if (lastRegularTick < range.high - range.tick * 0.01) {
        drawYTick(range.high);
    }
    for (int step = 0; step <= 6; ++step) {
        const qreal x = plot.left() + step * plot.width() / 6.0;
        painter.setPen(QPen(gridColor, 1));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const double seconds = startSeconds
            + step * (endSeconds - startSeconds) / 6.0;
        painter.setPen(mutedColor);
        painter.drawText(QRectF(x - 34, plot.bottom() + 5, 68, 18),
                         Qt::AlignHCenter | Qt::AlignTop,
                         relativeTimeText(seconds - endSeconds));
    }
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(plot);
    painter.setPen(mutedColor);
    painter.drawText(QRectF(area.left(), plot.top() - 20, 40, 18),
                     Qt::AlignRight | Qt::AlignVCenter,
                     mode == Mode::Temperature ? QStringLiteral("°C")
                                               : QStringLiteral("ADU"));

    if (visible.size() < 2 || series.isEmpty()) {
        painter.setPen(mutedColor);
        painter.drawText(plot, Qt::AlignCenter,
                         series.isEmpty() ? tr("请选择曲线") : tr("等待数据"));
        return;
    }

    const auto xFor = [&](const double seconds) {
        return plot.left() + (seconds - startSeconds) * plot.width()
            / std::max(0.001, endSeconds - startSeconds);
    };
    const auto yFor = [&](const double value) {
        return plot.bottom() - (value - range.low) * plot.height()
            / std::max(0.001, range.high - range.low);
    };

    int previousDecision = visible.constFirst().nucDecision;
    int previousCount = visible.constFirst().nucCount;
    int previousDg = visible.constFirst().nucDg;
    int previousDv = visible.constFirst().nucDv;
    int previousDn = visible.constFirst().nucDn;
    qreal lastEventLabelX = -1000.0;
    for (int index = 1; index < visible.size(); ++index) {
        const TrendSample &sample = visible.at(index);
        const bool changed = sample.nucDecision != previousDecision
            || sample.nucCount != previousCount || sample.nucDg != previousDg
            || sample.nucDv != previousDv || sample.nucDn != previousDn;
        previousDecision = sample.nucDecision;
        previousCount = sample.nucCount;
        previousDg = sample.nucDg;
        previousDv = sample.nucDv;
        previousDn = sample.nucDn;
        if (!changed || sample.nucDecision == 255) continue;
        const qreal x = xFor(sample.seconds);
        const QColor color = eventColor(sample.nucDecision, dark);
        painter.setPen(QPen(color, 1, Qt::DashLine));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        if (x - lastEventLabelX > 44.0) {
            painter.setPen(color);
            painter.drawText(QRectF(x + 3, plot.top() + 2, 64, 16),
                             Qt::AlignLeft | Qt::AlignTop,
                             nucLabel(sample.nucDecision));
            lastEventLabelX = x;
        }
    }

    struct Bucket {
        bool valid = false;
        int firstIndex = -1;
        int minimumIndex = -1;
        int maximumIndex = -1;
        int lastIndex = -1;
        double minimum = 0.0;
        double maximum = 0.0;
    };
    const bool downsample = visible.size() > std::max(1, qRound(plot.width() * 2.0));
    for (const Series &entry : series) {
        QPainterPath path;
        bool started = false;
        if (!downsample) {
            for (const TrendSample &sample : visible) {
                const double value = sample.*entry.member;
                if (!std::isfinite(value)) {
                    started = false;
                    continue;
                }
                const QPointF point(xFor(sample.seconds), yFor(value));
                if (started) path.lineTo(point); else path.moveTo(point);
                started = true;
            }
        } else {
            QVector<Bucket> buckets(std::max(1, qCeil(plot.width()) + 1));
            for (int index = 0; index < visible.size(); ++index) {
                const TrendSample &sample = visible.at(index);
                const double value = sample.*entry.member;
                if (!std::isfinite(value)) continue;
                const int column = std::clamp(int(std::floor(xFor(sample.seconds)
                                                              - plot.left())),
                                              0, int(buckets.size()) - 1);
                Bucket &bucket = buckets[column];
                if (!bucket.valid) {
                    bucket = {true, index, index, index, index, value, value};
                } else {
                    bucket.lastIndex = index;
                    if (value < bucket.minimum) {
                        bucket.minimum = value;
                        bucket.minimumIndex = index;
                    }
                    if (value > bucket.maximum) {
                        bucket.maximum = value;
                        bucket.maximumIndex = index;
                    }
                }
            }
            for (const Bucket &bucket : std::as_const(buckets)) {
                if (!bucket.valid) continue;
                QVector<int> ordered{bucket.firstIndex, bucket.minimumIndex,
                                     bucket.maximumIndex, bucket.lastIndex};
                std::sort(ordered.begin(), ordered.end());
                ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
                for (const int index : std::as_const(ordered)) {
                    const TrendSample &sample = visible.at(index);
                    const double value = sample.*entry.member;
                    const QPointF point(xFor(sample.seconds), yFor(value));
                    if (started) path.lineTo(point); else path.moveTo(point);
                    started = true;
                }
            }
        }
        painter.setPen(QPen(entry.color, entry.width, entry.style,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

    if (!hoverPosition || !plot.contains(*hoverPosition)) return;
    const double hoverSeconds = startSeconds
        + (hoverPosition->x() - plot.left()) * (endSeconds - startSeconds)
            / plot.width();
    const auto candidate = std::lower_bound(
        visible.cbegin(), visible.cend(), hoverSeconds,
        [](const TrendSample &sample, const double seconds) {
            return sample.seconds < seconds;
        });
    int hoverIndex = int(std::distance(visible.cbegin(), candidate));
    if (hoverIndex >= visible.size()) hoverIndex = visible.size() - 1;
    if (hoverIndex > 0
        && std::abs(visible.at(hoverIndex - 1).seconds - hoverSeconds)
            < std::abs(visible.at(hoverIndex).seconds - hoverSeconds)) {
        --hoverIndex;
    }
    const TrendSample &hoverSample = visible.at(hoverIndex);
    const qreal hoverX = xFor(hoverSample.seconds);
    painter.setPen(QPen(dark ? QColor(QStringLiteral("#7f8b96"))
                             : QColor(QStringLiteral("#949aa5")), 1, Qt::DashLine));
    painter.drawLine(QPointF(hoverX, plot.top()), QPointF(hoverX, plot.bottom()));

    QStringList tooltipLines{relativeTimeText(hoverSample.seconds - endSeconds)};
    for (const Series &entry : series) {
        const double value = hoverSample.*entry.member;
        if (!std::isfinite(value)) continue;
        const QPointF point(hoverX, yFor(value));
        painter.setPen(QPen(surface, 3.5));
        painter.setBrush(entry.color);
        painter.drawEllipse(point, 3.5, 3.5);
        tooltipLines.append(QStringLiteral("%1  %2 %3")
                                .arg(entry.name,
                                     axisText(value, mode == Mode::Raw), entry.unit));
    }
    QFont tooltipFont = painter.font();
    tooltipFont.setPointSizeF(ChartTextPointSize);
    painter.setFont(tooltipFont);
    const QFontMetricsF metrics(tooltipFont);
    qreal tooltipWidth = 0.0;
    for (const QString &line : std::as_const(tooltipLines)) {
        tooltipWidth = std::max(tooltipWidth, metrics.horizontalAdvance(line));
    }
    tooltipWidth += 18.0;
    const qreal tooltipHeight = tooltipLines.size() * 18.0 + 10.0;
    qreal tooltipX = hoverX + 10.0;
    if (tooltipX + tooltipWidth > plot.right() - 4.0) {
        tooltipX = hoverX - tooltipWidth - 10.0;
    }
    const qreal tooltipY = std::clamp(hoverPosition->y() - tooltipHeight / 2.0,
                                      plot.top() + 5.0,
                                      plot.bottom() - tooltipHeight - 5.0);
    const QRectF tooltipRect(tooltipX, tooltipY, tooltipWidth, tooltipHeight);
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(dark ? QColor(QStringLiteral("#252b31"))
                          : QColor(QStringLiteral("#ffffff")));
    painter.drawRoundedRect(tooltipRect, 4.0, 4.0);
    for (int index = 0; index < tooltipLines.size(); ++index) {
        painter.setPen(index == 0 ? mutedColor : textColor);
        painter.drawText(QRectF(tooltipRect.left() + 9,
                                tooltipRect.top() + 5 + index * 18,
                                tooltipRect.width() - 18, 18),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         tooltipLines.at(index));
    }
}

} // namespace serein::scope::tn160
