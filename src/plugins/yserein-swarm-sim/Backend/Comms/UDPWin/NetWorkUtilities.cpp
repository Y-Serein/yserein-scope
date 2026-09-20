#include "NetWorkUtilities.h"
#include "ZSMLoggingCategory.h"

ZSM_LOGGING_CATEGORY(NerWockUtilitiesLog, "Backend.Comms.NetWorkUtilites");

//LARGE_INTEGER NetWorkUtilities::g_frequency = { 0 };

namespace NetWorkUtilities {

	LARGE_INTEGER g_frequency;

	template <typename TV, typename TM>
	inline TV RoundDown(TV Value, TM Multiple)
	{
		return((Value / Multiple) * Multiple);
	}

	template <typename TV, typename TM>
	inline TV RoundUp(TV Value, TM Multiple)
	{
		return(RoundDown(Value, Multiple) + (((Value % Multiple) > 0) ? Multiple : 0));
	}

	std::string GetLastErrorMessage(DWORD last_error, bool stripTrailingLineFeed)
	{
		CHAR errmsg[512];

		if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			0,
			last_error,
			0,
			errmsg,
			511,
			NULL))
		{
			// if we fail, call ourself to find out why and return that error

			const DWORD thisError = ::GetLastError();

			if (thisError != last_error)
			{
				return GetLastErrorMessage(thisError, stripTrailingLineFeed);
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

	void InitialiseWinsock()
	{
		zInfo(NerWockUtilitiesLog) << "start";

		WSADATA data;

		WORD wVersionRequested = 0x202;

		// 初始化 Winsock 库
		if (0 != ::WSAStartup(wVersionRequested, &data))
		{
			zWarning(NerWockUtilitiesLog) << "WSAStartup error";
			return ;
		}

		// 检查返回的版本是否是 2.2
		if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {

			WSACleanup();
			zWarning(NerWockUtilitiesLog) << "WCould not find a usable version of Winsock.dll";
		}

		if (NetConstant::USE_LARGE_PAGES)
		{
			// check that we have SeLockMemoryPrivilege and enable it
			zWarning(NerWockUtilitiesLog) << "TODO - USE_LARGE_PAGES";
		}
	}

	void SetupTiming()
	{
		if (!::QueryPerformanceFrequency(&g_frequency))
		{
			zWarning(NerWockUtilitiesLog) << "QueryPerformanceFrequency error";
		}
	}

	SOCKET CreateSocket(const DWORD flags)
	{
		SOCKET socket = ::WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, flags);

		if (socket == INVALID_SOCKET) {
			const DWORD thisError = ::GetLastError();
			zWarning(NerWockUtilitiesLog) << "WSASocket create failed:" << GetLastErrorMessage(thisError);
			WSACleanup();
		}

		return socket;
	}

    void CloseSocket(SOCKET& socket)
	{
        if( socket != INVALID_SOCKET){
            shutdown(socket, SD_BOTH); // 取消阻塞操作
            if (SOCKET_ERROR == closesocket(socket)) {
                const DWORD thisError = ::GetLastError();
                zWarning(NerWockUtilitiesLog) << "closesocket failed with error:" << GetLastErrorMessage(thisError);
                WSACleanup();
            }
            socket = INVALID_SOCKET; // 标记套接字为无效
        }


        // if (SOCKET_ERROR == closesocket(socket)) {
        //     const DWORD thisError = ::GetLastError();
        //     zWarning(NerWockUtilitiesLog) << "closesocket failed with error:" << GetLastErrorMessage(thisError);
        //     WSACleanup();
        // }
	}

	void StartTiming(LARGE_INTEGER& startCounter)
	{
		if (!::QueryPerformanceCounter(&startCounter)) {
			zWarning(NerWockUtilitiesLog) << "QueryPerformanceCounter error";
		}
		zInfo(NerWockUtilitiesLog) << "start count";
	}

	void StopTiming(LARGE_INTEGER& stopCounter)
	{
		if (!::QueryPerformanceCounter(&stopCounter)) {
			zWarning(NerWockUtilitiesLog) << "QueryPerformanceCounter error";
		}
		zInfo(NerWockUtilitiesLog) << "stop count";
	}

	void PrintTiming(LARGE_INTEGER& startCounter, LARGE_INTEGER& stopCounter)
	{
		LARGE_INTEGER elapsed;
		elapsed.QuadPart = (stopCounter.QuadPart - startCounter.QuadPart) / (g_frequency.QuadPart / 1000);

		zInfo(NerWockUtilitiesLog) << "Complete in " << elapsed.QuadPart << "ms";

		if (elapsed.QuadPart != 0)
		{
			long g_packets = NetConstant::DATAGRAMS_TO_SEND;				// 发送了多少数据
			const double perSec = g_packets / elapsed.QuadPart * 1000.00;

			zInfo(NerWockUtilitiesLog) << perSec << " datagrams per second";
			zInfo(NerWockUtilitiesLog) << perSec << "KB/s";
			zInfo(NerWockUtilitiesLog) << perSec / 1024 << "MB/s";
		}
	}

    void PrintTimingWithCounter(LARGE_INTEGER &startCounter, LARGE_INTEGER &stopCounter, long cnt)
    {
		LARGE_INTEGER elapsed;
		elapsed.QuadPart = (stopCounter.QuadPart - startCounter.QuadPart) / (g_frequency.QuadPart / 1000);

		zInfo(NerWockUtilitiesLog) << "Complete in " << elapsed.QuadPart << "ms";
		zInfo(NerWockUtilitiesLog) << "total Byte: " << cnt;

		if (elapsed.QuadPart != 0)
		{
			
			const double perSec = (cnt) / elapsed.QuadPart * 1000.00;

			zInfo(NerWockUtilitiesLog) << perSec << " datagrams per second";
			zInfo(NerWockUtilitiesLog) << perSec << "Byte/s";
			zInfo(NerWockUtilitiesLog) << perSec / 1024 << "KB/s";
			zInfo(NerWockUtilitiesLog) << perSec / 1024 / 1024 << "MB/s";
		}

    }

	
    void Bind(SOCKET socket, const unsigned short port)
    {
		sockaddr_in addr;

		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = INADDR_ANY;

		if (SOCKET_ERROR == ::bind(socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)))
		{
			const DWORD thisError = ::GetLastError();
			zWarning(NerWockUtilitiesLog) << "Bind failed!" << GetLastErrorMessage(thisError);
			CloseSocket(socket);
			WSACleanup();
		}
	}

#ifdef _MSC_VER
	SOCKET CreateRIOSocket(RIO_EXTENSION_FUNCTION_TABLE& rio, const bool bind)
	{
		SOCKET socket = CreateSocket(WSA_FLAG_REGISTERED_IO);

		if (bind)
		{
			Bind(socket, NetConstant::PORT);
		}

		InitialiseRIO(socket,rio);
		return socket;
	}

	void InitialiseRIO(SOCKET& socket, RIO_EXTENSION_FUNCTION_TABLE& rio)
	{
		GUID functionTableId = WSAID_MULTIPLE_RIO;

		DWORD dwBytes = 0;

		bool ok = true;

		if (0 != WSAIoctl(
			socket,
			SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
			&functionTableId,
			sizeof(GUID),
			(void**)&rio,
			sizeof(rio),
			&dwBytes,
			0,
			0))
		{
			zWarning(NerWockUtilitiesLog) << "WSAIoctl";
		}
	}
#endif // _MSC_VER

	HANDLE CreateIOCP()
	{
		HANDLE hIOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);

		if (0 == hIOCP)
		{
			zWarning(NerWockUtilitiesLog) << "CreateIoCompletionPort";
		}

		return hIOCP;
	}

	void PostIOCPRecvs(const DWORD recvBufferSize, const DWORD pendingRecvs, const SOCKET& _socket)
	{
		DWORD totalBuffersAllocated = 0;

		while (totalBuffersAllocated < pendingRecvs)
		{
			DWORD receiveBuffersAllocated = 0;
			char* pBuffer = AllocateBufferSpace(recvBufferSize, pendingRecvs, receiveBuffersAllocated);
			totalBuffersAllocated += receiveBuffersAllocated;
			DWORD offset = 0;

			const DWORD recvFlags = 0;
			NetConstant::EXTENDED_OVERLAPPED* pBufs = new NetConstant::EXTENDED_OVERLAPPED[receiveBuffersAllocated];

			DWORD bytesRecvd = 0;
			DWORD flags = 0;

			for (DWORD i = 0; i < receiveBuffersAllocated; ++i)
			{
				NetConstant::EXTENDED_OVERLAPPED* pOverlapped = pBufs + i;
				memset(pOverlapped, 0, sizeof(NetConstant::EXTENDED_OVERLAPPED));

				pOverlapped->buf.buf = pBuffer + offset;
				pOverlapped->buf.len = recvBufferSize;
				offset += recvBufferSize;

				// Create sockaddr_in to hold sender address
				SOCKADDR_IN senderAddr;
				int senderLen = sizeof(senderAddr);

				// Use WSARecvFrom to get the sender address
				if (SOCKET_ERROR == ::WSARecvFrom(_socket, &(pOverlapped->buf), 1, &bytesRecvd, &flags,
					reinterpret_cast<SOCKADDR*>(&senderAddr), &senderLen,
					static_cast<OVERLAPPED*>(pOverlapped), 0))
				{
					const DWORD lastError = ::GetLastError();
					if (lastError != ERROR_IO_PENDING)
					{
						zWarning(NerWockUtilitiesLog) << "WSARecvFrom error" << lastError;
					}
				}
				else
				{
					// Successfully received data, now you can access senderAddr to get sender IP
					zDebug(NerWockUtilitiesLog) << "Received data from IP:"
						<< inet_ntoa(senderAddr.sin_addr)
						<< "Port:" << ntohs(senderAddr.sin_port);
				}
			}

			if (totalBuffersAllocated != pendingRecvs)
			{
				zDebug(NerWockUtilitiesLog) << pendingRecvs << " receives pending";
			}
		}

		zDebug(NerWockUtilitiesLog) << totalBuffersAllocated << " total receives pending";
	}


	char* AllocateBufferSpace(const DWORD recvBufferSize, const DWORD pendingRecvs, DWORD& receiveBuffersAllocated)
	{
		DWORD notUsed;

		return AllocateBufferSpace(recvBufferSize, pendingRecvs, notUsed, receiveBuffersAllocated);
	}

	char* AllocateBufferSpace(const DWORD recvBufferSize, const DWORD pendingRecvs, DWORD& bufferSize, DWORD& receiveBuffersAllocated)
	{
		const DWORD preferredNumaNode = 0;

		const SIZE_T largePageMinimum = NetConstant::USE_LARGE_PAGES ? ::GetLargePageMinimum() : 0;

		SYSTEM_INFO systemInfo;

		::GetSystemInfo(&systemInfo);

		systemInfo.dwAllocationGranularity;

		const unsigned __int64 granularity = (largePageMinimum == 0 ? systemInfo.dwAllocationGranularity : largePageMinimum);

		const unsigned __int64 desiredSize = recvBufferSize * pendingRecvs;

		unsigned __int64 actualSize = RoundUp(desiredSize, granularity);

		if (actualSize > std::numeric_limits<DWORD>::max())
		{
			actualSize = (std::numeric_limits<DWORD>::max() / granularity) * granularity;
		}

		receiveBuffersAllocated = std::min<DWORD>(pendingRecvs, static_cast<DWORD>(actualSize / recvBufferSize));

		bufferSize = static_cast<DWORD>(actualSize);

		char* pBuffer = reinterpret_cast<char*>(VirtualAllocExNuma(GetCurrentProcess(), 0, bufferSize, MEM_COMMIT | MEM_RESERVE | (largePageMinimum != 0 ? MEM_LARGE_PAGES : 0), PAGE_READWRITE, preferredNumaNode));

		if (pBuffer == 0)
		{
			zWarning(NerWockUtilitiesLog) << "VirtualAlloc error";
		}

		return pBuffer;
	}

	void SetSocketBufferToMaximum(SOCKET& s, int optName, const QString& label)
	{
		int bufSize = 0;
		int optLen = sizeof(bufSize);

		if (::getsockopt(s, SOL_SOCKET, optName, reinterpret_cast<char*>(&bufSize), &optLen) == SOCKET_ERROR)
		{
			zWarning(NerWockUtilitiesLog) << "getsockopt error in get" << label << "bufsize";
		}

		zInfo(NerWockUtilitiesLog) << label << " buf (before) = " << bufSize;

		bufSize = std::numeric_limits<int>::max();
		zInfo(NerWockUtilitiesLog) << "Try to set " << label << " buf to " << bufSize;

		if (::setsockopt(s, SOL_SOCKET, optName, reinterpret_cast<char*>(&bufSize), sizeof(bufSize)) == SOCKET_ERROR)
		{
			zWarning(NerWockUtilitiesLog) << "setsockopt error in set" << label << "bufsize";
		}

		if (::getsockopt(s, SOL_SOCKET, optName, reinterpret_cast<char*>(&bufSize), &optLen) == SOCKET_ERROR)
		{
			zWarning(NerWockUtilitiesLog) << "getsockopt error in verify" << label << "bufsize";
		}

		zInfo(NerWockUtilitiesLog) << label << " buf actually set to " << bufSize;
	}


	void EnableSocketBroadcast(SOCKET &s)
	{
		BOOL bEnableBroadcast = TRUE;
		if (::setsockopt(s, SOL_SOCKET, SO_BROADCAST,
			reinterpret_cast<const char*>(&bEnableBroadcast),
			sizeof(BOOL)) == SOCKET_ERROR)
		{
			zWarning(NerWockUtilitiesLog) << "setsockopt error: enable broadcast failed";
			return;
		}

		// 验证广播是否设置成功
		BOOL bCurrentBroadcast = FALSE;
		int optLen = sizeof(bCurrentBroadcast);
		if (::getsockopt(s, SOL_SOCKET, SO_BROADCAST,
			reinterpret_cast<char*>(&bCurrentBroadcast),
			&optLen) == SOCKET_ERROR)
		{
			zWarning(NerWockUtilitiesLog) << "getsockopt error: check broadcast status failed";
			return;
		}

		if (bCurrentBroadcast)
		{
			zInfo(NerWockUtilitiesLog) << "Broadcast successfully enabled on socket";
		}
		else
		{
			zWarning(NerWockUtilitiesLog) << "Broadcast setting not effective!";
		}
	}


}// NetWorkUtilities
