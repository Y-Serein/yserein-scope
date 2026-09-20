#include "ZSMLoggingCategory.h"
#include <QtCore/QSettings>
#include <sstream>

ZSMLoggingCategoryRegister* ZSMLoggingCategoryRegister::instance()
{
    // 线程安全的单例
    static ZSMLoggingCategoryRegister instance;
    return &instance;
}

QStringList ZSMLoggingCategoryRegister::registerCategories()
{
    _registeredCategories.sort();
    return _registeredCategories;
}

void ZSMLoggingCategoryRegister::setCategoryLogging(const QString& category, bool enabled)
{
    QSettings settings;

    settings.beginGroup(_filterRulesSettingsGroup);
    settings.setValue(category, enabled);

    
}

bool ZSMLoggingCategoryRegister::categoryLogging(const QString& category)
{
    QSettings settings;
    settings.beginGroup(_filterRulesSettingsGroup);

    return settings.value(category).toBool();
}

void ZSMLoggingCategoryRegister::setFilterRules()
{
    QString filterRules;
    QString filterRuleFormat("%1.debug=true\n");

    filterRules += "*Log.debug=false\n";
    filterRules += "Backend.*.debug=false\n";

    foreach(QString category, _registeredCategories) {
        if (categoryLogging(category)) {
            filterRules += filterRuleFormat.arg(category);
        }
    }

    qDebug() << "[Debug]" << "Filter Rules:" << filterRules;
    QLoggingCategory::setFilterRules(filterRules);
}

std::string ZSMLoggingCategoryRegister::thread_id_to_string(const::std::thread::id& id)
{
    ::std::stringstream ss;
    ss << id;
    return ss.str();
}
