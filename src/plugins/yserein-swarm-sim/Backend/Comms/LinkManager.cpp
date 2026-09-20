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

