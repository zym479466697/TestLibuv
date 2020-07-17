#ifndef TCPCLIENT_H
#define TCPCLIENT_H
#include <string>
#include <list>
#include <queue>
#include "uv.h"
#include "Packet.h"

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

class TCPClient
{
public:
    TCPClient(uint32_t packhead);
    virtual ~TCPClient();
	enum  CONNECT_STATUS{
		CONNECT_TIMEOUT,
		CONNECT_FINISH,
		CONNECT_ERROR,
		CONNECT_DIS,
	};
public:
	bool Connect(const char* ip, int port, bool isIPv6 = false);
	int  Send(const char* data, std::size_t len);
	void Close();
	void SetReConnect(bool bReConn, int intervalTime = 3000);
	 bool IsClosed() {
		 return _is_closed;
	 }
	 bool IsConnected() {
		 return _connect_status == CONNECT_FINISH;
	 }
	 //Enable or disable Nagle’s algorithm. must call after Server succeed start.
	 bool SetNoDelay(bool enable);
	 //Enable or disable KeepAlive. must call after Server succeed start.
	 //delay is the initial delay in seconds, ignored when enable is zero
	 bool SetKeepAlive(int enable, unsigned int delay);

	 const char* GetLastErrMsg() const {
		 return errmsg_.c_str();
	 };

    void SetRecvCB(ClientRecvCB pfun, void* userdata);//set recv cb
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//set close cb.
	void SetConnectCB(ClientConnectCB pfun, void* userdata);
protected:
	bool StartReconnect(void);
	void StopReconnect(void);
	void PushConnectEvent();
	void PushCloseEvent();
	void DoEvent();
	int PushWriteEvent(const char* _buff, int _size);

    bool init();
    void closeinl();
	void connectinl();
	void sendinl(const char* _buff, int _size);
    bool run(int status = UV_RUN_DEFAULT);
	
    static void ConnectThread(void* arg);//connect thread,run until use close the client

    static void AfterConnect(uv_connect_t* handle, int status);
    static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
    static void AfterSend(uv_write_t* req, int status);
    static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void AfterHandleClose(uv_handle_t* handle);
	static void AsyncCB(uv_async_t* handle);//async close
	static void CloseWalkCB(uv_handle_t* handle, void* arg);//close all handle in loop
    static void GetPacket(const char* _buff, int _size, void* userdata);
	static void ReconnectTimer(uv_timer_t* handle);
private:
	std::queue<UvEvent*> _event_queue;
	uv::CMutex _event_queue_mutex;
	uv_async_t _async_event_handle;
	uv_timer_t _reconn_timer_handle;
	uv_thread_t _thread_handle;
	uv_connect_t _connect_req;
	uv_loop_t _loop;

	bool _is_ipv6;
	bool _is_closed;
	bool _is_reconnect;
	TcpClientCtx *_tcp_client_ctx;

	int64_t _reconn_interval_time;
	uint32_t _packet_head;//protocol head
    int _connect_status;
	int _connect_port;
	std::string _connect_ip;
	std::string errmsg_;

	ClientRecvCB _recvcb_func;
    void* recvcb_userdata_;

    TcpCloseCB closedcb_;
    void* closedcb_userdata_;

	ClientConnectCB _conn_cb;
	void* conn_userdata_;
};

}

#endif // TCPCLIENT_H