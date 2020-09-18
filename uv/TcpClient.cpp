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

CTcpClient::CTcpClient(uint32_t packhead): _packet_head(packhead)
	, _recvcb_func(nullptr), recvcb_userdata_(nullptr), closedcb_(nullptr), closedcb_userdata_(nullptr)
	, _connect_status(CONNECT_DIS)
	, _is_closed(true)
	, _is_ipv6(false)
{
	_tcp_client_ctx = AllocTcpClientCtx(this);
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

	_tcp_client_ctx->tcphandle.data = _tcp_client_ctx;
	_tcp_client_ctx->parent_server = this;
	_tcp_client_ctx->packet_->SetPacketCB(GetPacket, _tcp_client_ctx);
	_tcp_client_ctx->packet_->Start(_packet_head);

	_is_closed = false;
	return true;
}

void CTcpClient::Connect(const char* ip, int port, bool isIPv6/* = false*/)
{
	_connect_ip = ip;
	_connect_port = port;
	_is_ipv6 = isIPv6;

	UvEvent* pEvent = CreateUvEvent(UV_EVENT_TYPE_CONNECT);
	pEvent->_data = this;
	_looper->PushEvent(pEvent);
}

int  CTcpClient::Send(const char* _buff, std::size_t _size)
{
	if(_is_closed || !IsConnected())
		return 0;

	UvEvent* pEvent = CreateUvEvent(UV_EVENT_TYPE_WRITE, _buff, _size);
	pEvent->_data = this;
	_looper->PushEvent(pEvent);
	return _size;
}

void CTcpClient::Close()
{
	UvEvent* pEvent = CreateUvEvent(UV_EVENT_TYPE_CLOSE);
	pEvent->_data = this;
	_looper->PushEvent(pEvent);
}

void CTcpClient::SetRecvCB(ClientRecvCB pfun, void* userdata)
{
	_recvcb_func = pfun;
	recvcb_userdata_ = userdata;
}

void CTcpClient::SetClosedCB(TcpCloseCB pfun, void* userdata)
{
	closedcb_ = pfun;
	closedcb_userdata_ = userdata;
}

void CTcpClient::SetConnectCB(ClientConnectCB pfun, void* userdata)
{
	_conn_cb = pfun;
	conn_userdata_ = userdata;
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
	if (_is_closed) {
		return;
	}
	_is_closed = true;
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
	TcpClientCtx* theclass = (TcpClientCtx*)handle->handle->data;
	CTcpClient* parent = (CTcpClient*)theclass->parent_server;
	if (status) {
		parent->_connect_status = CONNECT_ERROR;
		parent->errmsg_ = GetUVError(status);
		LOGE("client connect error:%s",  parent->errmsg_.c_str());
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
}


void CTcpClient::AfterRecv(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf)
{
	TcpClientCtx* ptrClientCtx = (TcpClientCtx*)handle->data;
	assert(ptrClientCtx);
	CTcpClient* parent = (CTcpClient*)ptrClientCtx->parent_server;
	if (nread < 0) {
		parent->_connect_status = CONNECT_DIS;
		if (parent->_conn_cb) {
			parent->_conn_cb(parent->_connect_status, parent->conn_userdata_);
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
		ptrClientCtx->packet_->recvdata((const unsigned char*)buf->base, nread);
	}
}

void CTcpClient::AfterSend(uv_write_t* req, int status)
{
	TcpClientCtx* ptrClientCtx = (TcpClientCtx*)req->data;
	if (status < 0) {
		LOGE("send error:%s", GetUVError(status).c_str());
		fprintf(stderr, "send error %s\n", GetUVError(status));
	}
	FreeWriteParam((write_param*)req);
}

void CTcpClient::AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
	TcpClientCtx* ptrClientCtx = (TcpClientCtx*)handle->data;
	assert(ptrClientCtx);
	*buf = ptrClientCtx->read_buf_;
}


void CTcpClient::AfterHandleClose(uv_handle_t* handle)
{
	TcpClientCtx* ptrClientCtx = (TcpClientCtx*)handle->data;
	fprintf(stdout, "Close CB handle %p\n", handle);
	if (handle == (uv_handle_t*)&ptrClientCtx->tcphandle) {
		
	}
}

void CTcpClient::GetPacket(const char* _buff, int _size, void* userdata)
{
	assert(userdata);
	TcpClientCtx* ptrClientCtx = (TcpClientCtx*)userdata;
	NetPacket* pNetPacket = (NetPacket*)malloc(_size);
	std::memcpy(pNetPacket, _buff, _size);
	CTcpClient* parent = (CTcpClient*)ptrClientCtx->parent_server;
	if (parent->_recvcb_func) {//cb the data to user
		parent->_recvcb_func(pNetPacket, parent->recvcb_userdata_);
	}
	free(pNetPacket);
}


}