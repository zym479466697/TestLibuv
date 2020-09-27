// TestClient.cpp : 定义控制台应用程序的入口点。
//
#include "stdafx.h"
#include "TcpClient.h"
#include "Udp.h"
#include "Looper.h"
#include <map>


#pragma comment(lib, "libuv.lib")
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Userenv.lib")

void LogUv(int level, const char * file, int line, char *func, char* format, ...)
{
	va_list pArgList;
	va_start(pArgList, format);
	char szMessage[4000] = {0};
	_vsnprintf(szMessage, _countof(szMessage), format, pArgList);
	va_end(pArgList);

	SYSTEMTIME sys;
	GetLocalTime( &sys);
	char szTime[64] = {0};
	sprintf(szTime, "%d-%02d-%02d %02d:%02d:%02d.%03d", 
		sys.wYear,
		sys.wMonth,
		sys.wDay,
		sys.wHour,
		sys.wMinute,
		sys.wSecond,
		sys.wMilliseconds);

	char logbufw[4096] = {0};
	if(level == UV_LOG_LEVEL_ERROR) {
		_snprintf_s(logbufw, _countof(logbufw), _TRUNCATE, "[%06d][%s][ERROR]%s\n", GetCurrentThreadId(), szTime, szMessage);
	}
	else {
		_snprintf_s(logbufw, _countof(logbufw), _TRUNCATE, "[%06d][%s][INFO]%s\n", GetCurrentThreadId(), szTime, szMessage);
	}
	//写入文件
	//OutputDebugStringA(logbufw);
	std::string str = logbufw;
	printf("%s", logbufw);
}


std::string PacketData(int clientId, const char* data, int dataSize)
{
	uint32_t pack_size = dataSize + NET_PACKAGE_HEADLEN;
	NetPacket netPacket;
	netPacket.flag = DEF_PACK_HEAD_FLAG;
	netPacket.type = clientId;
	netPacket.dataSize = dataSize;

	std::string buffer;
	buffer.resize(pack_size);
	memcpy(&(buffer[0]), (const char*)&netPacket, 12);
	memcpy(&(buffer[12]), data, dataSize);
	return buffer;
}

void SetConsoleColor(unsigned short ForeColor=0, unsigned short BackGroundColor=0)
{
	HANDLE hCon=GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hCon,ForeColor|BackGroundColor);
}

int call_time = 0;
void TestTcpClientFunc()
{
	const int clientsize = 5;
	std::string strServerIp = "127.0.0.1";
	const int port = 6666;

	uv::CLooper* ptrLooper = new uv::CLooper;
	ptrLooper->InitLooper();
	std::map<int, uv::CTcpClient*> clientMap;
	for (int i = 0; i < clientsize; ++i) {
		uv::CTcpClient* ptrClient = new uv::CTcpClient(DEF_PACK_HEAD_FLAG);
		clientMap[ptrClient->GetClientId()] = ptrClient;
		ptrClient->AttachLooper(ptrLooper);

		ptrClient->OnConnectCBEvent([](int status, void* userdata){
			uv::CTcpClient* pClient = (uv::CTcpClient*)userdata;
			if(status == uv::CTcpClient::TCP_STATUS_CONNECTED){
				LOGI("client_id=%d connect succeed.", pClient->GetClientId());
			}
			else{
				LOGI("client_id=%d connect error=%s.", pClient->GetClientId(),  pClient->GetLastErrMsg());
			}
		});
		
		ptrClient->OnRecvCBEvent([](NetPacket* pNetPacket, void* userdata){
			uv::CTcpClient* pClient = (uv::CTcpClient*)userdata;
			char szRecvData[1024] = {0};
			std::memcpy(szRecvData, pNetPacket->data, pNetPacket->dataSize);
			LOGI("[RECV][id=%d] =>---%s",  pClient->GetClientId(), szRecvData);
		});

		ptrClient->OnCloseCBEvent([&clientMap](int clientid, void* userdata){
			uv::CTcpClient* pClient = (uv::CTcpClient*)userdata;
			LOGI("[CLOSE][id=%d]", clientid);
		});

		ptrClient->Connect(strServerIp.c_str(), port);
	}

	char senddata[256];
	int nTry = 500;
	while (nTry > 0) 
	{
		for (auto iter = clientMap.begin(), iterEnd = clientMap.end();
			iter != iterEnd; iter++) {
			uv::CTcpClient* pClient = iter->second;
			if(pClient->IsConnected())
			{
				memset(senddata, 0, sizeof(senddata));
				sprintf(senddata, "%d", ++call_time);
				std::string str = PacketData(pClient->GetClientId(), senddata, strlen(senddata));
				if (pClient->Send(&str[0], str.length()) <= 0) {
					LOGI("[ERROR][id=%d] =>%s",  pClient->GetClientId(), pClient->GetLastErrMsg());
				} else {
					LOGI("[SEND][id=%d] =>+++%s",  pClient->GetClientId(), senddata);
				}
			}
			
		}
		nTry--;
		Sleep(10);
	}

	//shutdow all
	for (auto iter = clientMap.begin(), iterEnd = clientMap.end();
		iter != iterEnd; iter++) {
			uv::CTcpClient* pClient = iter->second;
			if(pClient->IsConnected())
			{
				pClient->Shutdown();
			}
	}

	delete ptrLooper;
	ptrLooper = nullptr;

	//delete 
	for (auto iter = clientMap.begin(), iterEnd = clientMap.end();
		iter != iterEnd; iter++) {
			uv::CTcpClient* pClient = iter->second;
			delete pClient;
			pClient = nullptr;
	}
	clientMap.clear();
}

void TestUdpClientFunc()
{
	std::string strServerIp = "127.0.0.1";
	const int port = 6666;

	uv::CLooper* ptrLooper = new uv::CLooper;
	ptrLooper->InitLooper();
	uv::CUvUdp* ptrClient = new uv::CUvUdp();
	ptrClient->AttachLooper(ptrLooper);
	ptrClient->Bind("127.0.0.1", 3333);
	ptrClient->OnRecvCBEvent([](const char*_buff, int size, const struct sockaddr* pAddr, unsigned iFlag, void* userdata){
		uv::CUvUdp* pClient = (uv::CUvUdp*)userdata;
		char szRecvData[1024] = {0};
		std::memcpy(szRecvData, _buff, size);
		LOGI("[RECV] =>---%s",  szRecvData);
	});

	char senddata[256];
	int nTry = 500;
	while (nTry > 0) 
	{
		memset(senddata, 0, sizeof(senddata));
		sprintf(senddata, "%d", ++call_time);
		if (ptrClient->Send(senddata, strlen(senddata), strServerIp.c_str(), port) <= 0) {
			LOGI("[ERROR] =>%s",  ptrClient->GetLastErrMsg());
		} else {
			LOGI("[SEND] =>+++%s",  senddata);
		}
		nTry--;
		Sleep(10);
	}

	delete ptrLooper;
	ptrLooper = nullptr;

	//delete 
	delete ptrClient;
	ptrClient = nullptr;
}

int _tmain(int argc, _TCHAR* argv[])
{
	TestTcpClientFunc();
	//TestUdpClientFunc();
	system("pause");
	return 0;
}

