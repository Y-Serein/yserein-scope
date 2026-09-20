#include "NetUtils.h"
#include "ZSMLoggingCategory.h"
#include "NetWorkUtilities.h"

ZSM_LOGGING_CATEGORY(NetUtilsLog, "Backend.Comms.NetUtils")

namespace NetUtils {

std::string getLastErrorMessage(DWORD last_error, bool stripTrailingLineFeed)
{
	CHAR errmsg[512];

	if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		0,last_error,0,errmsg,511,NULL)){
		// if we fail, call ourself to find out why and return that error

		const DWORD thisError = ::GetLastError();
		if (thisError != last_error)
		{
			return getLastErrorMessage(thisError, stripTrailingLineFeed);
		}
		else
		{
			// But don't get into an infinite loop...

			return "Failed to obtain error string";
		}
	}

	if (stripTrailingLineFeed)
	{
		const size_t length = strlen(errmsg);
		if (errmsg[length - 1] == '\n')
		{
			errmsg[length - 1] = 0;
			if (errmsg[length - 2] == '\r')
			{
				errmsg[length - 2] = 0;
			}
		}
	}

	return errmsg;
}

std::string lastErroMessage()
{
	const DWORD lastError = ::GetLastError();

	return getLastErrorMessage(lastError);
}

std::string lastErroMessage(const DWORD lastError)
{
	return getLastErrorMessage(lastError);
}

bool initWSA()
{
	WSADATA data;
	WORD wVersionRequested = 0x202;
	const int rc = ::WSAStartup(wVersionRequested, &data);
	if (rc != 0)
	{
		zWarning(NetUtilsLog) << "WSAStartup failed, error:" << rc << getLastErrorMessage(rc, true);
		return false;
	}

	return true;
}

SOCKET createSocket(const DWORD flags)
{
	SOCKET socket = ::WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, flags);
	if (socket == INVALID_SOCKET) {
		const DWORD err = ::WSAGetLastError();
		zWarning(NetUtilsLog) << "createSocket failed, error:" << err << lastErroMessage(err);
		return INVALID_SOCKET;
	}
	return socket;
}

bool bind(SOCKET &socket, const USHORT port)
{
	// 配置复用
	// 设置 SO_REUSEADDR 允许端口重用
	BOOL reuse = TRUE;
	if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR)
	{
		zWarning(NetUtilsLog) << "setsockopt SO_REUSEADDR failed: " << WSAGetLastError();
	}

	// 启用广播
	BOOL broadcast = TRUE;
	if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast)) == SOCKET_ERROR)
	{
		zWarning(NetUtilsLog) << "setsockopt SO_BROADCAST failed: " << WSAGetLastError();
	}

	// 绑定
	sockaddr_in socketAddr;
	socketAddr.sin_family = AF_INET;
	socketAddr.sin_port = htons(port);
	socketAddr.sin_addr.s_addr = INADDR_ANY;

	if (SOCKET_ERROR == ::bind(socket, reinterpret_cast<struct sockaddr*>(&socketAddr), sizeof(socketAddr)))
	{
		const DWORD err = ::WSAGetLastError();
		zWarning(NetUtilsLog) << "bind port" << port << "failed, error:" << err << lastErroMessage(err);
		return false;
	}
	return true;
}

bool bind(SOCKET &socket, const ULONG ip, const USHORT port)
{
	// 配置复用
	// 设置 SO_REUSEADDR 允许端口重用
	BOOL reuse = TRUE;
	if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR)
	{
		zWarning(NetUtilsLog) << "setsockopt SO_REUSEADDR failed: " << WSAGetLastError();
	}

	// 启用广播
	BOOL broadcast = TRUE;
	if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast)) == SOCKET_ERROR)
	{
		zWarning(NetUtilsLog) << "setsockopt SO_BROADCAST failed: " << WSAGetLastError();
	}

	// 绑定
	sockaddr_in socketAddr;
	socketAddr.sin_family = AF_INET;
	socketAddr.sin_port = htons(port);
	socketAddr.sin_addr.s_addr = ip;

	if (SOCKET_ERROR == ::bind(socket, reinterpret_cast<struct sockaddr*>(&socketAddr), sizeof(socketAddr)))
	{
		const DWORD err = ::WSAGetLastError();
		zWarning(NetUtilsLog) << "bind ip" << ip << "port" << port << "failed, error:" << err << lastErroMessage(err);
		return false;
	}
	return true;
}


HANDLE createIOCP(SOCKET &socket)
{
	//* 1. 创建一个 IOCP

	//! numbterOfCurrentThreads 告诉 I/O 完成端口在同一时间最多能有多少线程处于可运行状态
	//! 如果为 0 IOCP 会使用默认值, 也就是允许并发执行的线程数量等于主机 CPU 数量
	HANDLE hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (0 == hIOCP)
	{
		zWarning(NetUtilsLog) << lastErroMessage();
		return 0;
	}

	//* 2. 把 IOCP 于一个设备关联

	// 设置为 1, 暂停的时候设置为 0
	if (0 == ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), hIOCP, 1, 0))
	{
		zWarning(NetUtilsLog) << lastErroMessage();
		return 0;
	}

	return hIOCP;
}


bool postIOCPRecvs(
	SOCKET &socket,
	const DWORD recvBufferSize,
	const DWORD pendingRecvs)
{
	DWORD totalBuffersAllocated = 0;

	while (totalBuffersAllocated < pendingRecvs)
	{
		// 申请大片内存
		DWORD receiveBuffersAllocated = 0;
		char* pBuffer = NetWorkUtilities::AllocateBufferSpace(recvBufferSize, pendingRecvs, receiveBuffersAllocated);
		
		totalBuffersAllocated += receiveBuffersAllocated;
		DWORD offset = 0;

		// 创建重叠 IO 结构体
		EXTENDED_OVERLAPPED* pBufs = new EXTENDED_OVERLAPPED[receiveBuffersAllocated];

		for (DWORD i = 0; i < receiveBuffersAllocated; ++i)
		{
			EXTENDED_OVERLAPPED* pOverlapped = pBufs + i;

			ZeroMemory(pOverlapped, sizeof(EXTENDED_OVERLAPPED));

			// 设置接收缓冲区
			pOverlapped->wsaBuf.buf = pBuffer + offset;
			pOverlapped->wsaBuf.len = recvBufferSize;
			offset += recvBufferSize;

			// 初始化其他字段
			pOverlapped->flags = 0;
			pOverlapped->senderAddrLen = sizeof(sockaddr_in);

			DWORD bytesRecvd = 0;

			// 投递 UDP 接收请求（带来源地址信息）
			if (SOCKET_ERROR == ::WSARecvFrom(
				socket,&(pOverlapped->wsaBuf), 1 , &bytesRecvd, &pOverlapped->flags,
				reinterpret_cast<sockaddr*>(&pOverlapped->senderAddr),
				&pOverlapped->senderAddrLen, &pOverlapped->overlapped, nullptr))
			{
				const DWORD lastError = ::WSAGetLastError();
				if (lastError != ERROR_IO_PENDING)
				{
					zWarning(NetUtilsLog) << "postIOCPRecvs WSARecvFrom failed, error:" << lastError << lastErroMessage(lastError);
					return false;
				}
			}
		}

		if (totalBuffersAllocated != pendingRecvs)
		{
			qDebug(NetUtilsLog) << __debugfunc__ << pendingRecvs << " receives pending";
		}
	}

	qDebug(NetUtilsLog) << __debugfunc__ << totalBuffersAllocated << " total receives pending";
	return true;
}


} // NetUtils