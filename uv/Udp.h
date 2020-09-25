#ifndef CVUDP_H
#define CVUDP_H
#include <string>
#include <list>
#include <queue>
#include "uv.h"
#include <functional>
#include "Packet.h"
#include "Looper.h"

namespace uv
{
	typedef struct _uv_udp_ctx {
		uv_udp_t _udp_handle;//store this on data
		uv_buf_t _read_buf;
	} UdpCtx;

	UdpCtx* AllocUdpCtx(void* parentserver);
	void FreeUdpCtx(UdpCtx* ctx);

	class CUvUdp :  public ILooperEvent{
	public:
		CUvUdp();
		virtual ~CUvUdp();
		bool AttachLooper(CLooper *_lp);
		bool Bind(const char* ip = "0.0.0.0", int port = 0, bool isIPv6 = false);
		int Send(const char* buffer, int size, const std::string& strIp, unsigned short uPort, bool isIPv6 = false);
		int Send(const char* buffer, int size, const struct sockaddr* pAddr);
		void OnRecvCBEvent(std::function<void(const char*, int size, const struct sockaddr* pAddr, unsigned iFlag, void*)> func_recv);
		void Close();

		const char* GetLastErrMsg() const {
			return errmsg_.c_str();
		};
	protected:
		static void AfterRecv(uv_udp_t* pHandle, ssize_t nRead, const uv_buf_t* pBuf, const struct sockaddr* pAddr, unsigned iFlag);
		static void AfterSend(uv_udp_send_t* pReq, int iStatus);
		static void AllocBufferForRecv(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);

		virtual void DoEvent(UvEvent *);
		virtual void OnHandleClose(uv_handle_t *);
	private:
		void sendinl(const struct sockaddr* addr, const char* _buff, int _size);
	protected:
		UdpCtx* _udp_ctx;
		std::string errmsg_;
	private:
		CLooper* _loop;
		std::function<void(const char*, int size, const struct sockaddr* pAddr, unsigned iFlag, void*)> _func_recv;
	};

}

#endif // CVUDP_H