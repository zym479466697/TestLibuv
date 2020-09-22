#ifndef TCPSERVER_H
#define TCPSERVER_H
#include <string>
#include <list>
#include <map>
#include <vector>
#include <queue>
#include "uv_utils.h"
#include "Packet.h"
#include "Looper.h"
#include <functional>

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

class TCPServer : public ILooperEvent
{
	friend TcpClientSession;
public:
    TCPServer(uint32_t packhead);
    virtual ~TCPServer();
public:
	bool InitLooper(CLooper* looper);
	void OnNewConnectCBEvent(std::function<void(int, void*)> func_new_conn);
	void OnTcpClientCloseCBEvent(std::function<void(int, void*)> func_tcp_client_close);
    
	bool Start(const char* ip, int port, bool isIPv6 = false);//Start the server, ipv4
	void Close();
	bool Broadcast(const std::string& senddata, std::vector<int> excludeid);//broadcast to all clients, except the client who's id in excludeid
    bool SetNoDelay(bool enable);
    bool SetKeepAlive(int enable, unsigned int delay);

	bool IsClosed() {//verify if real closed
		return _is_closed;
	};
    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };

protected:
	virtual void DoEvent(UvEvent *);
	virtual void OnHandleClose(uv_handle_t *);

    int CreateSessionId()const;
	bool PushBroadcastEvent(const std::string& senddata, std::vector<int> excludeid);

	bool bind(const char* ip, int port, bool isIPv6);
	bool listen(int backlog = SOMAXCONN);
	bool sendinl(const char* _buff, int _size, TcpSessionCtx* client);
	bool broadcastinl(const char* _buff, int _size, std::vector<int> excludeidList);

    //Static callback function
    static void AfterServerClose(uv_handle_t* handle);
	static void TcpClientSessionHandleClose(int clientid, void* userdata); //AcceptClient close cb
    static void AcceptConnection(uv_stream_t* server, int status);
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
	bool _is_closed;
	std::string errmsg_;

    CLooper* _looper;
    uv_tcp_t _tcp_server_handle;

	std::function<void(int, void*)> _func_new_conn;
	std::function<void(int, void*)> _func_tcp_client_close;
	
	std::map<int, TcpClientSession*> _client_session_map; //clients map
    std::list<TcpSessionCtx*> _idle_tcp_client_ctx_list;//Availa accept client data
    std::list<write_param*> writeparam_list_;//Availa write_t
};


class TcpClientSession : ILooperEvent
{
public:
    TcpClientSession(TcpSessionCtx* control, int clientid, uint32_t packhead, CLooper* loop);
    virtual ~TcpClientSession();
    TcpSessionCtx* GetTcpHandle(void) const;
    void Close();
    const char* GetLastErrMsg() const {
        return errmsg_.c_str();
    };
public:
	static void AfterClientClose(uv_handle_t* handle);
	static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
	static void AfterRecv(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf);
	static void AfterSend(uv_write_t* req, int status);
	static void GetPacket(const char* _buff, int _size, void* userdata);
protected:
	virtual void DoEvent(UvEvent *);
	virtual void OnHandleClose(uv_handle_t *);
private:
    CLooper* _looper;
    TcpSessionCtx* client_handle_;
	int client_id_;
    bool isclosed_;
    std::string errmsg_;
};

}


#endif // TCPSERVER_H