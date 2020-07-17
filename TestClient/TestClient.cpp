// TestClient.cpp : 定义控制台应用程序的入口点。
//
#include "stdafx.h"
#include "TcpClient.h"
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
		_snprintf_s(logbufw, _countof(logbufw), _TRUNCATE, "[%d][%s][ERROR]%s\n", GetCurrentThreadId(), szTime, szMessage);
	}
	else {
		_snprintf_s(logbufw, _countof(logbufw), _TRUNCATE, "[%d][%s][INFO]%s\n", GetCurrentThreadId(), szTime, szMessage);
	}
	//写入文件
	//OutputDebugStringA(logbufw);
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


void CloseCB(int clientid, void* userdata)
{
	LOGI("cliend %d close", clientid);
	uv::TCPClient* client = (uv::TCPClient*)userdata;
	client->Close();
}

//读取回调
void ReadCB(NetPacket* pNetPacket, void* userdata)
{
	char szRecvData[1024] = {0};
	std::memcpy(szRecvData, pNetPacket->data, pNetPacket->dataSize);
	LOGI("recv from server data =%s",  szRecvData);
}

//连接回调
void ConnectCB(int status, void* userdata)
{
	uv::TCPClient* pClient = (uv::TCPClient*)userdata;
	if(status == uv::TCPClient::CONNECT_FINISH)
	{
		LOGI("client(%p) connect succeed.", pClient);
	}
}

int call_time = 0;

int _tmain(int argc, _TCHAR* argv[])
{
	const int clientsize = 20;
	std::string strServerIp = "127.0.0.1";
	const int port = 6666;
	
	uv::TCPClient** pClients = new uv::TCPClient*[clientsize];

	for (int i = 0; i < clientsize; ++i) {
		pClients[i] = new uv::TCPClient(DEF_PACK_HEAD_FLAG);
		pClients[i]->SetRecvCB(ReadCB, pClients[i]);
		pClients[i]->SetClosedCB(CloseCB, pClients[i]);
		pClients[i]->SetConnectCB(ConnectCB, pClients[i]);
		if (!pClients[i]->Connect(strServerIp.c_str(), port)) {
			LOGE("connect error:%s\n", pClients[i]->GetLastErrMsg());
		} else {
			
		}
	}
	char senddata[256];
	while (true) {
		for (int i = 0; i < clientsize; ++i) {
			if(pClients[i]->IsConnected())
			{
				memset(senddata, 0, sizeof(senddata));
				sprintf(senddata, "client(%p) call %d", pClients[i], ++call_time);
				std::string str = PacketData(i, senddata, strlen(senddata));
				if (pClients[i]->Send(&str[0], str.length()) <= 0) {
					LOGE("(%p)send error.%s", pClients[i], pClients[i]->GetLastErrMsg());
				} else {
					LOGI("send succeed:%s", senddata);
				}
			}
		}
		Sleep(100);
	}
	return 0;
}

