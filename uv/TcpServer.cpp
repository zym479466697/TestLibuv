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
CTcpServer::CTcpServer(uint32_t packhead)
    : _packet_head(packhead)
    , _is_closed(true)
{
 
}


CTcpServer::~CTcpServer()
{
    Close();
    
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

bool CTcpServer::InitLooper(CLooper* looper)
{
	_looper = looper;
	int _ret = uv_tcp_init(_looper->GetLooper(), &_tcp_server_handle);
	if (_ret) {
		errmsg_ = GetUVError(_ret);
		LOGE("uv_tcp_init %s", errmsg_.c_str());
		return false;
	}
	_tcp_server_handle.data = this;
	_is_closed = false;
	return true;
}


bool CTcpServer::SetNoDelay(bool enable)
{
    int iret = uv_tcp_nodelay(&_tcp_server_handle, enable ? 1 : 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("uv_tcp_nodelay %s", errmsg_.c_str());
        return false;
    }
    return true;
}

bool CTcpServer::SetKeepAlive(int enable, unsigned int delay)
{
    int iret = uv_tcp_keepalive(&_tcp_server_handle, enable , delay);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("uv_tcp_keepalive %s", errmsg_.c_str());
        return false;
    }
    return true;
}

bool CTcpServer::bind(const char* ip, int port, bool isIPv6)
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


bool CTcpServer::listen(int backlog)
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

bool CTcpServer::Start(const char* ip, int port, bool isIPv6/* = false*/)
{
    _bind_ip = ip;
    _bind_port = port;
    
	if (!bind(_bind_ip.c_str(), _bind_port, isIPv6)) {
		return false;
	}
 
    if (!listen(SOMAXCONN)) {
        return false;
    }
    return true;
}


void CTcpServer::AcceptConnection(uv_stream_t* server, int status)
{
    CTcpServer* tcp_server = (CTcpServer*)server->data;
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

    int iret = uv_tcp_init(tcp_server->_looper->GetLooper(), &ptrCtx->_session_tcp_handle);
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

	ptrCtx->packet_->OnPackageCBEvent([ptrCtx](const char* _buff, int _size){
		CTcpServer* parent = (CTcpServer*)ptrCtx->parent_server;
		NetPacket* pNetPacket = (NetPacket*)malloc(_size);
		std::memcpy(pNetPacket, _buff, _size);
		if(parent->_func_tcp_client_recv)
			parent->_func_tcp_client_recv(pNetPacket, ptrCtx);
		free(pNetPacket);
	});

    ptrCtx->packet_->Init(tcp_server->_packet_head);
    iret = uv_read_start((uv_stream_t*)&ptrCtx->_session_tcp_handle, CTcpClientSession::AllocBufferForRecv, CTcpClientSession::AfterRecv);
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
	
    CTcpClientSession* cdata = new CTcpClientSession(ptrCtx, clientid, tcp_server->_packet_head, tcp_server->_looper); 
	tcp_server->_client_session_map.insert(std::make_pair(clientid, cdata)); //add accept client
    if (tcp_server->_func_new_conn) {
        tcp_server->_func_new_conn(clientid, cdata);
    }
    return;
}

void CTcpServer::OnNewConnectCBEvent(std::function<void(int, void*)> func_new_conn)
{
	_func_new_conn = func_new_conn;
}

void CTcpServer::OnTcpClientCloseCBEvent(std::function<void(int, void*)> func_tcp_client_close)
{
	_func_tcp_client_close = func_tcp_client_close;
}

void CTcpServer::OnTcpClientRecvCBEvent(std::function<void(NetPacket*, void*)> func_tcp_client_recv)
{
	_func_tcp_client_recv = func_tcp_client_recv;
}

void CTcpServer::AfterServerClose(uv_handle_t* handle)
{
    CTcpServer* theclass = (CTcpServer*)handle->data;
	theclass->_is_closed = true;
    fprintf(stdout, "Close listen tcp handle CB handle=%p\n", handle);
}


int CTcpServer::CreateSessionId() const
{
    static int s_id = 0;
    return ++s_id;
}

void CTcpServer::CTcpClientSessionHandleClose(int clientid, void* userdata)
{
    CTcpServer* theclass = (CTcpServer*)userdata;
    auto itfind = theclass->_client_session_map.find(clientid);
    if (itfind != theclass->_client_session_map.end()) {
        if (theclass->_func_tcp_client_close) {
            theclass->_func_tcp_client_close(clientid, itfind->second);
        }
        if (theclass->_idle_tcp_client_ctx_list.size() > MAXLISTSIZE) {
            FreeTcpSessionCtx(itfind->second->GetTcpHandle());
        } else {
            theclass->_idle_tcp_client_ctx_list.push_back(itfind->second->GetTcpHandle());
        }
        delete itfind->second;
        LOGI("delete client: %d", itfind->first);
        theclass->_client_session_map.erase(itfind);
    }
}

bool CTcpServer::PushBroadcastEvent(const std::string& senddata, std::vector<int> excludeidList)
{
	if(_looper){
		UvEvent* pEvent = CreateUvEvent(UV_EVENT_TYPE_BROADCAST, senddata.c_str(), senddata.size());
		pEvent->_data = this;
		pEvent->_list = excludeidList;
		_looper->PushEvent(pEvent);
		return true;
	}
	return false;
}


void CTcpServer::DoEvent(UvEvent* pEvent)
{
	switch(pEvent->type)
	{
	case UV_EVENT_TYPE_BROADCAST:
		{
			broadcastinl(pEvent->_buff, pEvent->_size, pEvent->_list);
		}break;
	}
	FreeUvEvent(pEvent);
}

void CTcpServer::OnHandleClose(uv_handle_t* handle)
{
	if (!uv_is_closing(handle)){
		uv_close(handle, AfterServerClose);
	}
}

void CTcpServer::Close()
{
	if(_looper)
		_looper->Close();
}

bool CTcpServer::Broadcast(const std::string& senddata, std::vector<int> excludeid)
{
	return PushBroadcastEvent(senddata, excludeid);
}

bool CTcpServer::broadcastinl(const char* _buff, int _size, std::vector<int> excludeidList)
{
	CTcpClientSession* pClient = NULL;
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

bool CTcpServer::sendinl(const char* _buff, int _size, TcpSessionCtx* client)
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
    int iret = uv_write((uv_write_t*)&writep->write_req_, (uv_stream_t*)&client->_session_tcp_handle, &writep->buf_, 1, CTcpClientSession::AfterSend);//发送
    if (iret) {
        writeparam_list_.push_back(writep);
        errmsg_ = "send data error.";
        LOGE("client(%d) send error:", client, GetUVError(iret));
        return false;
    }
    return true;
}


/*****************************************CTcpClientSession*************************************************************/
CTcpClientSession::CTcpClientSession(TcpSessionCtx* control,  int clientid, uint32_t packhead, CLooper* loop)
    : client_handle_(control)
    , client_id_(clientid), _looper(loop)
    , isclosed_(false)
{
	client_handle_->parent_acceptclient = this;
}

CTcpClientSession::~CTcpClientSession()
{
    Close();
    while (!isclosed_) {
        ThreadSleep(10);
    }
}

void CTcpClientSession::Close()
{
    if (isclosed_) {
        return;
    }
    client_handle_->_session_tcp_handle.data = this;
    uv_close((uv_handle_t*)&client_handle_->_session_tcp_handle, AfterClientClose);
}

void CTcpClientSession::Send(const char* _buff, int _size)
{
	CTcpServer* parent = (CTcpServer*)client_handle_->parent_server;
	parent->sendinl(_buff, _size, client_handle_);
}

void CTcpClientSession::AfterClientClose(uv_handle_t* handle)
{
    CTcpClientSession* theclass = (CTcpClientSession*)handle->data;
    assert(theclass);
    if (handle == (uv_handle_t*)&theclass->client_handle_->_session_tcp_handle) {
        theclass->isclosed_ = true;
		CTcpServer::CTcpClientSessionHandleClose(theclass->client_id_, theclass->client_handle_->parent_server);
    }
}


TcpSessionCtx* CTcpClientSession::GetTcpHandle(void) const
{
    return client_handle_;
}

void CTcpClientSession::DoEvent(UvEvent* pEvent)
{
	switch(pEvent->type)
	{
	case UV_EVENT_TYPE_CLOSE:
		{
			CTcpClientSession* ptr = (CTcpClientSession*)pEvent->_data;
			uv_handle_t* tcp_handle = (uv_handle_t*)&(ptr->client_handle_->_session_tcp_handle);
			if (!uv_is_closing(tcp_handle)){
				uv_close(tcp_handle, AfterClientClose);
			}
		}break;
	}
	FreeUvEvent(pEvent);
}

void CTcpClientSession::OnHandleClose(uv_handle_t* handle)
{
	if (!uv_is_closing(handle)){
		uv_close(handle, AfterClientClose);
	}
}

void CTcpClientSession::AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    TcpSessionCtx* theclass = (TcpSessionCtx*)handle->data;
    assert(theclass);
    *buf = theclass->read_buf_;
}

void CTcpClientSession::AfterRecv(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
    TcpSessionCtx* pSessionCtx = (TcpSessionCtx*)handle->data;
    assert(pSessionCtx);
    if (nread < 0) {/* Error or EOF */
        if (nread == UV_EOF) {
            LOGI("client(%d) eof", pSessionCtx->clientid);
        } else if (nread == UV_ECONNRESET) {
            LOGI("client(%d) conn reset ", pSessionCtx->clientid);
        } else {
			LOGI("client(%d) read error=%s", pSessionCtx->clientid, GetUVError(nread).c_str());
        }
        CTcpClientSession* clien_session = (CTcpClientSession*)pSessionCtx->parent_acceptclient;
        clien_session->Close();
        return;
    } else if (0 == nread)  {/* Everything OK, but nothing read. */

    } else {
		if(pSessionCtx->packet_)
			pSessionCtx->packet_->OnRecvData((const unsigned char*)buf->base, nread);
    }
}

void CTcpClientSession::AfterSend(uv_write_t* req, int status)
{
    TcpSessionCtx* theclass = (TcpSessionCtx*)req->data;
    CTcpServer* parent = (CTcpServer*)theclass->parent_server;
    if (parent->writeparam_list_.size() > MAXLISTSIZE) {
        FreeWriteParam((write_param*)req);
    } else {
        parent->writeparam_list_.push_back((write_param*)req);
    }
    if (status < 0) {
        LOGE("send data error:%s", GetUVError(status).c_str());
    }
}

}