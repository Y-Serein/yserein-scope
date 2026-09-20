//
// Created by Hoppz on 25-10-10.
//
#pragma once

#include <QtCore/QObject>

#include "NetUtils.h"

struct SendContext
{
    WSAOVERLAPPED overlapped{};
    WSABUF  wsaBuf{};
    sockaddr_in targetAddr{};
    int senderAddrLen = sizeof(sockaddr_in);
    QByteArray payload;     ///< 确保不会被析构
};

class UDPWinSenderWorker : public QObject
{
    Q_OBJECT
public:
    explicit UDPWinSenderWorker(HANDLE hIOCP, QObject* parent = nullptr);
    ~UDPWinSenderWorker();

    void init();
    void stop();

    bool isRunning() const {return _isRunning.load(std::memory_order_acquire);}

    std::atomic<uint32_t> _successPackage;

public slots:
    void workLoop();

private:
    std::atomic<bool> _isRunning{false};

    HANDLE _hIOCP = NULL;
};




