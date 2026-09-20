#pragma once

#include <QObject>
#include <QByteArray>

#include <atomic>

#include "NetUtils.h"

class UDPWinLink;

class UDPWinReceiver : public QObject
{
    Q_OBJECT
public:
    explicit UDPWinReceiver(QObject *parent = nullptr);
    ~UDPWinReceiver() override;

    void init();
    void stop();
    bool sendData(const QByteArray& data, const SOCKADDR_IN& target);

    bool isRunning() const {return _isRunning.load(std::memory_order_acquire);}

public slots:
    void workLoop();

private:
    UDPWinLink* _link = nullptr;

    bool _initNoError = false;

    std::atomic<bool> _isRunning{false};

    SOCKET _socket = INVALID_SOCKET;
    HANDLE _hIOCP = NULL;

    static constexpr int _recvBufferSize = 10240;
    static constexpr int _iocpPendingRecvs = 20000;
};