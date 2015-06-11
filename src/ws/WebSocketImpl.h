#ifndef __WebSocketImpl_H__
#define __WebSocketImpl_H__

#include "kmdefs.h"
#include "WSHandler.h"
#include "TcpSocketImpl.h"
#include "Uri.h"

KUMA_NS_BEGIN

class WebSocketImpl
{
public:
    typedef std::function<void(uint8_t*, uint32_t)> DataCallback;
    typedef std::function<void(int)> EventCallback;
    
    WebSocketImpl(EventLoopImpl* loop);
    ~WebSocketImpl();
    
    int connect(const char* ws_url, const char* proto, const char* origin, EventCallback& cb);
    int connect(const char* ws_url, const char* proto, const char* origin, EventCallback&& cb);
    int attachFd(SOCKET_FD fd, uint8_t* init_data = nullptr, uint32_t init_len = 0);
    int send(uint8_t* data, uint32_t len);
    int close();
    
    void setDataCallback(DataCallback& cb) { cb_data_ = cb; }
    void setWriteCallback(EventCallback& cb) { cb_write_ = cb; }
    void setErrorCallback(EventCallback& cb) { cb_error_ = cb; }
    void setDataCallback(DataCallback&& cb) { cb_data_ = std::move(cb); }
    void setWriteCallback(EventCallback&& cb) { cb_write_ = std::move(cb); }
    void setErrorCallback(EventCallback&& cb) { cb_error_ = std::move(cb); }
    
protected: // callbacks of tcp_socket
    void onConnect(int err);
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
protected:
    const char* getObjKey();

private:
    enum State {
        STATE_IDLE,
        STATE_CONNECTING,
        STATE_HANDSHAKE,
        STATE_OPEN,
        STATE_ERROR,
        STATE_CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    int connect_i(const char* ws_url);
    void cleanup();
    
    void sendWsResponse();
    void onWsData(uint8_t* data, uint32_t len);
    void onWsHandshake(int err);
    void onStateOpen();
    
private:
    State                   state_;
    WSHandler               ws_handler_;
    Uri                     uri_;
    std::vector<uint8_t>    send_buffer_;
    uint32_t                send_offset_;
    TcpSocketImpl           tcp_socket_;
    bool                    is_server_;
    
    uint32_t                body_bytes_sent_;
    
    std::string             proto_;
    std::string             origin_;
    DataCallback            cb_data_;
    EventCallback           cb_connect_;
    EventCallback           cb_write_;
    EventCallback           cb_error_;
    
    bool*                   destroy_flag_ptr_;
};

KUMA_NS_END

#endif
