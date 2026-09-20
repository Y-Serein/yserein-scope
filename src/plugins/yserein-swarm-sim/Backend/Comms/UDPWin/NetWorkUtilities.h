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

Q_DECLARE_LOGGING_CATEGORY(NerWockUtilitiesLog)

namespace NetConstant{
    constexpr unsigned short PORT = 37890;					/// 服务器的端口号
	constexpr DWORD NUM_IOCP_THREADS = 1;
	constexpr bool USE_LARGE_PAGES = false;
	constexpr const char* LOCAL_IP = "127.0.0.1";			/// 发送的服务器 ip
	
	constexpr DWORD EXPECTED_DATA_SIZE = 300;				/// 缓冲区的大小
	constexpr DWORD RECV_BUFFER_SIZE = EXPECTED_DATA_SIZE;	
	constexpr DWORD SEND_BUFFER_SIZE = EXPECTED_DATA_SIZE;
	constexpr DWORD IOCP_PENDING_RECVS = 5000;

	constexpr DWORD RIO_PENDING_RECVS = 150000;				/// 即允许同时进行的接收操作
	constexpr DWORD RIO_MAX_RESULTS = 1000;
	constexpr DWORD RIO_RESULTS_THRESHOLD = (RIO_MAX_RESULTS / 3) * 2;
	constexpr long DATAGRAMS_TO_SEND = 1000000;
	constexpr DWORD SPIN_COUNT = 4000;						/// 自旋计数，表示线程在进入 等待状态之前的自旋次数

	constexpr DWORD SEND_THREAD_NUM = 1;
	constexpr DWORD RECEIVE_THREAD_NUM = 1;

	///===============================================================================
	/// 结构体定义
	

	typedef std::deque<HANDLE> Threads;

#ifdef _MSC_VER
	// The Windows 8 Registered I/O (RIO) API is an MSVC/Windows-SDK-only
	// optimisation; MinGW's mswsock.h does not declare these types.
	typedef std::deque<RIO_BUFFERID> BufferList;

	struct EXTENDED_RIO_BUF : public RIO_BUF
	{
		DWORD operation;

		EXTENDED_RIO_BUF* pNext;
	};
#endif // _MSC_VER

	struct EXTENDED_OVERLAPPED : public OVERLAPPED
	{
		WSABUF buf;
	};

	//struct IOCP_OVERLAPPED {
	//	OVERLAPPED overlapped;
	//	WSABUF buffer;
	//	char data[BUFFER_SIZE];
	//	sockaddr_in clientAddr;
	//	int addrLen;
	//	SOCKET socket;
	//};

} // NetConstant

namespace NetWorkUtilities
{

	std::string GetLastErrorMessage(DWORD last_error, bool stripTrailingLineFeed = true);

	// 初始化 winsock
	void InitialiseWinsock();

	// 初始化计时器
	void SetupTiming();

	// 创建 Socket
	SOCKET CreateSocket(const DWORD flags = 0);

    void CloseSocket(SOCKET& socket);

	// 开启计时
	void StartTiming(LARGE_INTEGER& startCounter);

	// 关闭计时
	void StopTiming(LARGE_INTEGER& stopCounter);

	void PrintTiming(LARGE_INTEGER& startCounter, LARGE_INTEGER& stopCounter);

	// 用于计算 Vehicle 发送数据的流量
	void PrintTimingWithCounter(LARGE_INTEGER& startCounter, LARGE_INTEGER& stopCounter, long cnt);

	void Bind(SOCKET socket, const unsigned short port);

#ifdef _MSC_VER
	SOCKET CreateRIOSocket(RIO_EXTENSION_FUNCTION_TABLE &rio,const bool bind = true);

	void InitialiseRIO(SOCKET &socket, RIO_EXTENSION_FUNCTION_TABLE &rio);
#endif // _MSC_VER

	HANDLE CreateIOCP();

	void PostIOCPRecvs(const DWORD recvBufferSize, const DWORD pendingRecvs,const SOCKET &_socket);

	char* AllocateBufferSpace(const DWORD recvBufferSize, const DWORD pendingRecvs, DWORD& receiveBuffersAllocated);

	char* AllocateBufferSpace(const DWORD recvBufferSize, const DWORD pendingRecvs
							, DWORD& bufferSize, DWORD &receiveBuffersAllocated);

	void SetSocketBufferToMaximum(SOCKET& s, int optName, const QString& label);

	void EnableSocketBroadcast(SOCKET &s);

} // NetWorkUtilities
