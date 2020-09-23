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
    ctx->write_req.data = parentserver;//store self
    return ctx;
}

void FreeTcpClientCtx(TcpClientCtx* ctx)
{
    delete ctx->packet_;
    free(ctx->read_buf_.base);
    free(ctx);
}

int CTcpClient::s_base_client_id = 1000;
CTcpClient::CTcpClient(uint32_t packhead): _packet_head(packhead)
	, _connect_status(TCP_STATUS_NONE)
	, _is_wait_closed(true)
	, _is_ipv6(false)
{
	_tcp_client_ctx = AllocTcpClientCtx(this);
	_tcp_client_ctx->clientid = CTcpClient::s_base_client_id++;
}

CTcpClient::~CTcpClient()
{
	Close();
	FreeTcpClientCtx(_tcp_client_ctx);
}

bool CTcpClient::Init(CLooper *_lp)
{
	_looper = _lp;
	int iret = uv_tcp_init(_looper->GetLooper(), &_tcp_client_ctx->tcphandle);
	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE("%s", errmsg_.c_str());
		return false;
	}

	_tcp_client_ctx->tcphandle.data = this;
	_tcp_client_ctx->packet_->Init(_packet_head);
	_tcp_client_ctx->packet_->OnPackageCBEvent([this](const char* _buff, int _size){

		CTcpClient* theClass = (CTcpClient*)_tcp_client_ctx->write_req.data;
		NetPacket* pNetPacket = (NetPacket*)malloc(_size);
		std::memcpy(pNetPacket, _buff, _size);
		if (theClass->_func_recv_cb) {//cb the data to user
			theClass->_func_recv_cb(pNetPacket, (void*)theClass);
		}
		free(pNetPacket);
	});

	_is_wait_closed = false;
	return true;
}

bool CTcpClient::Connect(const char* ip, int port, bool isIPv6)
{
	if(_connect_status != TCP_STATUS_NONE 
		&& _connect_status != TCP_STATUS_CONNECT_ERROR)
	{
		return false;
	}

	if(_is_wait_closed)
		return false;

	_connect_ip = ip;
	_connect_port = port;
	_is_ipv6 = isIPv6;
	_connect_status = TCP_STATUS_NONE;
	_is_wait_closed = false;
	if(_looper){
		_connect_status = TCP_STATUS_CONNECTING;
		UvEvent* pEvent = CreateUvEvent(UV_EVENT_TYPE_CONNECT);
		pEvent->_data = this;
		_looper->PushEvent(pEvent);
	}
	return true;
}

void CTcpClient::OnConnectCBEvent(std::function<void(int, void*)> func_conn)
{
	_func_conn_cb = func_conn;
}

void CTcpClient::OnRecvCBEvent(std::function<void(NetPacket*, void*)> func_recv)
{
	_func_recv_cb = func_recv;
}

void CTcpClient::OnCloseCBEvent(std::function<void(int, void*)> func_close)
{
	_func_close_cb = func_close;
}

int  CTcpClient::Send(const char* _buff, std::size_t _size)
{
	if(_is_wait_closed || !IsConnected())
		return 0;

	if(_looper){
		UvEvent* pEvent = CreateUvEvent(UV_EVENT_TYPE_WRITE, _buff, _size);
		pEvent->_data = this;
		_looper->PushEvent(pEvent);
		return _size;
	}
	return 0;
}


bool CTcpClient::SetNoDelay(bool enable)
{
	int iret = uv_tcp_nodelay(&_tcp_client_ctx->tcphandle, enable ? 1 : 0);
	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE("uv_tcp_nodelay %s", errmsg_.c_str());
		return false;
	}
	return true;
}

bool CTcpClient::SetKeepAlive(int enable, unsigned int delay)
{
	int iret = uv_tcp_keepalive(&_tcp_client_ctx->tcphandle, enable , delay);
	if (iret) {
		errmsg_ = GetUVError(iret);
		LOGE("uv_tcp_keepalive %s", errmsg_.c_str());
		return false;
	}
	return true;
}

void CTcpClient::Close()
{
	if (_connect_status != TCP_STATUS_CONNECTED 
		&&  _connect_status !=TCP_STATUS_CONNECTING) {
		return;
	}

	if(_looper){
		UvEvent* pEvent = CreateUvEvent(UV_EVENT_TYPE_CLOSE);
		pEvent->_data = this;
		_looper->PushEvent(pEvent);
	}
}

void CTcpClient::DoEvent(UvEvent* pEvent)
{
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
	FreeUvEvent(pEvent);
}

void CTcpClient::OnHandleClose(uv_handle_t* handle)
{
	if (!uv_is_closing(handle)){
		uv_close(handle, AfterHandleClose);
	}
}

void CTcpClient::connectinl()
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

void CTcpClient::closeinl()
{
	if (_connect_status != TCP_STATUS_CONNECTED 
		&&  _connect_status !=TCP_STATUS_CONNECTING) {
		return;
	}
	if(_is_wait_closed)
		return;
	_is_wait_closed = true;
	uv_handle_t* tcp_handle = (uv_handle_t*)&_tcp_client_ctx->tcphandle;
	if (!uv_is_closing(tcp_handle)){
		uv_close(tcp_handle, AfterHandleClose);
	}
}

void CTcpClient::sendinl(const char* _buff, int _size)
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


void CTcpClient::AfterConnect(uv_connect_t* handle, int status)
{
	CTcpClient* theClass = (CTcpClient*)handle->handle->data;
	if (status) {
		theClass->_connect_status = TCP_STATUS_CONNECT_ERROR;
		theClass->errmsg_ = GetUVError(status);
		LOGE("client connect error:%s",  theClass->errmsg_.c_str());
		//parent->_conn_cb(parent->_connect_status ,  parent->conn_userdata_);
		if(theClass->_func_conn_cb)
			theClass->_func_conn_cb(theClass->_connect_status ,  (void*)theClass);
		return;
	}
	int iret = uv_read_start(handle->handle, AllocBufferForRecv, AfterRecv);
	if (iret) {
		theClass->errmsg_ = GetUVError(status);
		LOGE("client() uv_read_start error:%s", theClass->errmsg_.c_str());
		theClass->_connect_status = TCP_STATUS_CONNECT_ERROR;
	} else {
		theClass->_connect_status = TCP_STATUS_CONNECTED;
		//LOGI("clientid=%d connect to server ok", theclass->clientid);
	}
	//connect callback
	//parent->_conn_cb(parent->_connect_status ,  parent->conn_userdata_);
	if(theClass->_func_conn_cb)
		theClass->_func_conn_cb(theClass->_connect_status ,  (void*)theClass);
}


void CTcpClient::AfterRecv(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
	CTcpClient* theClass = (CTcpClient*)handle->data;
	assert(theClass);
	if (nread < 0) {
		theClass->_connect_status = TCP_STATUS_NONE;
		if(theClass->_func_conn_cb)
			theClass->_func_conn_cb(theClass->_connect_status ,  (void*)theClass);

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
	
	if (nread > 0) {
		theClass->_tcp_client_ctx->packet_->OnRecvData((const unsigned char*)buf->base, nread);
	}
}

void CTcpClient::AfterSend(uv_write_t* req, int status)
{
	CTcpClient* theClass = (CTcpClient*)req->data;
	if (status < 0) {
		LOGE("send error:%s", GetUVError(status).c_str());
		fprintf(stderr, "send error %s\n", GetUVError(status));
	}
	FreeWriteParam((write_param*)req);
}

void CTcpClient::AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	CTcpClient* theClass = (CTcpClient*)handle->data;
	TcpClientCtx* ptrClientCtx = theClass->_tcp_client_ctx;
	assert(ptrClientCtx);
	*buf = ptrClientCtx->read_buf_;
}


void CTcpClient::AfterHandleClose(uv_handle_t* handle)
{
	CTcpClient* theClass = (CTcpClient*)handle->data;
	TcpClientCtx* ptrClientCtx = theClass->_tcp_client_ctx;
	if (handle == (uv_handle_t*)&ptrClientCtx->tcphandle) {
		if(theClass->_func_close_cb)
			theClass->_func_close_cb(theClass->GetClientId() ,  (void*)theClass);
		theClass->_connect_status = TCP_STATUS_NONE;
		theClass->_is_wait_closed = false;
	}
}

}