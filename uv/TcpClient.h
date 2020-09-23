#ifndef TCPCLIENT_H
#define TCPCLIENT_H
#include <string>
#include <list>
#include <queue>
#include "uv.h"
#include "Packet.h"
#include "Looper.h"
#include <functional>

namespace uv
{

typedef struct _tcpclient_ctx {
    uv_tcp_t tcphandle;//store this on data
    uv_write_t write_req;//store this on data
    PacketSync* packet_;//store this on userdata
    uv_buf_t read_buf_;
    int clientid;
} TcpClientCtx;

TcpClientCtx* AllocTcpClientCtx(void* parentserver);
void FreeTcpClientCtx(TcpClientCtx* ctx);

class CTcpClient : public ILooperEvent
{
	friend class CLooper;
public:
	enum  TCP_STATUS{
		TCP_STATUS_NONE,
		TCP_STATUS_CONNECTING,
		TCP_STATUS_CONNECTED,
		TCP_STATUS_CONNECT_ERROR,
	};

	CTcpClient(uint32_t packhead);
	virtual ~CTcpClient();

	bool Init(CLooper *_lp);
	bool Connect(const char* ip, int port, bool isIPv6 = false);
	void Close();
	int Send(const char* data, std::size_t len);
	bool SetNoDelay(bool enable);
	bool SetKeepAlive(int enable, unsigned int delay);

	void OnConnectCBEvent(std::function<void(int, void*)> func_conn);
	void OnRecvCBEvent(std::function<void(NetPacket*, void*)> func_recv);
	void OnCloseCBEvent(std::function<void(int, void*)> func_close);

	bool IsConnected() {
		return _connect_status == TCP_STATUS_CONNECTED;
	}
	const char* GetLastErrMsg() const {
		return errmsg_.c_str();
	};

	int GetClientId(){
		return _tcp_client_ctx->clientid;
	}

protected:
	virtual void DoEvent(UvEvent* _event);
	virtual void OnHandleClose(uv_handle_t*);

	void connectinl();
	void closeinl();
	void sendinl(const char* _buff, int _size);

	static void AfterConnect(uv_connect_t* handle, int status);
	static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
	static void AfterSend(uv_write_t* req, int status);
	static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
	static void AfterHandleClose(uv_handle_t* handle);
private:
	static int s_base_client_id;

	std::function<void(int, void*)> _func_conn_cb;
	std::function<void(int, void*)> _func_close_cb;
	std::function<void(NetPacket*, void*)> _func_recv_cb;

	TcpClientCtx *_tcp_client_ctx;
	uv_connect_t _connect_req;
	bool _is_ipv6;
	bool _is_wait_closed;
	uint32_t _packet_head;
	int _connect_status;
	int _connect_port;
	std::string _connect_ip;
	std::string errmsg_;
	CLooper *_looper;
};

}

#endif // TCPCLIENT_H