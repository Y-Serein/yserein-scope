#include "ZSMScopedTimer.h"

ZSMScopedTimer::ZSMScopedTimer(const char* funcName)
	: _funcName(funcName)
{	
	start();
}

ZSMScopedTimer::~ZSMScopedTimer()
{
	if (!_ended) end();
}

void ZSMScopedTimer::start()
{
	_start = std::chrono::high_resolution_clock::now();
	_qtimer.start();
}

void ZSMScopedTimer::end()
{
	_ended = true;
	auto _end = std::chrono::high_resolution_clock::now();
	qint64 elapsedNs = _qtimer.nsecsElapsed();
	qint64 elaspedUs = elapsedNs / 1000;
	double  elaspedMs = elapsedNs / 1000000.0;


	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(_end - _start).count();

	qDebug().noquote() << QStringLiteral("[%1][chrono]").arg(_funcName) << "ns:"<< duration << "mcs:" << duration / 1000.0 << "mls:" << duration / 1000000.0;
	qDebug().noquote() << QStringLiteral("[%1][Qt]").arg(_funcName) << "ns:" << elapsedNs << "mcs:" << elaspedUs << "mls:" << elaspedMs;
}