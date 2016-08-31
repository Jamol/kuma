/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
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
#include "util/DestroyDetector.h"
#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <sys/socket.h>
#endif

#ifdef KUMA_HAS_OPENSSL
#include "ssl/SslHandler.h"
#endif

#include <vector>

KUMA_NS_BEGIN

class EventLoopImpl;

class TcpSocketImpl : public KMObject, public DestroyDetector
{
public:
    typedef std::function<void(KMError)> EventCallback;
    
    TcpSocketImpl(EventLoopImpl* loop);
    TcpSocketImpl(const TcpSocketImpl &other) = delete;
    TcpSocketImpl& operator= (const TcpSocketImpl &other) = delete;
    ~TcpSocketImpl();
    
    KMError setSslFlags(uint32_t ssl_flags);
    uint32_t getSslFlags() const { return ssl_flags_; }
    bool sslEnabled() const;
    KMError bind(const char* bind_host, uint16_t bind_port);
    KMError connect(const char* host, uint16_t port, EventCallback cb, uint32_t timeout_ms = 0);
    KMError attachFd(SOCKET_FD fd);
    KMError attach(TcpSocketImpl &&other);
    KMError detachFd(SOCKET_FD &fd);
#ifdef KUMA_HAS_OPENSSL
    KMError setAlpnProtocols(const AlpnProtos &protocols);
    KMError getAlpnSelected(std::string &proto);
    KMError setSslServerName(std::string serverName);
    KMError attachFd(SOCKET_FD fd, SSL* ssl);
    KMError detachFd(SOCKET_FD &fd, SSL* &ssl);
    KMError startSslHandshake(SslRole ssl_role);
#endif
    int send(const uint8_t* data, size_t length);
    int send(iovec* iovs, int count);
    int receive(uint8_t* data, size_t length);
    KMError close();
    
    KMError pause();
    KMError resume();
    
    void setReadCallback(EventCallback cb) { read_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    
    SOCKET_FD getFd() const { return fd_; }
    EventLoopImpl* getEventLoop() { return loop_; }
    
private:
    KMError connect_i(const char* addr, uint16_t port, uint32_t timeout_ms);
    void setSocketOption();
    void ioReady(uint32_t events);
    void onConnect(KMError err);
    void onSend(KMError err);
    void onReceive(KMError err);
    void onClose(KMError err);
    
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
    SOCKET_FD       fd_{ INVALID_FD };
    EventLoopImpl*  loop_;
    State           state_{ State::IDLE };
    bool            registered_{ false };
    uint32_t        ssl_flags_{ SSL_NONE };
    
#ifdef KUMA_HAS_OPENSSL
    SslHandler*     ssl_handler_{ nullptr };
    AlpnProtos      alpn_protos_;
    std::string     ssl_server_name;
#endif
    
    EventCallback   connect_cb_;
    EventCallback   read_cb_;
    EventCallback   write_cb_;
    EventCallback   error_cb_;
};

KUMA_NS_END

#endif
