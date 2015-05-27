#ifndef __TcpServerSocket_H__
#define __TcpServerSocket_H__

#include "kuma.h"
#include "evdefs.h"

KUMA_NS_BEGIN

class EventLoop;

class TcpServerSocket
{
public:
    typedef std::function<void(SOCKET_FD)> EventCallback;
    
    TcpServerSocket(EventLoop* loop);
    ~TcpServerSocket();
    
    int startListen(const char* addr, uint16_t port);
    int stopListen(const char* addr, uint16_t port);
    int close();
    
    SOCKET_FD getFd() { return fd_; }
    
protected:
    const char* getObjKey();
    
private:
    void setSocketOption();
    void ioReady(uint32_t events);
    void onAccept();
    void onClose(int err);
    
private:
    enum State {
        ST_IDLE,
        ST_ACCEPTING,
        ST_CLOSED
    };
    
    State getState() { return state_; }
    void setState(State st) { state_ = st; }
    void cleanup();
    
private:
    SOCKET_FD   fd_;
    EventLoop*  loop_;
    State       state_;
    bool        registered_;
    uint32_t    flags_;
};

KUMA_NS_END

#endif
