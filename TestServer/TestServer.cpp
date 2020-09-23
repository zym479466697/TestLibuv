// TestServer.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "TcpServer.h"
#include "Looper.h"

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


bool CtrlHandler(DWORD fdwctrltype)
{
	switch (fdwctrltype)
	{
	// handle the ctrl-c signal.
	case CTRL_C_EVENT:
		printf("ctrl-c event\n\n");
		return(false);
	default:
		return false;
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
	uv::CLooper* ptrLooper = new uv::CLooper;
	ptrLooper->InitLooper();
	uv::CTcpServer server(DEF_PACK_HEAD_FLAG);
	server.InitLooper(ptrLooper);

	server.OnNewConnectCBEvent([](int clientid, void* userdata){
		uv::CTcpClientSession *theclass = (uv::CTcpClientSession *)userdata;
		LOGI("new connect:%d\n", clientid);
	});

	server.OnTcpClientCloseCBEvent([](int clientid, void* userdata){
		LOGI("cliend %d close", clientid);
		uv::CTcpClientSession *theclass = (uv::CTcpClientSession *)userdata;
	});

	server.OnTcpClientRecvCBEvent([](NetPacket* pNetPacket, void* userdata){
		uv::TcpSessionCtx *tcpSessionCtx = (uv::TcpSessionCtx*)userdata;
		
		char szRecvData[1024] = {0};
		std::memcpy(szRecvData, pNetPacket->data, pNetPacket->dataSize);
		LOGI("clientid=%d recv=%s", tcpSessionCtx->clientid, szRecvData);

		//send back
		uv::CTcpClientSession* parent = (uv::CTcpClientSession*)tcpSessionCtx->parent_acceptclient;
		int pack_size = pNetPacket->dataSize + NET_PACKAGE_HEADLEN;
		std::string buffer;
		buffer.resize(pack_size);
		memcpy(&(buffer[0]), (void*)pNetPacket, NET_PACKAGE_HEADLEN);
		memcpy(&(buffer[NET_PACKAGE_HEADLEN]), pNetPacket->data, pNetPacket->dataSize);
		parent->Send((char*)&buffer[0], buffer.size());
	});

	if(!server.Start("0.0.0.0", 6666)) {
		LOGE("Start Server error:%s\n", server.GetLastErrMsg());
	}
	server.SetKeepAlive(1, 60);//enable Keepalive, 60s
	LOGI("server on main.");

	if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, true))
	{
		while (1) { Sleep(1000); }
	}
	else
	{
		delete ptrLooper;
		ptrLooper = nullptr;
		printf("exit \r\n");
	}
	//Sleep(10000);
	//delete ptrLooper;
	//ptrLooper = nullptr;
	return 0;
}

