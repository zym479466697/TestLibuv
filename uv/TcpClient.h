#ifndef TCPCLIENT_H
#define TCPCLIENT_H
#include <string>
#include <list>
#include <queue>
#include "uv.h"
#include "Packet.h"
#include "Looper.h"

namespace uv
{

typedef struct _tcpclient_ctx {
    uv_tcp_t tcphandle;//store this on data
    uv_write_t write_req;//store this on data
    PacketSync* packet_;//store this on userdata
    uv_buf_t read_buf_;
    int clientid;
    void* parent_server;//store TCPClient point
} TcpClientCtx;

TcpClientCtx* AllocTcpClientCtx(void* parentserver);
void FreeTcpClientCtx(TcpClientCtx* ctx);

class CTcpClient
{
public:
	enum  CONNECT_STATUS{
		CONNECT_TIMEOUT,
		CONNECT_FINISH,
		CONNECT_ERROR,
		CONNECT_DIS,
	};

	CTcpClient(uint32_t packhead);
	virtual ~CTcpClient();
	bool Init(CLooper *_lp);
	void Connect(const char* ip, int port, bool isIPv6 = false);
	int  Send(const char* data, std::size_t len);
	void Close();
	bool IsConnected() {
		return _connect_status == CONNECT_FINISH;
	}
	const char* GetLastErrMsg() const {
		return errmsg_.c_str();
	};

	void SetRecvCB(ClientRecvCB pfun, void* userdata);//set recv cb
	void SetClosedCB(TcpCloseCB pfun, void* userdata);//set close cb.
	void SetConnectCB(ClientConnectCB pfun, void* userdata);

	void DoEvent(UvEvent* _event);
	void OnHandleClose(uv_handle_t*);
protected:
	
	void connectinl();
	void closeinl();
	void sendinl(const char* _buff, int _size);

	static void AfterConnect(uv_connect_t* handle, int status);
	static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
	static void AfterSend(uv_write_t* req, int status);
	static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
	static void AfterHandleClose(uv_handle_t* handle);
	static void GetPacket(const char* _buff, int _size, void* userdata);

	ClientRecvCB _recvcb_func;
	void* recvcb_userdata_;

	TcpCloseCB closedcb_;
	void* closedcb_userdata_;

	ClientConnectCB _conn_cb;
	void* conn_userdata_;
private:
	TcpClientCtx *_tcp_client_ctx;
	uv_connect_t _connect_req;
	bool _is_ipv6;
	bool _is_closed;
	uint32_t _packet_head;
	int _connect_status;
	int _connect_port;
	std::string _connect_ip;
	std::string errmsg_;
	CLooper *_looper;
};

}

#endif // TCPCLIENT_H