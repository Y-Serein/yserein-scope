#include "LinkManager.h"
#include "UDPWinLink.h"
#include "ZSMLoggingCategory.h"

#include <QtCore/qapplicationstatic.h>

Q_APPLICATION_STATIC(LinkManager, _linkManagerInstance);
ZSM_LOGGING_CATEGORY(LinkManagerLog, "Backend.Comms.LinkManager")

LinkManager::LinkManager(QObject* parent)
	: QObject(parent)
{
	zInfo(LinkManagerLog) << "create";
}

LinkManager::~LinkManager()
{
	zInfo(LinkManagerLog) << "destory";
}

LinkManager* LinkManager::instance()
{
	return _linkManagerInstance();
}

void LinkManager::init()
{
	_udpWinLink = new UDPWinLink(this);
	_udpWinLink->connect();
}

void LinkManager::stop()
{
	// UDPWinLink 持有独立的接收线程和发送 worker 线程，析构时会
	// 请求线程退出并限时等待。释放后置空以便下次 init 重建。
	if (_udpWinLink) {
		delete _udpWinLink;
		_udpWinLink = nullptr;
	}
}

