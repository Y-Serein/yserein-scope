#pragma once
#define NOMINMAX

#pragma comment(lib, "ws2_32.lib")
#include <WS2tcpip.h>
#include <MSWsock.h>
#include <process.h>
#include <QtCore/qloggingcategory.h>

#include <deque>
#include <thread>
#include <string>

Q_DECLARE_LOGGING_CATEGORY(NetUtilsLog)

namespace NetUtils {

	struct EXTENDED_OVERLAPPED
	{
		WSAOVERLAPPED overlapped{};
		WSABUF  wsaBuf{};
		sockaddr_in senderAddr{};
		int senderAddrLen = sizeof(sockaddr_in);
		DWORD flags = 0;
	};

	std::string getLastErrorMessage(DWORD last_error, bool stripTrailingLineFeed = true);

	std::string lastErroMessage();

	std::string lastErroMessage(const DWORD lastError);

	bool initWSA();

	SOCKET createSocket(const DWORD flags = 0);

	bool bind(SOCKET &socket, const USHORT port);

	bool bind(SOCKET &socket, const ULONG ip, const USHORT port);

	HANDLE createIOCP(SOCKET &socket);

	bool postIOCPRecvs(SOCKET& socket, const DWORD recvBufferSize, const DWORD pendingRecvs);

} // WinNetUtils