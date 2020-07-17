#include "TcpClient.h"
#define MAXLISTSIZE 20

namespace uv
{
TcpClientCtx* AllocTcpClientCtx(void* parentserver)
{
    TcpClientCtx* ctx = (TcpClientCtx*)malloc(sizeof(*ctx));
    ctx->packet_ = new PacketSync;
    ctx->read_buf_.base = (char*)malloc(PACK_BUFFER_SIZE);
    ctx->read_buf_.len = PACK_BUFFER_SIZE;
    ctx->write_req.data = ctx;//store self
    ctx->parent_server = parentserver;//store TCPClient
    return ctx;
}

void FreeTcpClientCtx(TcpClientCtx* ctx)
{
    delete ctx->packet_;
    free(ctx->read_buf_.base);
    free(ctx);
}


/*****************************************TCP Client*************************************************************/
TCPClient::TCPClient(uint32_t packhead)
    : _packet_head(packhead)
    , _recvcb_func(nullptr), recvcb_userdata_(nullptr), closedcb_(nullptr), closedcb_userdata_(nullptr)
    , _connect_status(CONNECT_DIS)
    , _is_closed(true),_is_reconnect(false)
    , _is_ipv6(false)
{
    _tcp_client_ctx = AllocTcpClientCtx(this);
    int iret = uv_loop_init(&_loop);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("%s", errmsg_.c_str());
        fprintf(stdout, "init loop error: %s\n", errmsg_.c_str());
    }
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("%s", errmsg_.c_str());
    }
    _connect_req.data = this;
}


TCPClient::~TCPClient()
{
    Close();
    uv_thread_join(&_thread_handle);
    FreeTcpClientCtx(_tcp_client_ctx);
    uv_loop_close(&_loop);
    LOGI("client("")exit");
}

bool TCPClient::init()
{
    if (!_is_closed) {
        return true;
    }
	int iret = uv_async_init(&_loop, &_async_event_handle, AsyncCB);

    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("%s", errmsg_.c_str());
        return false;
    }
    _async_event_handle.data = this;

    iret = uv_tcp_init(&_loop, &_tcp_client_ctx->tcphandle);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("%s", errmsg_.c_str());
        return false;
    }
    _tcp_client_ctx->tcphandle.data = _tcp_client_ctx;
    _tcp_client_ctx->parent_server = this;

    _tcp_client_ctx->packet_->SetPacketCB(GetPacket, _tcp_client_ctx);
    _tcp_client_ctx->packet_->Start(_packet_head);

    iret = uv_timer_init(&_loop, &_reconn_timer_handle);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("%s", errmsg_.c_str());
        return false;
    }
    _reconn_timer_handle.data = this;
    LOGI("client()Init");
    _is_closed = false;
    return true;
}

void TCPClient::closeinl()
{
    if (_is_closed) {
        return;
    }
    StopReconnect();
    uv_walk(&_loop, CloseWalkCB, this);
    LOGI("client()close");
}

bool TCPClient::run(int status)
{
    int iret = uv_run(&_loop, (uv_run_mode)status);
    _is_closed = true;
    LOGI("client had closed.");
    if (closedcb_) {//trigger close cb to user
        closedcb_(-1, closedcb_userdata_); //client id is -1.
    }
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("%s", errmsg_.c_str());
        return false;
    }
    return true;
}

bool TCPClient::SetNoDelay(bool enable)
{
    int iret = uv_tcp_nodelay(&_tcp_client_ctx->tcphandle, enable ? 1 : 0);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("%s", errmsg_.c_str());
        return false;
    }
    return true;
}

bool TCPClient::SetKeepAlive(int enable, unsigned int delay)
{
    int iret = uv_tcp_keepalive(&_tcp_client_ctx->tcphandle, enable , delay);
    if (iret) {
        errmsg_ = GetUVError(iret);
        LOGE("%s", errmsg_.c_str());
        return false;
    }
    return true;
}

bool TCPClient::Connect(const char* ip, int port, bool isIPv6/* = false*/)
{
	if(!init()) {
		return false;
	}
	_connect_ip = ip;
	_connect_port = port;
	_is_ipv6 = isIPv6;
	
	int iret = uv_thread_create(&_thread_handle, ConnectThread, this);//thread to wait for succeed connect.
	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE("%s", errmsg_.c_str());
		return false;
	}
	return true;
}

void TCPClient::connectinl()
{
	struct sockaddr_in bind_addr_ipv4;
	struct sockaddr_in6 bind_addr_ipv6;
	int iret = 0;
	if(!_is_ipv6)
		iret = uv_ip4_addr(_connect_ip.c_str(), _connect_port, &bind_addr_ipv4);
	else
		iret = uv_ip6_addr(_connect_ip.c_str(), _connect_port, &bind_addr_ipv6);
	
	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE("%s", errmsg_.c_str());
		return;
	}
	if(!_is_ipv6)
		iret = uv_tcp_connect(&_connect_req, &_tcp_client_ctx->tcphandle, (const sockaddr*)&bind_addr_ipv4, AfterConnect);
	else
		iret = uv_tcp_connect(&_connect_req, &_tcp_client_ctx->tcphandle, (const sockaddr*)&bind_addr_ipv6, AfterConnect);

	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE("%s", errmsg_.c_str());
	}
}

void TCPClient::ConnectThread(void* arg)
{
    TCPClient* pclient = (TCPClient*)arg;
	pclient->connectinl();
    pclient->run();
}

void TCPClient::SetReConnect(bool bReConn, int intervalTime/* = 3000*/)
{
	_is_reconnect = true;
	_reconn_interval_time = intervalTime;
}

void TCPClient::AfterConnect(uv_connect_t* handle, int status)
{
    TcpClientCtx* theclass = (TcpClientCtx*)handle->handle->data;
    TCPClient* parent = (TCPClient*)theclass->parent_server;
    if (status) {
        parent->_connect_status = CONNECT_ERROR;
        parent->errmsg_ = GetUVError(status);
        LOGE("client connect error:%s",  parent->errmsg_.c_str());

        if (parent->_is_reconnect) {
            uv_timer_stop(&parent->_reconn_timer_handle);
            uv_timer_start(&parent->_reconn_timer_handle, TCPClient::ReconnectTimer, parent->_reconn_interval_time, 0);
        }
		parent->_conn_cb(parent->_connect_status ,  parent->conn_userdata_);
        return;
    }
	int iret = uv_read_start(handle->handle, AllocBufferForRecv, AfterRecv);
	if (iret) {
        parent->errmsg_ = GetUVError(status);
        LOGE("client() uv_read_start error:%s", parent->errmsg_.c_str());
        parent->_connect_status = CONNECT_ERROR;
    } else {
        parent->_connect_status = CONNECT_FINISH;
        LOGI("clientid=%d connect to server ok", theclass->clientid);
    }
	//connect callback
	parent->_conn_cb(parent->_connect_status ,  parent->conn_userdata_);

    if (parent->_is_reconnect) {
        LOGI("reconnect succeed");
        parent->StopReconnect();
    }
}

int TCPClient::Send(const char* data, std::size_t len)
{
	if(_is_closed || !IsConnected())
		return 0;
	PushWriteEvent(data, len);
    return len;
}

void TCPClient::Close()
{
	if (_is_closed) {
		return;
	}
	PushCloseEvent();
}


void TCPClient::SetRecvCB(ClientRecvCB pfun, void* userdata)
{
    _recvcb_func = pfun;
    recvcb_userdata_ = userdata;
}

void TCPClient::SetClosedCB(TcpCloseCB pfun, void* userdata)
{
    closedcb_ = pfun;
    closedcb_userdata_ = userdata;
}

void TCPClient::SetConnectCB(ClientConnectCB pfun, void* userdata)
{
	_conn_cb = pfun;
	conn_userdata_ = userdata;
}


void TCPClient::AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    TcpClientCtx* theclass = (TcpClientCtx*)handle->data;
    assert(theclass);
    *buf = theclass->read_buf_;
}

void TCPClient::AfterRecv(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
    TcpClientCtx* theclass = (TcpClientCtx*)handle->data;
    assert(theclass);
    TCPClient* parent = (TCPClient*)theclass->parent_server;
    if (nread < 0) {
		parent->_connect_status = CONNECT_DIS;
		if (parent->_conn_cb) {
			parent->_conn_cb(parent->_connect_status, parent->conn_userdata_);
		}
        if (!parent->StartReconnect()) {
            LOGE("Start Reconnect Failure");
            return;
        }
        if (nread == UV_EOF) {
            LOGE("Server close(EOF) Client %p\n", handle);
        } else if (nread == UV_ECONNRESET) {
            LOGE("Server close(conn reset),Client %p\n", handle);
        } else {
            LOGE("Server close,Client %p:%s\n", handle, GetUVError(nread));
        }
        uv_close((uv_handle_t*)handle, AfterHandleClose);//close before reconnect
        return;
    }
    //parent->sendinl(NULL);
    if (nread > 0) {
        theclass->packet_->recvdata((const unsigned char*)buf->base, nread);
    }
}

void TCPClient::AfterSend(uv_write_t* req, int status)
{
    TCPClient* theclass = (TCPClient*)req->data;
    if (status < 0) {
        LOGE("send error:%s", GetUVError(status).c_str());
        fprintf(stderr, "send error %s\n", GetUVError(status));
    }
   FreeWriteParam((write_param*)req);
}

/* Fully close a loop */
void TCPClient::CloseWalkCB(uv_handle_t* handle, void* arg)
{
    TCPClient* theclass = (TCPClient*)arg;
    if (!uv_is_closing(handle)) {
        uv_close(handle, AfterHandleClose);
    }
}

void TCPClient::AfterHandleClose(uv_handle_t* handle)
{
    TCPClient* theclass = (TCPClient*)handle->data;
    fprintf(stdout, "Close CB handle %p\n", handle);
    if (handle == (uv_handle_t*)&theclass->_tcp_client_ctx->tcphandle && theclass->_is_reconnect) {
        //closed, start reconnect timer
        int iret = 0;
		uv_timer_stop(&theclass->_reconn_timer_handle);
        iret = uv_timer_start(&theclass->_reconn_timer_handle, TCPClient::ReconnectTimer, theclass->_reconn_interval_time, 0);
        if (iret) {
            uv_close((uv_handle_t*)&theclass->_reconn_timer_handle, TCPClient::AfterHandleClose);
            LOGE("%s", GetUVError(iret).c_str());
            return;
        }
    }
}

void TCPClient::GetPacket(const char* _buff, int _size, void* userdata)
{
    assert(userdata);
    TcpClientCtx* theclass = (TcpClientCtx*)userdata;
	NetPacket* pNetPacket = (NetPacket*)malloc(_size);
	std::memcpy(pNetPacket, _buff, _size);
    TCPClient* parent = (TCPClient*)theclass->parent_server;
    if (parent->_recvcb_func) {//cb the data to user
        parent->_recvcb_func(pNetPacket, parent->recvcb_userdata_);
    }
	free(pNetPacket);
}

void TCPClient::AsyncCB(uv_async_t* handle)
{
	TCPClient* theclass = (TCPClient*)handle->data;
	theclass->DoEvent();
}


void TCPClient::PushConnectEvent()
{
	CAutoLock locker(&_event_queue_mutex);
	_event_queue.push(CreateUvEvent(UV_EVENT_TYPE_CONNECT));
	uv_async_send(&_async_event_handle);
}

void TCPClient::PushCloseEvent()
{
	CAutoLock locker(&_event_queue_mutex);
	_event_queue.push(CreateUvEvent(UV_EVENT_TYPE_CLOSE));
	uv_async_send(&_async_event_handle);
}

int TCPClient::PushWriteEvent(const char* _buff, int _size)
{
	CAutoLock locker(&_event_queue_mutex);
	_event_queue.push(CreateUvEvent(UV_EVENT_TYPE_WRITE, _buff, _size));
	uv_async_send(&_async_event_handle);
	return _size;
}

void TCPClient::DoEvent()
{
	CAutoLock locker(&_event_queue_mutex);
	while(!_event_queue.empty())
	{
		UvEvent *pEvent = _event_queue.front();
		switch(pEvent->type)
		{
		case UV_EVENT_TYPE_CONNECT:
			{
				connectinl();
			}break;
		case UV_EVENT_TYPE_CLOSE:
			{
				closeinl();
			}break;
		case UV_EVENT_TYPE_WRITE:
			{
				sendinl(pEvent->_buff, pEvent->_size);
			}break;
		}
		_event_queue.pop();
		FreeUvEvent(pEvent);
	}
}

void TCPClient::sendinl(const char* _buff, int _size)
{
	write_param* writep = AllocWriteParam(_size);
	memcpy((char*)(writep->buf_.base), _buff, _size);
	//writep->buf_.len = _write_circularbuf.read(writep->buf_.base, writep->buf_.len); 
	int iret = uv_write((uv_write_t*)&writep->write_req_, (uv_stream_t*)&_tcp_client_ctx->tcphandle, &writep->buf_, 1, AfterSend);
	if (iret != 0) {
		FreeWriteParam(writep);
		LOGE("client() send error:%s", GetUVError(iret).c_str());
	}
}

bool TCPClient::StartReconnect(void)
{
    _is_reconnect = true;
    _tcp_client_ctx->tcphandle.data = this;
    _reconn_interval_time = 3000;//1 sec
    return true;
}

void TCPClient::StopReconnect(void)
{
    _is_reconnect = false;
    _tcp_client_ctx->tcphandle.data = _tcp_client_ctx;
    _reconn_interval_time = 3000;//1 sec
    uv_timer_stop(&_reconn_timer_handle);
}

void TCPClient::ReconnectTimer(uv_timer_t* handle)
{
    TCPClient* theclass = (TCPClient*)handle->data;
    if (!theclass->_is_reconnect) {
        return;
    }
    LOGI("start reconnect...\n");
    do {
        int iret = uv_tcp_init(&theclass->_loop, &theclass->_tcp_client_ctx->tcphandle);
        if (iret) {
            LOGE("%s", GetUVError(iret).c_str());
            break;
        }
        theclass->_tcp_client_ctx->tcphandle.data = theclass->_tcp_client_ctx;
        theclass->_tcp_client_ctx->parent_server = theclass;
        struct sockaddr* pAddr;
        if (theclass->_is_ipv6) {
            struct sockaddr_in6 bind_addr;
            int iret = uv_ip6_addr(theclass->_connect_ip.c_str(), theclass->_connect_port, &bind_addr);
            if (iret) {
                LOGE("%s", GetUVError(iret).c_str());
                uv_close((uv_handle_t*)&theclass->_tcp_client_ctx->tcphandle, NULL);
                break;
            }
            pAddr = (struct sockaddr*)&bind_addr;
        } else {
            struct sockaddr_in bind_addr;
            int iret = uv_ip4_addr(theclass->_connect_ip.c_str(), theclass->_connect_port, &bind_addr);
            if (iret) {
                LOGE("%s", GetUVError(iret).c_str());
                uv_close((uv_handle_t*)&theclass->_tcp_client_ctx->tcphandle, NULL);
                break;
            }
            pAddr = (struct sockaddr*)&bind_addr;
        }
        iret = uv_tcp_connect(&theclass->_connect_req, &theclass->_tcp_client_ctx->tcphandle, (const sockaddr*)pAddr, AfterConnect);
        if (iret) {
            LOGE("%s", GetUVError(iret).c_str());
            uv_close((uv_handle_t*)&theclass->_tcp_client_ctx->tcphandle, NULL);
            break;
        }
        return;
    } while (0);
    //reconnect failure, restart timer to trigger reconnect.
    uv_timer_stop(handle);
	uv_close((uv_handle_t*)handle, NULL);
    theclass->_reconn_interval_time *= 2;
    uv_timer_start(handle, TCPClient::ReconnectTimer, theclass->_reconn_interval_time, 0);
}

}