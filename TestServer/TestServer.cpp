// TestServer.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include "TcpServer.h"
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


uv::TCPServer server(DEF_PACK_HEAD_FLAG);

void CloseCB(int clientid, void* userdata)
{
	LOGI("cliend %d close\n",clientid);
	uv::TCPServer *theclass = (uv::TCPServer *)userdata;
	//is_eist = true;
}

void NewConnect(int clientid, void* userdata)
{
	LOGI("new connect:%d\n",clientid);
	//server.SetRecvCB(clientid,NULL,NULL);
}

int _tmain(int argc, _TCHAR* argv[])
{
	server.SetNewConnectCB(NewConnect, &server);
	//server.SetPortocol(&protocol);
	if(!server.Start("0.0.0.0",6666)) {
		LOGE("Start Server error:%s\n", server.GetLastErrMsg());
	}
	server.SetKeepAlive(1, 60);//enable Keepalive, 60s
	LOGI("server on main.");
	while(true) {
		Sleep(10);
	}
	return 0;
}

