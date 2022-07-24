/* Copyright (c) 2014-2017, Fengping Bao <jamol@live.com>
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
#include "kmapi.h"
#include "libkev/src/utils/kmobject.h"
#include "libkev/src/utils/DestroyDetector.h"
#include "EventLoopImpl.h"

#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <sys/socket.h>
#endif

#ifdef KUMA_HAS_OPENSSL
#include "ssl/SslHandler.h"
#endif

#include <vector>
#include <mutex>

KUMA_NS_BEGIN
class SocketBase;

class TcpSocket::Impl : public kev::KMObject
{
public:
    using EventCallback = TcpSocket::EventCallback;
    
    Impl(const EventLoopPtr &loop);
    Impl(const Impl &other) = delete;
    Impl(Impl &&other);
    ~Impl();
    
    Impl& operator= (const Impl &other) = delete;
    Impl& operator= (Impl &&other);
    
    KMError setSslFlags(uint32_t ssl_flags);
    uint32_t getSslFlags() const { return ssl_flags_; }
    bool sslEnabled() const;
    KMError bind(const std::string &bind_host, uint16_t bind_port);
    KMError connect(const std::string &host, uint16_t port, EventCallback cb, uint32_t timeout_ms = 0);
    KMError attachFd(SOCKET_FD fd);
    KMError attach(Impl &&other);
    KMError detachFd(SOCKET_FD &fd);
#ifdef KUMA_HAS_OPENSSL
    KMError setAlpnProtocols(const AlpnProtos &protocols);
    KMError getAlpnSelected(std::string &protocol);
    KMError setSslServerName(std::string serverName);
    KMError attachFd(SOCKET_FD fd, SSL *ssl, BIO *nbio);
    KMError detachFd(SOCKET_FD &fd, SSL* &ssl, BIO* &nbio);
    KMError startSslHandshake(SslRole ssl_role, EventCallback cb=nullptr);
#endif
    int send(const void *data, size_t length);
    int send(const iovec *iovs, int count);
    int send(const KMBuffer &buf);
    int receive(void *data, size_t length);
    KMError close();
    
    KMError pause();
    KMError resume();
    
    void setReadCallback(EventCallback cb) { read_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    
    SOCKET_FD getFd() const;
    EventLoopPtr eventLoop() const;
    bool isReady() const;
    
private:
    void onConnect(KMError err);
    void onSend(KMError err);
    void onReceive(KMError err);
    void onClose(KMError err);
    
    bool createSocket();
#ifdef KUMA_HAS_OPENSSL
    bool createSslHandler();
    KMError checkSslHandshake(KMError err);
#endif
    int sendData(const void *data, size_t length);
    int sendData(const iovec *iovs, int count);
    int sendData(const KMBuffer &buf);
    int recvData(void *data, size_t length);
    
private:
    void cleanup();
    
private:
    EventLoopWeakPtr    loop_;
    uint32_t            ssl_flags_{ SSL_NONE };
    
    std::unique_ptr<SocketBase> socket_;
#ifdef KUMA_HAS_OPENSSL
    bool                is_bio_handler_ = false;
    std::unique_ptr<SslHandler> ssl_handler_;
    AlpnProtos          alpn_protos_;
    std::string         ssl_server_name_;
    std::string         ssl_host_name_;
#endif
    
    EventCallback       connect_cb_;
    EventCallback       read_cb_;
    EventCallback       write_cb_;
    EventCallback       error_cb_;
};

KUMA_NS_END

#endif
