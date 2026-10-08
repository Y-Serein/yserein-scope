#pragma once

#include <QtCore/QString>

class QFrame;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QVBoxLayout;
class QWidget;

namespace serein::scope::thermal {

struct WorkbenchSection {
    QFrame *frame = nullptr;
    QVBoxLayout *body = nullptr;
};

class ThermalWorkbenchUi final {
public:
    ThermalWorkbenchUi(QWidget *owner, QString objectPrefix);

    [[nodiscard]] QWidget *contextPanel() const noexcept;
    [[nodiscard]] QWidget *inspectorPanel() const noexcept;
    [[nodiscard]] QWidget *outputPanel() const noexcept;
    [[nodiscard]] QTabBar *outputTabs() const noexcept;
    [[nodiscard]] QStackedWidget *outputStack() const noexcept;

    WorkbenchSection addContextSection(const QString &objectName,
                                       const QString &title,
                                       bool showTitle = true);
    WorkbenchSection addInspectorSection(const QString &objectName,
                                         const QString &title);
    QFrame *addMetric(WorkbenchSection section, const QString &name,
                      QLabel **value, bool primary = false);
    int addOutputPage(const QString &title, QWidget *page);
    void addContextStretch();
    void addInspectorStretch();

private:
    WorkbenchSection addSection(QWidget *body, QVBoxLayout *layout,
                                const QString &objectName,
                                const QString &title, bool showTitle);

    QScrollArea *m_context = nullptr;
    QWidget *m_contextBody = nullptr;
    QVBoxLayout *m_contextLayout = nullptr;
    QScrollArea *m_inspector = nullptr;
    QWidget *m_inspectorBody = nullptr;
    QVBoxLayout *m_inspectorLayout = nullptr;
    QWidget *m_output = nullptr;
    QTabBar *m_outputTabs = nullptr;
    QStackedWidget *m_outputStack = nullptr;
};

} // namespace serein::scope::thermal
