#include <ThermalWorkbenchUi.h>

#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabBar>
#include <QtWidgets/QWidget>

#include <iostream>

using serein::scope::thermal::ThermalWorkbenchUi;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QWidget owner;
    ThermalWorkbenchUi ui(&owner, QStringLiteral("testThermal"));
    auto context = ui.addContextSection(QStringLiteral("connection"),
                                        QStringLiteral("设备与连接"));
    auto inspector = ui.addInspectorSection(QStringLiteral("metrics"),
                                            QStringLiteral("实时数据"));
    QLabel *value = nullptr;
    ui.addMetric(inspector, QStringLiteral("温度"), &value, true);
    auto *trend = new QWidget(ui.outputStack());
    auto *result = new QWidget(ui.outputStack());
    ui.addOutputPage(QStringLiteral("实时趋势"), trend);
    ui.addOutputPage(QStringLiteral("标定结果"), result);
    ui.addContextStretch();
    ui.addInspectorStretch();

    const bool valid = context.frame && context.body && value
        && qobject_cast<QScrollArea *>(ui.contextPanel())
        && qobject_cast<QScrollArea *>(ui.inspectorPanel())
        && ui.outputTabs()->count() == 2
        && ui.outputStack()->count() == 2
        && ui.outputStack()->currentWidget() == trend;
    if (!valid) {
        std::cerr << "thermal workbench shell structure failed\n";
        return 1;
    }
    ui.outputTabs()->setCurrentIndex(1);
    if (ui.outputStack()->currentWidget() != result) {
        std::cerr << "thermal workbench output tab binding failed\n";
        return 1;
    }
    return 0;
}
