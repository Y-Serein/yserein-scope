#ifndef ZSMSCOPEDTIMER_H
#define ZSMSCOPEDTIMER_H

#include <chrono>
#include <qDebug>
#include <QElapsedTimer>

#define ZDS_RUNTIME_UNIQUE_NAME(base, line) base##line
#define ZDS_RUNTIME_VAR(base, line) ZDS_RUNTIME_UNIQUE_NAME(base, line)

#define ZSM_SCOPEDTIME_BEGIN \
    ZSMScopedTimer ZDS_RUNTIME_VAR(_zds_timer_, __func__)(__func__);

#define ZSM_SCOPEDTIME_END \
	ZDS_RUNTIME_VAR(_zds_timer_, __func__).end();

class ZSMScopedTimer {
public:
	explicit ZSMScopedTimer(const char* funcName);
	~ZSMScopedTimer();

	void start();
	void end();

private:
	std::chrono::high_resolution_clock::time_point _start;
	QElapsedTimer _qtimer;
	const char* _funcName;
	bool _ended{ false };
};


#endif
