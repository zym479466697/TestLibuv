#ifndef TCPSERVER_H
#define TCPSERVER_H
#include <string>
#include <list>
#include <map>
#include <vector>
#include <queue>
#include "uv_utils.h"
#include "Packet.h"
namespace uv
{

class TcpClientSession;
typedef struct _tcp_session_ctx {
    uv_tcp_t _session_tcp_handle;//data filed store this
    PacketSync* packet_;//userdata filed storethis
    uv_buf_t read_buf_;
    int clientid;
    void* parent_server;//tcpserver
    void* parent_acceptclient;//accept client
} TcpSessionCtx;

TcpSessionCtx* AllocTcpSessionCtx(void* parentserver);
void FreeTcpSessionCtx(TcpSessionCtx* ctx);

class TCPServer
{
	friend TcpClientSession;
public:
    TCPServer(uint32_t packhead);
    virtual ~TCPServer();
public:
    void SetNewConnectCB(NewConnectCB cb, void* userdata);//set new connect cb.
    //void SetTcpClientSessionRecvCB(int clientid, TcpClientSessionRecvCB cb, void* userdata); //set recv cb. call for each accept client.
    void SetTcpClosedCB(TcpCloseCB pfun, void* userdata);//set close cb.
    bool Start(const char* ip, int port, bool isIPv6 = false);//Start the server, ipv4
    void Close();//send close command. verify IsClosed for real closed
	bool Broadcast(const std::string& senddata, std::vector<int> excludeid);//broadcast to all clients, except the client who's id in excludeid

    //Enable or disable Nagle’s algorithm. must call after Server succeed start.
    bool SetNoDelay(bool enable);
	//Enable or disable KeepAlive. must call after Server succeed start.
	//delay is the initial delay in seconds, ignored when enable is zero
    bool SetKeepAlive(int enable, unsigned int delay);

	bool IsClosed() {//verify if real closed
		return _is_closed;
	};
    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };

protected:
    int CreateSessionId()const;
	void PushCloseEvent();
	int PushWriteEvent(const char* _buff, int _size);
	bool PushBroadcastEvent(const std::string& senddata, std::vector<int> excludeid);
	void DoEvent();
    //Static callback function
	static void AsyncCB(uv_async_t* handle);//async close
    static void AfterServerClose(uv_handle_t* handle);
	static void TcpClientSessionHandleClose(int clientid, void* userdata); //AcceptClient close cb
    static void AcceptConnection(uv_stream_t* server, int status);
	static void CloseWalkCB(uv_handle_t* handle, void* arg);//close all handle in loop
	static void WorkThread(void* arg);//start thread,run until use close the server
private:
    enum {
        START_TIMEOUT,
        START_FINISH,
        START_ERROR,
        START_DIS,
    };
	std::string _bind_ip;
	int _bind_port;
	uint32_t _packet_head;//protocol head

    bool init();
    void closeinl();//real close fun
    bool run(int status = UV_RUN_DEFAULT);
    bool bind(const char* ip, int port, bool isIPv6);
    bool listen(int backlog = SOMAXCONN);
    bool sendinl(const char* _buff, int _size, TcpSessionCtx* client);
    bool broadcastinl(const char* _buff, int _size, std::vector<int> excludeidList);
    uv_loop_t loop_;
    uv_tcp_t _tcp_server_handle;
	uv_async_t _async_event_handle;
	uv_thread_t _thread_handle;//start thread handle

	std::queue<UvEvent*> _event_queue;
	uv::CMutex _event_queue_mutex;
	std::map<int, TcpClientSession*> _client_session_map; //clients map
    bool _is_closed;
    //TCPServerProtocolProcess* protocol_;//protocol
    int _start_status;
    std::string errmsg_;

    NewConnectCB _new_conn_cb_func;
    void* _new_conn_cb_func_user_data;
    TcpCloseCB _tcp_closed_cb_func;
    void* _tcp_closed_cb_func_user_data;

    std::list<TcpSessionCtx*> _idle_tcp_client_ctx_list;//Availa accept client data
public:
    std::list<write_param*> writeparam_list_;//Availa write_t
};


class TcpClientSession
{
public:
    TcpClientSession(TcpSessionCtx* control, int clientid, uint32_t packhead, uv_loop_t* loop);
    virtual ~TcpClientSession();
    void SetRecvCB(TcpClientSessionRecvCB pfun, void* userdata);//set recv cb
    void SetClosedCB(TcpCloseCB pfun, void* userdata);//set close cb.
	bool Send(const char* _buff, int _size);
    TcpSessionCtx* GetTcpHandle(void) const;
    void Close();
    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
private:
    bool init(uint32_t packhead);
    uv_loop_t* loop_;
    int client_id_;
    TcpSessionCtx* client_handle_;//accept client data
    bool isclosed_;
    std::string errmsg_;
    TcpClientSessionRecvCB recvcb_;
    void* recvcb_userdata_;
    TcpCloseCB closedcb_;
    void* closedcb_userdata_;
public:
    static void AfterClientClose(uv_handle_t* handle);
	static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
	static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
	static void AfterSend(uv_write_t* req, int status);
	static void GetPacket(const char* _buff, int _size, void* userdata);
};

}


#endif // TCPSERVER_H