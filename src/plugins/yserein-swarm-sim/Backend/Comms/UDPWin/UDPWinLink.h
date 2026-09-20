#pragma once

#include <QtCore/QObject>
#include <QVariant>
#include "UDPQtClient.h"
#include <vector>
#include <QThread>
#include <atomic>


class UDPWinReceiver;
class UDPWinSender;

class UDPWinLink : public QObject
{
	Q_OBJECT
public:
	explicit UDPWinLink(QObject *parent = nullptr);
	~UDPWinLink();

	void writeBytesThreadSafe(QByteArray& data);

	/// 由 MockVehicleWorker 调用
	void writeBytesThreadSafe(std::vector<QByteArray>& buffer);

	// void writeBytesThreadSafe(UDPWinSendQueue_PToken* pToken,const std::vector<QByteArray>& buffer);

	/// getter --------------------------------------------

	bool isConnected() const;
	/// create Token need
	// UDPWinSendQueuePtr sendMsgQueue() {return &_udpServer->_sendMsgQueue;}

	/// setter --------------------------------------------
	
	void connect();

signals:
	void bytesReceived(const QByteArray& data, const QVariant& host);

public slots:
	void _onDataReceived(const QByteArray& data, const SOCKADDR_IN& host);

// private slots:
	// void _report();

private:
	UDPWinReceiver* _receiver;
	QThread* _recvThread;

	UDPWinSender* _sender;
	// QThread* _sendThread;

	SOCKADDR_IN _defaultGCSAddress{};
};