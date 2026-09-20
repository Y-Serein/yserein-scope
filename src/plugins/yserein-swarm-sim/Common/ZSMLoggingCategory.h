#pragma once

#include <QtCore/QObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QStringList>
#include <thread>
#include <qthread.h>

// 对 Q_LOGGING_CATEGORY 再封装一次, 方便我们对其进行管理
#define ZSM_LOGGING_CATEGORY(name, ...) \
    static ZSMLoggingCategory ZSMCategory ## name(__VA_ARGS__); \
    Q_LOGGING_CATEGORY(name, __VA_ARGS__)

// 输出函数名称
#ifndef __infofunc__
#define __infofunc__     QStringLiteral("\t\t[Info][%1][%2]").arg(__func__).arg(QThread::currentThread()->objectName())
#endif

#ifndef __debugfunc__
#define __debugfunc__   QStringLiteral("\t\t[Debug][%1][%2]").arg(__func__).arg(QThread::currentThread()->objectName())
#endif

#ifndef __warningfunc__
#define __warningfunc__   QStringLiteral("\t\t[Warning][%1:%2][%3]").arg(__func__).arg(__LINE__).arg(QThread::currentThread()->objectName())
#endif

#ifndef __criticalfunc__
#define __criticalfunc__ QStringLiteral("\t\t[Critical][%1:%2][%3]").arg(__func__).arg(__LINE__).arg(QThread::currentThread()->objectName())
#endif

#ifndef qTest
#define qTest() qDebug().noquote()  << QStringLiteral("[Test][%1][%2]").arg(Q_FUNC_INFO).arg(QThread::currentThread()->objectName())
#endif

#ifndef zTest
#define zTest() qTest()
#endif

#ifndef zDebug
#define zDebug(name) qCDebug(name).noquote() << __debugfunc__ 
#endif

#ifndef zInfo
#define zInfo(name) qCInfo(name).noquote()  << __infofunc__ 
#endif

#ifndef zWarning
#define zWarning(name) qCWarning(name).noquote()  << __warningfunc__ 
#endif

#ifndef zCritical
#define zCritical(name) qCCritical(name).noquote() << __criticalfunc__
#endif

// TODO
// qZWarning(Log) => qCWarning(Log) << __warningfunc__  就不用每次后面还要跟一个

class ZSMLoggingCategoryRegister : public QObject
{
    Q_OBJECT
public:

    /// @brief 单例工厂 
    static ZSMLoggingCategoryRegister* instance();

    /// @brief 注册日志类型
    /// @param category 日志的名称（之后用于筛选，打印）
    void registerCategory(const char* category) { _registeredCategories << category; }

    /// @brief 返回所有注册的日志类型
    Q_INVOKABLE QStringList registerCategories();

    /// @brief 开启 / 关闭 category 的日志记录, 存储在 Settings 中
    /// @param category 日志类型的名称
    /// @param enabled  true : 开启记录 / false : 关闭记录
    Q_INVOKABLE void setCategoryLogging(const QString& category, bool enabled);

    /// @brief 是否开启了日志记录
    /// @return true : 开启 / false : 未开启
    Q_INVOKABLE bool categoryLogging(const QString& category);

    /// @brief 配置全局日志过滤
    void setFilterRules();

    static std::string thread_id_to_string(const ::std::thread::id& id);

private:
    ZSMLoggingCategoryRegister() {}            ///> 不允许外部创建, 只能通过 instance 创建

    QStringList _registeredCategories;          ///> 存储已注册的所有日志类型
    static constexpr const char* _filterRulesSettingsGroup = "LoggingFilters";  ///> 在日志中存储 Logging settings 的 group
};

class ZSMLoggingCategory
{
public:
    ZSMLoggingCategory(const char* category) { ZSMLoggingCategoryRegister::instance()->registerCategory(category); }
};
