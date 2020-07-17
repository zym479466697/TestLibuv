#include "TcpServer.h"
#include <assert.h>
#include <algorithm>
#define MAXLISTSIZE 20

namespace uv
{

TcpSessionCtx* AllocTcpSessionCtx(void* parentserver)
{
	TcpSessionCtx* ctx = (TcpSessionCtx*)malloc(sizeof(*ctx));
	ctx->packet_ = new PacketSync;
	ctx->read_buf_.base = (char*)malloc(PACK_BUFFER_SIZE);
	ctx->read_buf_.len = PACK_BUFFER_SIZE;
	ctx->parent_server = parentserver;
	ctx->parent_acceptclient = NULL;
	return ctx;
}

void FreeTcpSessionCtx(TcpSessionCtx* ctx)
{
	delete ctx->packet_;
	free(ctx->read_buf_.base);
	free(ctx);
}

/*****************************************TCP Server*************************************************************/
TCPServer::TCPServer(uint32_t packhead)
    : _packet_head(packhead)
    , _new_conn_cb_func(nullptr), _new_conn_cb_func_user_data(nullptr)
	, _tcp_closed_cb_func(nullptr), _tcp_closed_cb_func_user_data(nullptr)
    , _is_closed(true)
    , _start_status(START_DIS)
{
    int iret = uv_loop_init(&loop_);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("init loop error: %s\n", errmsg_.c_str());
    }
}


TCPServer::~TCPServer()
{
    Close();
    uv_thread_join(&_thread_handle);
    uv_loop_close(&loop_);
    for (auto it = _idle_tcp_client_ctx_list.begin(); it != _idle_tcp_client_ctx_list.end(); ++it) {
        FreeTcpSessionCtx(*it);
    }
    _idle_tcp_client_ctx_list.clear();

    for (auto it = writeparam_list_.begin(); it != writeparam_list_.end(); ++it) {
        FreeWriteParam(*it);
    }
    writeparam_list_.clear();
    LOGI("tcp server exit.");
}

bool TCPServer::init()
{
    if (!_is_closed) {
        return true;
    }
    int iret = uv_async_init(&loop_, &_async_event_handle, AsyncCB);
    if (iret) {
        errmsg_ = GetUVError(iret);
		LOGE("uv_async_init %s", errmsg_.c_str());
        return false;
    }
    _async_event_handle.data = this;
    iret = uv_tcp_init(&loop_, &_tcp_server_handle);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("uv_tcp_init %s", errmsg_.c_str());
        return false;
    }
    _tcp_server_handle.data = this;
    _is_closed = false;
    return true;
}

void TCPServer::closeinl()
{
    if (_is_closed) {
        return;
    }
	{
		//关闭所有session tcp handle
		for (auto iter = _client_session_map.begin(), iterEnd = _client_session_map.end(); 
			iter != iterEnd; ++iter) {
			iter->second->Close();
		}
	}
	//关闭loop所有handle
    uv_walk(&loop_, CloseWalkCB, this);//close all handle in loop
    LOGI("close server");
}

bool TCPServer::run(int status)
{
    LOGI("server runing.");
    int iret = uv_run(&loop_, (uv_run_mode)status);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("uv_run %s", errmsg_.c_str());
        return false;
    }
    return true;
}

bool TCPServer::SetNoDelay(bool enable)
{
    int iret = uv_tcp_nodelay(&_tcp_server_handle, enable ? 1 : 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("uv_tcp_nodelay %s", errmsg_.c_str());
        return false;
    }
    return true;
}

bool TCPServer::SetKeepAlive(int enable, unsigned int delay)
{
    int iret = uv_tcp_keepalive(&_tcp_server_handle, enable , delay);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("uv_tcp_keepalive %s", errmsg_.c_str());
        return false;
    }
    return true;
}

bool TCPServer::bind(const char* ip, int port, bool isIPv6)
{
    struct sockaddr_in bind_addr;
	 struct sockaddr_in6 bind_addr_ipv6;
    int iret = 0;
	if(!isIPv6)
		iret = uv_ip4_addr(ip, port, &bind_addr);
	else
		iret = uv_ip6_addr(ip, port, &bind_addr_ipv6);

    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("uv_ip4_addr %s", errmsg_.c_str());
        return false;
    }
	if(!isIPv6)
		 iret = uv_tcp_bind(&_tcp_server_handle, (const struct sockaddr*)&bind_addr, 0);
	else
		 iret = uv_tcp_bind(&_tcp_server_handle, (const struct sockaddr*)&bind_addr_ipv6, 0);

    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("uv_tcp_bind %s", errmsg_.c_str());
        return false;
    }
	LOGI("server bind ip=%s port=%d", ip, port);
    return true;
}


bool TCPServer::listen(int backlog)
{
    int iret = uv_listen((uv_stream_t*) &_tcp_server_handle, backlog, AcceptConnection);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("listen %s", errmsg_.c_str());
        return false;
    }
    LOGI("server Start listen. Runing.......");
    return true;
}

bool TCPServer::Start(const char* ip, int port, bool isIPv6/* = false*/)
{
    _bind_ip = ip;
    _bind_port = port;
    closeinl();
    if (!init()) {
        return false;
    }
	if (!bind(_bind_ip.c_str(), _bind_port, isIPv6)) {
		return false;
	}
 
    if (!listen(SOMAXCONN)) {
        return false;
    }

    int iret = uv_thread_create(&_thread_handle, WorkThread, this);//use thread to wait for start succeed.
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("uv_thread_create %s", errmsg_.c_str());
        return false;
    }
    int wait_count = 0;
    while (_start_status == START_DIS) {
        ThreadSleep(100);
        if (++wait_count > 100) {
            _start_status = START_TIMEOUT;
            break;
        }
    }
    return _start_status == START_FINISH;
}


void TCPServer::WorkThread(void* arg)
{
    TCPServer* theclass = (TCPServer*)arg;
    theclass->_start_status = START_FINISH;
    theclass->run();
    //the server is close when come here
    theclass->_is_closed = true;
    LOGI("server  had closed.");
    if (theclass->_tcp_closed_cb_func) {//trigger the close cb
		//-1表示bind tcp close cb
        theclass->_tcp_closed_cb_func(-1, theclass->_tcp_closed_cb_func_user_data);
    }
}

void TCPServer::AcceptConnection(uv_stream_t* server, int status)
{
    TCPServer* tcp_server = (TCPServer*)server->data;
    assert(tcp_server);
    if (status) {
        tcp_server->errmsg_ = GetUVError(status);
		LOGE("AcceptConnection %s", tcp_server->errmsg_.c_str());
        return;
    }

    TcpSessionCtx* ptrCtx = NULL;
    if (tcp_server->_idle_tcp_client_ctx_list.empty()) {
        ptrCtx = AllocTcpSessionCtx(tcp_server);
    } else {
        ptrCtx = tcp_server->_idle_tcp_client_ctx_list.front();
        tcp_server->_idle_tcp_client_ctx_list.pop_front();
        ptrCtx->parent_acceptclient = NULL;
    }

    int iret = uv_tcp_init(&tcp_server->loop_, &ptrCtx->_session_tcp_handle);
    if (iret) {
        tcp_server->_idle_tcp_client_ctx_list.push_back(ptrCtx);//Recycle
        tcp_server->errmsg_ = GetUVError(iret);
		LOGE("uv_tcp_init %s", tcp_server->errmsg_.c_str());
        return;
    }
    ptrCtx->_session_tcp_handle.data = ptrCtx;

    auto clientid = tcp_server->CreateSessionId();
    ptrCtx->clientid = clientid;
    iret = uv_accept((uv_stream_t*)server, (uv_stream_t*)&ptrCtx->_session_tcp_handle);
    if (iret) {
        tcp_server->_idle_tcp_client_ctx_list.push_back(ptrCtx);//Recycle
        tcp_server->errmsg_ = GetUVError(iret);
        LOGE("uv_accept %s", tcp_server->errmsg_.c_str());
        return;
    }
    ptrCtx->packet_->SetPacketCB(TcpClientSession::GetPacket, (void*)ptrCtx);
    ptrCtx->packet_->Start(tcp_server->_packet_head);
    iret = uv_read_start((uv_stream_t*)&ptrCtx->_session_tcp_handle, TcpClientSession::AllocBufferForRecv, TcpClientSession::AfterRecv);
    if (iret) {
        uv_close((uv_handle_t*)&ptrCtx->_session_tcp_handle,  NULL);
		//Recycle
		if (tcp_server->_idle_tcp_client_ctx_list.size() > MAXLISTSIZE) {
			FreeTcpSessionCtx(ptrCtx);
		} else {
			tcp_server->_idle_tcp_client_ctx_list.push_back(ptrCtx);
		}
        tcp_server->errmsg_ = GetUVError(iret);
        LOGE("uv_close %s", tcp_server->errmsg_.c_str());
        return;
    }

    TcpClientSession* cdata = new TcpClientSession(ptrCtx, clientid, tcp_server->_packet_head, &tcp_server->loop_); //delete on SubClientClosed
    cdata->SetClosedCB(TCPServer::TcpClientSessionHandleClose, tcp_server);
	tcp_server->_client_session_map.insert(std::make_pair(clientid, cdata)); //add accept client
    if (tcp_server->_new_conn_cb_func) {
        tcp_server->_new_conn_cb_func(clientid, tcp_server->_new_conn_cb_func_user_data);
    }
	LOGI("new client id=%d",clientid);
    return;
}

//void TCPServer::SetTcpClientSessionRecvCB(int clientid, TcpClientSessionRecvCB cb, void* userdata)
//{
//    auto itfind = _client_session_map.find(clientid);
//    if (itfind != _client_session_map.end()) {
//        itfind->second->SetRecvCB(cb, userdata);
//    }
//}

void TCPServer::SetNewConnectCB(NewConnectCB cb, void* userdata)
{
    _new_conn_cb_func = cb;
    _new_conn_cb_func_user_data = userdata;
}

void TCPServer::SetTcpClosedCB(TcpCloseCB pfun, void* userdata)
{
    _tcp_closed_cb_func = pfun;
    _tcp_closed_cb_func_user_data = userdata;
}

/* Fully close a loop */
void TCPServer::CloseWalkCB(uv_handle_t* handle, void* arg)
{
	TCPServer* theclass = (TCPServer*)arg;
	if (!uv_is_closing(handle)) {
		uv_close(handle, AfterServerClose);
	}
}

void TCPServer::AfterServerClose(uv_handle_t* handle)
{
    TCPServer* theclass = (TCPServer*)handle->data;
    fprintf(stdout, "Close CB handle %p\n", handle);
}


int TCPServer::CreateSessionId() const
{
    static int s_id = 0;
    return ++s_id;
}

void TCPServer::TcpClientSessionHandleClose(int clientid, void* userdata)
{
    TCPServer* theclass = (TCPServer*)userdata;
    auto itfind = theclass->_client_session_map.find(clientid);
    if (itfind != theclass->_client_session_map.end()) {
        if (theclass->_tcp_closed_cb_func) {
            theclass->_tcp_closed_cb_func(clientid, theclass->_tcp_closed_cb_func_user_data);
        }
        if (theclass->_idle_tcp_client_ctx_list.size() > MAXLISTSIZE) {
            FreeTcpSessionCtx(itfind->second->GetTcpHandle());
        } else {
            theclass->_idle_tcp_client_ctx_list.push_back(itfind->second->GetTcpHandle());
        }
        delete itfind->second;
        LOGI("delete client:%d", itfind->first);
        theclass->_client_session_map.erase(itfind);
    }
}

void TCPServer::AsyncCB(uv_async_t* handle)
{
    TCPServer* theclass = (TCPServer*)handle->data;
	theclass->DoEvent();
}

void TCPServer::PushCloseEvent()
{
	CAutoLock locker(&_event_queue_mutex);
	_event_queue.push(CreateUvEvent(UV_EVENT_TYPE_CLOSE));
	uv_async_send(&_async_event_handle);
}

int TCPServer::PushWriteEvent(const char* _buff, int _size)
{
	CAutoLock locker(&_event_queue_mutex);
	_event_queue.push(CreateUvEvent(UV_EVENT_TYPE_WRITE, _buff, _size));
	uv_async_send(&_async_event_handle);
	return _size;
}

bool TCPServer::PushBroadcastEvent(const std::string& senddata, std::vector<int> excludeidList)
{
	CAutoLock locker(&_event_queue_mutex);
	UvEvent* pEvent = CreateUvEvent(UV_EVENT_TYPE_BROADCAST, senddata.c_str(), senddata.size());
	pEvent->_list = excludeidList;
	_event_queue.push(pEvent);
	uv_async_send(&_async_event_handle);
	return true;
}

void TCPServer::DoEvent()
{
	CAutoLock locker(&_event_queue_mutex);
	while(!_event_queue.empty())
	{
		UvEvent *pEvent = _event_queue.front();
		switch(pEvent->type)
		{
		case UV_EVENT_TYPE_CLOSE:
			{
				closeinl();
			}break;
		case UV_EVENT_TYPE_BROADCAST:
			{
				broadcastinl(pEvent->_buff, pEvent->_size, pEvent->_list);
			}break;
		}
		_event_queue.pop();
		FreeUvEvent(pEvent);
	}
}

void TCPServer::Close()
{
	if (_is_closed) {
		return;
	}
	PushCloseEvent();
}

bool TCPServer::Broadcast(const std::string& senddata, std::vector<int> excludeid)
{
	return PushBroadcastEvent(senddata, excludeid);
}

bool TCPServer::broadcastinl(const char* _buff, int _size, std::vector<int> excludeidList)
{
	TcpClientSession* pClient = NULL;
	write_param* writep = NULL;
	if (excludeidList.empty()) {
		for (auto it = _client_session_map.begin(); it != _client_session_map.end(); ++it) {
			pClient = it->second;
			sendinl(_buff, _size, pClient->GetTcpHandle());
		}
	} else {
		for (auto it = _client_session_map.begin(); it != _client_session_map.end(); ++it) {
			auto itfind = std::find(excludeidList.begin(), excludeidList.end(), it->first);
			if (itfind != excludeidList.end()) {
				excludeidList.erase(itfind);
				continue;
			}
			pClient = it->second;
			sendinl(_buff, _size, pClient->GetTcpHandle());
		}
	}
	return true;
}

bool TCPServer::sendinl(const char* _buff, int _size, TcpSessionCtx* client)
{
    write_param* writep = NULL;
    if (writeparam_list_.empty()) {
        writep = AllocWriteParam(PACK_BUFFER_SIZE);
    } else {
        writep = writeparam_list_.front();
        writeparam_list_.pop_front();
    }
    if (writep->buf_.len < _size) {
        writep->buf_.base = (char*)realloc(writep->buf_.base, _size);
        writep->buf_.len = _size;
    }
    memcpy(writep->buf_.base, _buff, _size);
    writep->buf_.len = _size;
    writep->write_req_.data = client;
    int iret = uv_write((uv_write_t*)&writep->write_req_, (uv_stream_t*)&client->_session_tcp_handle, &writep->buf_, 1, TcpClientSession::AfterSend);//发送
    if (iret) {
        writeparam_list_.push_back(writep);
        errmsg_ = "send data error.";
        LOGE("client(%d) send error:", client, GetUVError(iret));
        LOGE("send error. %s-%s\n", uv_err_name(iret), uv_strerror(iret));
        return false;
    }
    return true;
}

//void TCPServer::SetPortocol(TCPServerProtocolProcess* pro)
//{
//    protocol_ = pro;
//}

/*****************************************TcpClientSession*************************************************************/
TcpClientSession::TcpClientSession(TcpSessionCtx* control,  int clientid, uint32_t packhead, uv_loop_t* loop)
    : client_handle_(control)
    , client_id_(clientid), loop_(loop)
    , isclosed_(true)
    , recvcb_(nullptr), recvcb_userdata_(nullptr), closedcb_(nullptr), closedcb_userdata_(nullptr)
{
    init(packhead);
}

TcpClientSession::~TcpClientSession()
{
    Close();
    //while will block loop.
	//the right way is new TcpClientSession and delete it on SetClosedCB'cb
    while (!isclosed_) {
        ThreadSleep(10);
    }
}

bool TcpClientSession::init(uint32_t packhead)
{
    if (!isclosed_) {
        return true;
    }
    client_handle_->parent_acceptclient = this;
    isclosed_ = false;
    return true;
}

void TcpClientSession::Close()
{
    if (isclosed_) {
        return;
    }
    client_handle_->_session_tcp_handle.data = this;
    //send close command
    uv_close((uv_handle_t*)&client_handle_->_session_tcp_handle, AfterClientClose);
    LOGI("client(%d)close", this->client_id_);
}

void TcpClientSession::AfterClientClose(uv_handle_t* handle)
{
    TcpClientSession* theclass = (TcpClientSession*)handle->data;
    assert(theclass);
    if (handle == (uv_handle_t*)&theclass->client_handle_->_session_tcp_handle) {
        theclass->isclosed_ = true;
        LOGI("client  had closed.");
        if (theclass->closedcb_) {//notice tcpserver the client had closed
            theclass->closedcb_(theclass->client_id_, theclass->closedcb_userdata_);
        }
    }
}

void TcpClientSession::SetRecvCB(TcpClientSessionRecvCB pfun, void* userdata)
{
    //GetPacket trigger this cb
    recvcb_ = pfun;
    recvcb_userdata_ = userdata;
}

void TcpClientSession::SetClosedCB(TcpCloseCB pfun, void* userdata)
{
    //AfterRecv trigger this cb
    closedcb_ = pfun;
    closedcb_userdata_ = userdata;
}

TcpSessionCtx* TcpClientSession::GetTcpHandle(void) const
{
    return client_handle_;
}

void TcpClientSession::AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    TcpSessionCtx* theclass = (TcpSessionCtx*)handle->data;
    assert(theclass);
    *buf = theclass->read_buf_;
}

void TcpClientSession::AfterRecv(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
    TcpSessionCtx* theclass = (TcpSessionCtx*)handle->data;
    assert(theclass);
    if (nread < 0) {/* Error or EOF */
        if (nread == UV_EOF) {
            LOGI("client(%d)eof", theclass->clientid);
        } else if (nread == UV_ECONNRESET) {
            LOGI("client(%d) conn reset ", theclass->clientid);
        } else {
			LOGI("client(%d) read error=%s", theclass->clientid, GetUVError(nread).c_str());
        }
        TcpClientSession* clien_session = (TcpClientSession*)theclass->parent_acceptclient;
        clien_session->Close();
        return;
    } else if (0 == nread)  {/* Everything OK, but nothing read. */

    } else {
        theclass->packet_->recvdata((const unsigned char*)buf->base, nread);
    }
}

void TcpClientSession::AfterSend(uv_write_t* req, int status)
{
    TcpSessionCtx* theclass = (TcpSessionCtx*)req->data;
    TCPServer* parent = (TCPServer*)theclass->parent_server;
    if (parent->writeparam_list_.size() > MAXLISTSIZE) {
        FreeWriteParam((write_param*)req);
    } else {
        parent->writeparam_list_.push_back((write_param*)req);
    }
    if (status < 0) {
        LOGE("send data error:%s", GetUVError(status).c_str());
        LOGE("send error %s.%s\n", uv_err_name(status), uv_strerror(status));
    }
}

void TcpClientSession::GetPacket(const char* _buff, int _size, void* userdata)
{
    assert(userdata);
    TcpSessionCtx* theclass = (TcpSessionCtx*)userdata;
    TCPServer* parent = (TCPServer*)theclass->parent_server;

	NetPacket* pNetPacket = (NetPacket*)malloc(_size);
	std::memcpy(pNetPacket, _buff, _size);
    //const std::string& senddata = parent->protocol_->ParsePacket(packethead, packetdata);
	char szRecvData[1024] = {0};
	std::memcpy(szRecvData, pNetPacket->data, pNetPacket->dataSize);
	LOGI("clientid=%d recv=%s", theclass->clientid, szRecvData);
    parent->sendinl(_buff, _size, theclass);
	free(pNetPacket);
}

}