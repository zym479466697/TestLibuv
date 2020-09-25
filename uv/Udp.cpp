#include "Udp.h"
#include <assert.h>

namespace uv
{
	UdpCtx* AllocUdpCtx(void* parentserver)
	{
		UdpCtx* ctx = (UdpCtx*)malloc(sizeof(*ctx));
		ctx->_read_buf.base = (char*)malloc(PACK_UDP_SIZE);
		ctx->_read_buf.len = PACK_UDP_SIZE;
		return ctx;
	}

	void FreeUdpCtx(UdpCtx* ctx)
	{
		free(ctx->_read_buf.base);
		free(ctx);
	}


	//=============
	CUvUdp::CUvUdp()
	{
		_loop = nullptr;
		_udp_ctx = AllocUdpCtx(this);
		_udp_ctx->_udp_handle.data = this;
	}

	CUvUdp::~CUvUdp()
	{
		Close();
		FreeUdpCtx(_udp_ctx);
		_udp_ctx= nullptr;
	}

	bool CUvUdp::AttachLooper(CLooper *_lp)
	{
		_loop = _lp;
		int _ret = uv_udp_init(_loop->GetLooper(), &_udp_ctx->_udp_handle);
		if (_ret) {
			errmsg_ = GetUVError(_ret);
			LOGE("%s", errmsg_.c_str());
			return false;
		}
		return true;
	}

	void CUvUdp::OnRecvCBEvent(std::function<void(const char*, int size, const struct sockaddr* pAddr, unsigned iFlag, void*)> func_recv)
	{
		_func_recv = func_recv;
	}

	void CUvUdp::AfterRecv(uv_udp_t* pHandle, ssize_t nRead, const uv_buf_t* pBuf, const struct sockaddr* pAddr, unsigned iFlag) {
		if (nRead <= 0){
			return;
		}
		CUvUdp* theClass = (CUvUdp*)pHandle->data;
		if (theClass->_func_recv) {
			theClass->_func_recv(pBuf->base, nRead, pAddr, iFlag, theClass);
		}
	}

	void CUvUdp::AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
	{
		CUvUdp* theClass = (CUvUdp*)handle->data;
		UdpCtx* _udp_ctx = theClass->_udp_ctx;
		assert(_udp_ctx);
		*buf = _udp_ctx->_read_buf;
	}

	int CUvUdp::Send(const char* buffer, int size, const std::string& strIp, unsigned short uPort, bool isIPv6) 
	{
		assert(size <= PACK_UDP_SIZE);
		struct sockaddr_in bind_addr_ipv4;
		struct sockaddr_in6 bind_addr_ipv6;
		int iret = 0;
		if(!isIPv6)
		{
			iret = uv_ip4_addr(strIp.c_str(), uPort, &bind_addr_ipv4);
			return Send(buffer, size, (const struct sockaddr*)&bind_addr_ipv4);
		}
		else
		{
			iret = uv_ip6_addr(strIp.c_str(), uPort, &bind_addr_ipv6);
			return Send(buffer, size, (const struct sockaddr*)&bind_addr_ipv6);
		}
		return 0;
	}

	int CUvUdp::Send(const char* buffer, int size, const struct sockaddr* pAddr)
	{
		assert(size <= PACK_UDP_SIZE);
		if(_loop){
			UvEvent* pEvent = CreateUvEvent(UV_EVENT_TYPE_WRITE, buffer, size);
			pEvent->_addr = *pAddr;
			pEvent->_data = this;
			_loop->PushEvent(pEvent);
			return size;
		}
		return 0;
	}

	void CUvUdp::AfterSend(uv_udp_send_t* pReq, int iStatus) {
		CUvUdp* theClass = (CUvUdp*)pReq->data;
		if (iStatus < 0) {
			LOGE("send error:%s", GetUVError(iStatus).c_str());
		}
		FreeSendParam((send_param*)pReq);
	}


	bool CUvUdp::Bind(const char* ip, int port, bool isIPv6) 
	{
		struct sockaddr_in bind_addr_ipv4;
		struct sockaddr_in6 bind_addr_ipv6;
		int iret = 0;
		if(!isIPv6)
			iret = uv_ip4_addr(ip, port, &bind_addr_ipv4);
		else
			iret = uv_ip6_addr(ip, port, &bind_addr_ipv6);

		if(!isIPv6)
			iret = uv_ip4_addr(ip, port, &bind_addr_ipv4);
		else
			iret = uv_ip6_addr(ip, port, &bind_addr_ipv6);

		if (port > 0) {
			if(!isIPv6)
				iret = uv_udp_bind(&_udp_ctx->_udp_handle, (struct sockaddr*)&bind_addr_ipv4, UV_UDP_REUSEADDR);
			else
				iret = uv_udp_bind(&_udp_ctx->_udp_handle, (struct sockaddr*)&bind_addr_ipv6, UV_UDP_REUSEADDR);
		}
		else {
			iret = uv_udp_set_broadcast(&_udp_ctx->_udp_handle, 1);
		}

		if (iret) {
			errmsg_ = GetUVError(iret);
			LOGE("%s", errmsg_.c_str());
			return false;
		}

		iret = uv_udp_recv_start(&_udp_ctx->_udp_handle, AllocBufferForRecv, CUvUdp::AfterRecv);
		if (iret) {
			errmsg_ = GetUVError(iret);
			LOGE("%s", errmsg_.c_str());
			return false;
		}
		return true;
	}

	void CUvUdp::sendinl(const struct sockaddr* addr, const char* _buff, int _size)
	{
		send_param* sendp = AllocSendParam(_size);
		sendp->send_req_.data = this;
		std::memcpy((char*)(sendp->buf_.base), _buff, _size);

		int iret = uv_udp_send(&sendp->send_req_, &_udp_ctx->_udp_handle, &sendp->buf_, 1, (struct sockaddr*)addr, CUvUdp::AfterSend);
		if (iret != 0) {
			FreeSendParam(sendp);
			LOGE("client() send error:%s", GetUVError(iret).c_str());
		}
	}

	void CUvUdp::DoEvent(UvEvent* pEvent)
	{
		switch(pEvent->type)
		{
		case UV_EVENT_TYPE_WRITE:
			{
				sendinl(&pEvent->_addr, pEvent->_buff, pEvent->_size);
			}break;
		}
		FreeUvEvent(pEvent);
	}

	void CUvUdp::OnHandleClose(uv_handle_t* handle)
	{
		if (!uv_is_closing(handle)){
			uv_close(handle, NULL);
		}
	}

	void CUvUdp::Close() {
		if(_loop){
			_loop->Close();
		}
	}
}