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

	/// @brief 停掉并释放 UDP 收发链路（含接收线程和发送 worker 线程）。
	/// 必须在 backend 线程内，且无其他线程持有 UDPWinLink 引用时调用。
	void stop();

	/// getter ----------------------------------------
	UDPWinLink* udpWinLink() {return _udpWinLink;}


	/// setter ----------------------------------------



private:
	UDPWinLink* _udpWinLink = nullptr;
};