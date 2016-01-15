/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __TcpSocketImpl_H__
#define __TcpSocketImpl_H__

#include "kmdefs.h"
#include "evdefs.h"

#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <sys/socket.h>
#endif

#ifdef KUMA_HAS_OPENSSL
#include "SslHandler.h"
#endif

KUMA_NS_BEGIN

class EventLoopImpl;

class TcpSocketImpl
{
public:
    typedef std::function<void(int)> EventCallback;
    
    TcpSocketImpl(EventLoopImpl* loop);
    ~TcpSocketImpl();
    
    int bind(const char* bind_host, uint16_t bind_port);
    int connect(const char* host, uint16_t port, const EventCallback& cb, uint32_t flags = 0, uint32_t timeout_ms = 0);
    int connect(const char* host, uint16_t port, EventCallback&& cb, uint32_t flags = 0, uint32_t timeout_ms = 0);
    int attachFd(SOCKET_FD fd, uint32_t flags);
    int detachFd(SOCKET_FD &fd);
#ifdef KUMA_HAS_OPENSSL
    int attachFd(SOCKET_FD fd, SSL* ssl, uint32_t flags);
    int detachFd(SOCKET_FD &fd, SSL* &ssl);
#endif
    int startSslHandshake(bool is_server);
    int send(const uint8_t* data, uint32_t length);
    int send(iovec* iovs, uint32_t count);
    int receive(uint8_t* data, uint32_t length);
    int close();
    
    int pause();
    int resume();
    
    void setReadCallback(const EventCallback& cb) { cb_read_ = cb; }
    void setWriteCallback(const EventCallback& cb) { cb_write_ = cb; }
    void setErrorCallback(const EventCallback& cb) { cb_error_ = cb; }
    void setReadCallback(EventCallback&& cb) { cb_read_ = std::move(cb); }
    void setWriteCallback(EventCallback&& cb) { cb_write_ = std::move(cb); }
    void setErrorCallback(EventCallback&& cb) { cb_error_ = std::move(cb); }
    
    SOCKET_FD getFd() const { return fd_; }
    
    uint32_t getFlags() const { return flags_; }
    
protected:
    const char* getObjKey() const;
    
private:
    int connect_i(const char* addr, uint16_t port, uint32_t timeout_ms);
    void setSocketOption();
    void ioReady(uint32_t events);
    void onConnect(int err);
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
private:
    enum State {
        ST_IDLE,
        ST_CONNECTING,
        ST_OPEN,
        ST_CLOSED
    };
    
    State getState() const { return state_; }
    void setState(State state) { state_ = state; }
    void cleanup();
    bool SslEnabled();
    bool isReady();
    
private:
    SOCKET_FD       fd_;
    EventLoopImpl*  loop_;
    State           state_;
    bool            registered_;
    bool*           destroy_flag_ptr_;
    uint32_t        flags_;
    
#ifdef KUMA_HAS_OPENSSL
    SslHandler*     ssl_handler_;
#endif
    
    EventCallback   cb_connect_;
    EventCallback   cb_read_;
    EventCallback   cb_write_;
    EventCallback   cb_error_;
};

KUMA_NS_END

#endif
