#pragma once

#include <QtCore/QObject>

class UDPWinLink;

class LinkManager : public QObject
{
	Q_OBJECT
public:
	explicit LinkManager(QObject* parent = nullptr);
	~LinkManager();

	static LinkManager* instance();
	
	void init();

	/// getter ----------------------------------------
	UDPWinLink* udpWinLink() {return _udpWinLink;}


	/// setter ----------------------------------------



private:
	UDPWinLink* _udpWinLink = nullptr;
};