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
#include "util/kmobject.h"
#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <sys/socket.h>
#endif

#ifdef KUMA_HAS_OPENSSL
#include "SslHandler.h"
#endif

#include <vector>

KUMA_NS_BEGIN

class EventLoopImpl;

class TcpSocketImpl : public KMObject
{
public:
    typedef std::function<void(int)> EventCallback;
    
    TcpSocketImpl(EventLoopImpl* loop);
    TcpSocketImpl(const TcpSocketImpl &other) = delete;
    TcpSocketImpl& operator= (const TcpSocketImpl &other) = delete;
    ~TcpSocketImpl();
    
    int setSslFlags(uint32_t ssl_flags);
    uint32_t getSslFlags() const { return ssl_flags_; }
    bool SslEnabled();
    int bind(const char* bind_host, uint16_t bind_port);
    int connect(const char* host, uint16_t port, EventCallback cb, uint32_t timeout_ms = 0);
    int attachFd(SOCKET_FD fd);
    int attach(TcpSocketImpl &&other);
    int detachFd(SOCKET_FD &fd);
#ifdef KUMA_HAS_OPENSSL
    int setAlpnProtocols(const AlpnProtos &protocols);
    int getAlpnSelected(std::string &proto);
    int attachFd(SOCKET_FD fd, SSL* ssl);
    int detachFd(SOCKET_FD &fd, SSL* &ssl);
    int startSslHandshake(SslRole ssl_role);
#endif
    int send(const uint8_t* data, size_t length);
    int send(iovec* iovs, int count);
    int receive(uint8_t* data, size_t length);
    int close();
    
    int pause();
    int resume();
    
    void setReadCallback(EventCallback cb) { read_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    
    SOCKET_FD getFd() const { return fd_; }
    
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
        IDLE,
        CONNECTING,
        OPEN,
        CLOSED
    };
    
    State getState() const { return state_; }
    void setState(State state) { state_ = state; }
    void cleanup();
    bool isReady();
    
private:
    SOCKET_FD       fd_ = INVALID_FD;
    EventLoopImpl*  loop_;
    State           state_ = State::IDLE;
    bool            registered_ = false;
    uint32_t        ssl_flags_ = SSL_NONE;
    
#ifdef KUMA_HAS_OPENSSL
    SslHandler*     ssl_handler_ = nullptr;
    AlpnProtos      alpn_protos_;
#endif
    
    EventCallback   connect_cb_;
    EventCallback   read_cb_;
    EventCallback   write_cb_;
    EventCallback   error_cb_;
    
    bool*           destroy_flag_ptr_ = nullptr;
};

KUMA_NS_END

#endif
