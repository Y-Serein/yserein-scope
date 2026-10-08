#include "ThermalWorkbenchUi.h"

#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace serein::scope::thermal {
namespace {
QScrollArea *makeScroll(QWidget *owner, const QString &name,
                        QWidget **body, QVBoxLayout **layout,
                        const QSizePolicy::Policy horizontalPolicy)
{
    auto *scroll = new QScrollArea(owner);
    scroll->setObjectName(name);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(0);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    *body = new QWidget(scroll);
    (*body)->setObjectName(name + QStringLiteral("Body"));
    (*body)->setMinimumWidth(0);
    (*body)->setSizePolicy(horizontalPolicy, QSizePolicy::Preferred);
    *layout = new QVBoxLayout(*body);
    (*layout)->setContentsMargins(12, 10, 12, 12);
    (*layout)->setSpacing(8);
    scroll->setWidget(*body);
    return scroll;
}
} // namespace

ThermalWorkbenchUi::ThermalWorkbenchUi(QWidget *owner, QString objectPrefix)
{
    m_context = makeScroll(owner, objectPrefix + QStringLiteral("UnifiedContext"),
                           &m_contextBody, &m_contextLayout,
                           QSizePolicy::Ignored);
    m_inspector = makeScroll(owner,
                             objectPrefix + QStringLiteral("UnifiedInspector"),
                             &m_inspectorBody, &m_inspectorLayout,
                             QSizePolicy::Preferred);
    m_output = new QWidget(owner);
    m_output->setObjectName(objectPrefix + QStringLiteral("UnifiedOutput"));
    auto *layout = new QVBoxLayout(m_output);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    m_outputTabs = new QTabBar(m_output);
    m_outputTabs->setObjectName(objectPrefix + QStringLiteral("UnifiedOutputTabs"));
    m_outputTabs->setDrawBase(false);
    m_outputTabs->setExpanding(false);
    layout->addWidget(m_outputTabs);
    m_outputStack = new QStackedWidget(m_output);
    m_outputStack->setObjectName(objectPrefix + QStringLiteral("UnifiedOutputStack"));
    m_outputStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    layout->addWidget(m_outputStack, 1);
    QObject::connect(m_outputTabs, &QTabBar::currentChanged,
                     m_outputStack, &QStackedWidget::setCurrentIndex);
}

QWidget *ThermalWorkbenchUi::contextPanel() const noexcept { return m_context; }
QWidget *ThermalWorkbenchUi::inspectorPanel() const noexcept { return m_inspector; }
QWidget *ThermalWorkbenchUi::outputPanel() const noexcept { return m_output; }
QTabBar *ThermalWorkbenchUi::outputTabs() const noexcept { return m_outputTabs; }
QStackedWidget *ThermalWorkbenchUi::outputStack() const noexcept { return m_outputStack; }

WorkbenchSection ThermalWorkbenchUi::addSection(
    QWidget *body, QVBoxLayout *layout, const QString &objectName,
    const QString &title, const bool showTitle)
{
    auto *frame = new QFrame(body);
    frame->setObjectName(objectName);
    frame->setMinimumWidth(0);
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *sectionLayout = new QVBoxLayout(frame);
    sectionLayout->setContentsMargins(8, 8, 8, 8);
    sectionLayout->setSpacing(6);
    if (showTitle) {
        auto *label = new QLabel(title, frame);
        label->setObjectName(QStringLiteral("metricGroupLabel"));
        sectionLayout->addWidget(label);
    }
    layout->addWidget(frame);
    return {frame, sectionLayout};
}

WorkbenchSection ThermalWorkbenchUi::addContextSection(
    const QString &objectName, const QString &title, const bool showTitle)
{
    return addSection(m_contextBody, m_contextLayout, objectName, title,
                      showTitle);
}

WorkbenchSection ThermalWorkbenchUi::addInspectorSection(
    const QString &objectName, const QString &title)
{
    return addSection(m_inspectorBody, m_inspectorLayout, objectName, title,
                      true);
}

QFrame *ThermalWorkbenchUi::addMetric(WorkbenchSection section,
                                      const QString &name, QLabel **value,
                                      const bool primary)
{
    auto *row = new QFrame(section.frame);
    row->setObjectName(primary ? QStringLiteral("metricPrimary")
                               : QStringLiteral("metricRow"));
    row->setFixedHeight(primary ? 38 : 28);
    row->setMinimumWidth(0);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(4, 0, 4, 0);
    auto *title = new QLabel(name, row);
    title->setObjectName(primary ? QStringLiteral("metricPrimaryLabel")
                                 : QStringLiteral("metricLabel"));
    title->setMinimumWidth(0);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    *value = new QLabel(QStringLiteral("—"), row);
    (*value)->setObjectName(primary ? QStringLiteral("metricPrimaryValue")
                                    : QStringLiteral("metricValue"));
    (*value)->setMinimumWidth(0);
    (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    (*value)->setTextInteractionFlags(Qt::TextSelectableByMouse
                                      | Qt::TextSelectableByKeyboard);
    layout->addWidget(title, 1);
    layout->addWidget(*value);
    section.body->addWidget(row);
    return row;
}

int ThermalWorkbenchUi::addOutputPage(const QString &title, QWidget *page)
{
    const int index = m_outputStack->addWidget(page);
    m_outputTabs->addTab(title);
    if (m_outputTabs->count() == 1) {
        m_outputTabs->setCurrentIndex(0);
        m_outputStack->setCurrentIndex(0);
    }
    return index;
}

void ThermalWorkbenchUi::addContextStretch() { m_contextLayout->addStretch(1); }
void ThermalWorkbenchUi::addInspectorStretch() { m_inspectorLayout->addStretch(1); }

} // namespace serein::scope::thermal
