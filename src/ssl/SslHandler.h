#ifndef __SslHandler_H__
#define __SslHandler_H__

#include "kuma.h"
#include "evdefs.h"
#include "OpenSslLib.h"

struct iovec;

KUMA_NS_BEGIN

class SslHandler
{
public:
    enum SslState{
        SSL_NONE        = 0,
        SSL_HANDSHAKE   = 1,
        SSL_SUCCESS     = 2,
        SSL_ERROR       = -1,
    };
    
public:
    SslHandler();
    ~SslHandler();
    
    int attachFd(SOCKET_FD fd, bool is_server);
    SslState doSslHandshake();
    int send(uint8_t* data, uint32_t length);
    int send(iovec* iovs, uint32_t count);
    int receive(uint8_t* data, uint32_t length);
    int close();
    
    bool isServer() { return is_server_; }
    
    SslState getState() { return state_; }
    
protected:
    const char* getObjKey();
    
private:
    SslState sslConnect();
    SslState sslAccept();
    void setState(SslState state) { state_ = state; }
    void cleanup();
    
private:
    SOCKET_FD   fd_;
    SSL*        ssl_;
    SslState    state_;
    bool        is_server_;
};

KUMA_NS_END

#endif
