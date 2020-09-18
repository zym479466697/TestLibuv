#ifndef PACKET_SYNC_H
#define PACKET_SYNC_H
#include "uv_utils.h"

#define NET_PACKAGE_HEADLEN		3*sizeof(uint32_t)
#define DEF_PACK_HEAD_FLAG			0xD532D532 //协议头标志
#define PACK_BUFFER_SIZE (1024*4)

#pragma pack(push,1) 
typedef struct PackHeader
{
	uint32_t flag;
	uint32_t type;
	uint32_t dataSize;
	char data[1];
} NetPacket;
#pragma pack(pop)

typedef void (*GetFullPacket)(const char* _buff, int _size, void* userdata);


enum UV_EVENT_TYPE
{
	UV_EVENT_TYPE_CONNECT,
	UV_EVENT_TYPE_WRITE,
	UV_EVENT_TYPE_CLOSE,
	UV_EVENT_TYPE_BROADCAST,
};

struct	UvEvent
{
	UvEvent()
	{
		_buff = nullptr;
		_size = 0;
		_data = nullptr;
	}
	UV_EVENT_TYPE type;
	char* _buff;
	int _size;
	std::vector<int> _list;
	void* _data;
};

inline UvEvent* CreateUvEvent(UV_EVENT_TYPE type, const char* pBuff = nullptr, int size = 0)
{
	UvEvent* _event = new UvEvent;
	_event->type = type;
	if(pBuff){
		_event->_buff = (char*)malloc(size);
		memcpy(_event->_buff, pBuff, size);
		_event->_size = size;
	}
	return _event;
}

inline void FreeUvEvent(UvEvent* _event)
{
	if(_event->_buff){
		free(_event->_buff);
		_event->_buff = nullptr;
		_event->_size = 0;
		_event->_data = nullptr;
	}
	delete _event;
	_event = nullptr;
}


class PacketSync
{
public:
    PacketSync(): packet_cb_(NULL), packetcb_userdata_(NULL) {
        read_data = uv_buf_init((char*)malloc(PACK_BUFFER_SIZE), PACK_BUFFER_SIZE); //负责从circulebuffer_读取数据
        real_packet_len = 0;//readdata有效数据长度
    }
    virtual ~PacketSync() {
        free(read_data.base);
    }

    bool Start(uint32_t packhead) {
        HEAD = packhead;
        return true;
    }

public:
    void recvdata(const unsigned char* data, int len) { //接收到数据，把数据保存在circulebuffer_
        int iret = 0;
		bool hasPack = true;
		if(real_packet_len + len > read_data.len) 
		{
			read_data.base = (char*)realloc(read_data.base, real_packet_len + len);
			read_data.len = real_packet_len + len;
		}
		std::memcpy(read_data.base + real_packet_len, data, len);
		real_packet_len += len;

        while (hasPack &&  real_packet_len > NET_PACKAGE_HEADLEN) 
		{
			uint32_t head_flag = *(uint32_t*)(read_data.base);
			if(head_flag == HEAD)
			{
				uint32_t real_data_len = real_packet_len;
				uint32_t pack_size = *(uint32_t*)(read_data.base + 2*sizeof(uint32_t)) + NET_PACKAGE_HEADLEN;

				if(pack_size <= real_data_len)
				{
					//回调帧数据给用户
					if (this->packet_cb_) {
						//NetPacket* pNetPacket = (NetPacket*)malloc(pack_size);
						//std::memcpy(pNetPacket, read_data.base, pack_size);
						this->packet_cb_(read_data.base, pack_size, this->packetcb_userdata_);
					}
					std::memmove(read_data.base, read_data.base + pack_size, real_packet_len - pack_size);
					real_packet_len = real_packet_len - pack_size;//删除数据
				}
				else
				{
					hasPack = false;
				}
			}
			else
			{
				real_packet_len = 0;
			}
        }
    }

    void SetPacketCB(GetFullPacket pfun, void* userdata) {
        packet_cb_ = pfun;
        packetcb_userdata_ = userdata;
    }
private:
    GetFullPacket packet_cb_;//回调函数
    void*         packetcb_userdata_;//回调函数所带的自定义数据

    uv_buf_t  read_data;//负责从circulebuffer_读取packet 中data部分
    int real_packet_len;//readdata有效数据长度
	uint32_t HEAD;//包头
private:
    PacketSync(const PacketSync&);
    PacketSync& operator = (const PacketSync&);
};

typedef struct _write_param{//param of uv_write
	uv_write_t write_req_;//store TCPClient on data
	uv_buf_t buf_;
}write_param;

inline write_param* AllocWriteParam(int size)
{
	write_param* param = (write_param*)malloc(sizeof(*param));
	param->buf_.base = (char*)malloc(size);
	param->buf_.len = size;
	return param;
}

inline void FreeWriteParam(write_param* param)
{
	free(param->buf_.base);
	free(param);
}

//客户端或服务器关闭的回调函数
//服务器：当clientid为-1时，表现服务器的关闭事件
//客户端：clientid无效，永远为-1
typedef void (*TcpCloseCB)(int clientid, void* userdata);

//TCPServer接收到新客户端回调给用户
typedef void (*NewConnectCB)(int clientid, void* userdata);

//TCPServer接收到客户端数据回调给用户
typedef void (*TcpClientSessionRecvCB)(int clientid, const NetPacket& packethead, const unsigned char* buf, void* userdata);

//TCPClient接收到服务器数据回调给用户
typedef void (*ClientRecvCB)(NetPacket* pNetPacket, void* userdata);

typedef void (*ClientConnectCB)(int status, void* userdata);


#endif//PACKET_SYNC_H