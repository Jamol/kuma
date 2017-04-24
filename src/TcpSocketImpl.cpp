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

#include "kmconf.h"

#if defined(KUMA_OS_WIN)
# include <Ws2tcpip.h>
# include <windows.h>
# include <time.h>
#elif defined(KUMA_OS_LINUX)
# include <string.h>
# include <pthread.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/time.h>
# include <sys/socket.h>
# include <netdb.h>
# include <arpa/inet.h>
# include <netinet/tcp.h>
# include <netinet/in.h>
# ifdef KUMA_OS_ANDROID
#  include <sys/uio.h>
# endif
#elif defined(KUMA_OS_MAC)
# include <string.h>
# include <pthread.h>
# include <unistd.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/ioctl.h>
# include <sys/fcntl.h>
# include <sys/time.h>
# include <sys/uio.h>
# include <netinet/tcp.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <ifaddrs.h>
#else
# error "UNSUPPORTED OS"
#endif

#include <stdarg.h>
#include <errno.h>

#include "EventLoopImpl.h"
#include "TcpSocketImpl.h"
#include "util/util.h"
#include "util/kmtrace.h"
#include "DnsResolver.h"

#ifdef KUMA_HAS_OPENSSL
# include "ssl/SslHandler.h"
#endif

using namespace kuma;

TcpSocket::Impl::Impl(const EventLoopPtr &loop)
: loop_(loop)
{
    KM_SetObjKey("TcpSocket");
}

TcpSocket::Impl::~Impl()
{
    cleanup();
}

void TcpSocket::Impl::cleanup()
{
    if (!dns_token_.expired()) {
        DnsResolver::get().cancel("", dns_token_);
        dns_token_.reset();
    }
#ifdef KUMA_HAS_OPENSSL
    if(ssl_handler_) {
        ssl_handler_->close();
        delete ssl_handler_;
        ssl_handler_ = nullptr;
    }
#endif
    if(INVALID_FD != fd_) {
        SOCKET_FD fd = fd_;
        fd_ = INVALID_FD;
        shutdown(fd, 0); // only stop receive
        if(registered_) {
            registered_ = false;
            auto loop = loop_.lock();
            if (loop) {
                loop->unregisterFd(fd, true);
            }
        } else {
            closeFd(fd);
        }
    }
}

KMError TcpSocket::Impl::setSslFlags(uint32_t ssl_flags)
{
#ifdef KUMA_HAS_OPENSSL
    ssl_flags_ = ssl_flags;
    return KMError::NOERR;
#else
    //KUMA_ERRXTRACE("setSslFlags, OpenSSL is not enabled, please define KUMA_HAS_OPENSSL and recompile");
    return KMError::UNSUPPORT;
#endif
}

KMError TcpSocket::Impl::bind(const char *bind_host, uint16_t bind_port)
{
    KUMA_INFOTRACE("bind, bind_host="<<bind_host<<", bind_port="<<bind_port);
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("bind, invalid state, state="<<getState());
        return KMError::INVALID_STATE;
    }
    if(fd_ != INVALID_FD) {
        cleanup();
    }
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST;//AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(km_set_sock_addr(bind_host, bind_port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
        return KMError::INVALID_PARAM;
    }
    fd_ = ::socket(ss_addr.ss_family, SOCK_STREAM, 0);
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("bind, socket failed, err="<<getLastError());
        return KMError::FAILED;
    }
    int addr_len = km_get_addr_length(ss_addr);
    int ret = ::bind(fd_, (struct sockaddr*)&ss_addr, addr_len);
    if(ret < 0) {
        KUMA_ERRXTRACE("bind, bind failed, err="<<getLastError());
        return KMError::FAILED;
    }
    return KMError::NOERR;
}

KMError TcpSocket::Impl::connect(const char *host, uint16_t port, EventCallback cb, uint32_t timeout_ms)
{
    KUMA_INFOXTRACE("connect, host="<<host<<", port="<<port<<", this="<<this);
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("connect, invalid state, state="<<getState());
        return KMError::INVALID_STATE;
    }
    connect_cb_ = std::move(cb);
    if (!km_is_ip_address(host)) {
#ifdef KUMA_HAS_OPENSSL
        if (sslEnabled()) {
            ssl_host_name_ = host;
        }
#endif
        sockaddr_storage ss_addr = {0};
        if (DnsResolver::get().getAddress(host, ss_addr) == KMError::NOERR) {
            return connect_i(ss_addr, timeout_ms);
        }
        setState(HOST_RESOLVING);
        dns_token_ = DnsResolver::get().resolve(host, port, [this](KMError err, const sockaddr_storage &addr) {
            onResolved(err, addr);
        });
        return KMError::NOERR;
    }
    return connect_i(host, port, timeout_ms);
}

KMError TcpSocket::Impl::connect_i(const char* host, uint16_t port, uint32_t timeout_ms)
{
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST|AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(km_set_sock_addr(host, port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
        auto err = getLastError();
        KUMA_ERRXTRACE("connect_i, DNS resolving failure, host="<<host<<", err="<<err);
        return KMError::INVALID_PARAM;
    }
    return connect_i(ss_addr, timeout_ms);
}

KMError TcpSocket::Impl::connect_i(const sockaddr_storage &ss_addr, uint32_t timeout_ms)
{
#ifndef KUMA_HAS_OPENSSL
    if (sslEnabled()) {
        KUMA_ERRXTRACE("connect_i, OpenSSL is disabled");
        return KMError::UNSUPPORT;
    }
#endif
    
    if(INVALID_FD == fd_) {
        fd_ = ::socket(ss_addr.ss_family, SOCK_STREAM, 0);
        if(INVALID_FD == fd_) {
            KUMA_ERRXTRACE("connect_i, socket failed, err="<<getLastError());
            return KMError::FAILED;
        }
    }
    setSocketOption();
    
    int addr_len = km_get_addr_length(ss_addr);
    int ret = ::connect(fd_, (struct sockaddr *)&ss_addr, addr_len);
    if(0 == ret) {
        setState(State::CONNECTING); // wait for writable event
    } else if(ret < 0 &&
#ifdef KUMA_OS_WIN
              WSAEWOULDBLOCK
#else
              EINPROGRESS
#endif
              == getLastError()) {
        setState(State::CONNECTING);
    } else {
        KUMA_ERRXTRACE("connect_i, error, fd="<<fd_<<", err="<<getLastError());
        cleanup();
        setState(State::CLOSED);
        return KMError::FAILED;
    }

#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
    socklen_t len = sizeof(ss_addr);
#else
    int len = sizeof(ss_addr);
#endif
    char local_ip[128] = {0};
    uint16_t local_port = 0;
    ret = getsockname(fd_, (struct sockaddr*)&ss_addr, &len);
    if(ret != -1) {
        km_get_sock_addr((struct sockaddr*)&ss_addr, sizeof(ss_addr), local_ip, sizeof(local_ip), &local_port);
    }
    
    KUMA_INFOXTRACE("connect_i, fd="<<fd_<<", local_ip="<<local_ip
                   <<", local_port="<<local_port<<", state="<<getState());
    
    auto loop = loop_.lock();
    if (loop) {
        loop->registerFd(fd_, KUMA_EV_NETWORK, [this] (uint32_t ev) { ioReady(ev); });
        registered_ = true;
    }
    
    return KMError::NOERR;
}

KMError TcpSocket::Impl::attachFd(SOCKET_FD fd)
{
    KUMA_INFOXTRACE("attachFd, fd="<<fd<<", flags="<<ssl_flags_<<", state="<<getState());
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("attachFd, invalid state, state="<<getState());
        return KMError::INVALID_STATE;
    }
#ifndef KUMA_HAS_OPENSSL
    if (sslEnabled()) {
        KUMA_ERRXTRACE("attachFd, OpenSSL is disabled");
        return KMError::UNSUPPORT;
    }
#endif
    
    fd_ = fd;
    setSocketOption();
    setState(State::OPEN);
#ifdef KUMA_HAS_OPENSSL
    if(sslEnabled()) {
        auto ret = startSslHandshake(SslRole::SERVER);
        if(ret != KMError::NOERR) {
            return ret;
        }
    }
#endif
    auto loop = loop_.lock();
    if (loop) {
        loop->registerFd(fd_, KUMA_EV_NETWORK, [this] (uint32_t ev) { ioReady(ev); });
        registered_ = true;
    }
    return KMError::NOERR;
}

KMError TcpSocket::Impl::attach(Impl &&other)
{
#ifdef KUMA_HAS_OPENSSL
    SOCKET_FD fd;
    SSL* ssl = nullptr;
    uint32_t sslFlags = other.getSslFlags();
    auto ret = other.detachFd(fd, ssl);
    setSslFlags(sslFlags);
    ret = attachFd(fd, ssl);
#else
    SOCKET_FD fd;
    auto ret = other.detachFd(fd);
    ret = attachFd(fd);
#endif
    return ret;
}

KMError TcpSocket::Impl::detachFd(SOCKET_FD &fd)
{
    KUMA_INFOXTRACE("detachFd, fd="<<fd_<<", state="<<getState());
    fd = fd_;
    fd_ = INVALID_FD;
    if(registered_) {
        registered_ = false;
        auto loop = loop_.lock();
        if (loop) {
            loop->unregisterFd(fd, false);
        }
    }
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

#ifdef KUMA_HAS_OPENSSL
KMError TcpSocket::Impl::setAlpnProtocols(const AlpnProtos &protocols)
{
    alpn_protos_ = protocols;
    return KMError::NOERR;
}

KMError TcpSocket::Impl::getAlpnSelected(std::string &proto)
{
    if (!sslEnabled()) {
        return KMError::INVALID_PROTO;
    }
    if (ssl_handler_) {
        return ssl_handler_->getAlpnSelected(proto);
    } else {
        return KMError::INVALID_STATE;
    }
}

KMError TcpSocket::Impl::setSslServerName(std::string serverName)
{
    ssl_server_name_ = std::move(serverName);
    return KMError::NOERR;
}

KMError TcpSocket::Impl::attachFd(SOCKET_FD fd, SSL* ssl)
{
    KUMA_INFOXTRACE("attachFd, with ssl, fd="<<fd<<", flags="<<ssl_flags_<<", state="<<getState());
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("attachFd, invalid state, state="<<getState());
        return KMError::INVALID_STATE;
    }
    
    fd_ = fd;
    setSocketOption();
    setState(State::OPEN);
    
    if(sslEnabled()) {
        if (ssl) {
            ssl_handler_ = new SslHandler();
            ssl_handler_->attachSsl(fd, ssl);
        } else {
            auto ret = startSslHandshake(SslRole::SERVER);
            if(ret != KMError::NOERR) {
                return ret;
            }
        }
    }

    auto loop = loop_.lock();
    if (loop) {
        loop->registerFd(fd_, KUMA_EV_NETWORK, [this] (uint32_t ev) { ioReady(ev); });
        registered_ = true;
    }
    return KMError::NOERR;
}

KMError TcpSocket::Impl::detachFd(SOCKET_FD &fd, SSL* &ssl)
{
    KUMA_INFOXTRACE("detachFd, with ssl, fd="<<fd_<<", state="<<getState());
    fd = fd_;
    fd_ = INVALID_FD;
    if(registered_) {
        registered_ = false;
        auto loop = loop_.lock();
        if (loop) {
            loop->unregisterFd(fd, false);
        }
    }
    if (ssl_handler_) {
        ssl_handler_->detachSsl(ssl);
    }
    cleanup();
    setState(State::CLOSED);
    return KMError::NOERR;
}

KMError TcpSocket::Impl::startSslHandshake(SslRole ssl_role)
{
    KUMA_INFOXTRACE("startSslHandshake, ssl_role="<<int(ssl_role)<<", fd="<<fd_<<", state="<<getState());
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("startSslHandshake, invalid fd");
        return KMError::INVALID_STATE;
    }
    if(ssl_handler_) {
        ssl_handler_->close();
        delete ssl_handler_;
        ssl_handler_ = nullptr;
    }
    ssl_handler_ = new SslHandler();
    auto ret = ssl_handler_->attachFd(fd_, ssl_role);
    if(ret != KMError::NOERR) {
        return ret;
    }
    if (ssl_role == SslRole::CLIENT) {
        if (!alpn_protos_.empty()) {
            ssl_handler_->setAlpnProtocols(alpn_protos_);
        }
        if (!ssl_server_name_.empty()) {
            ssl_handler_->setServerName(ssl_server_name_);
        }
        if (!ssl_host_name_.empty() && (ssl_flags_ & SSL_VERIFY_HOST_NAME)) {
            ssl_handler_->setHostName(ssl_host_name_);
        }
    }
    ssl_flags_ |= SSL_ENABLE;
    SslHandler::SslState ssl_state = ssl_handler_->doSslHandshake();
    if(SslHandler::SslState::SSL_ERROR == ssl_state) {
        return KMError::SSL_FAILED;
    }
    return KMError::NOERR;
}
#endif

void TcpSocket::Impl::setSocketOption()
{
    if(INVALID_FD == fd_) {
        return ;
    }
    
#ifdef KUMA_OS_LINUX
    fcntl(fd_, F_SETFD, FD_CLOEXEC);
#endif
    
    // nonblock
    set_nonblocking(fd_);
    
    if(0) {
        int opt_val = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt_val, sizeof(int));
    }
    
    if(set_tcpnodelay(fd_) != 0) {
        KUMA_WARNXTRACE("setSocketOption, failed to set TCP_NODELAY, fd="<<fd_<<", err="<<getLastError());
    }
}

bool TcpSocket::Impl::sslEnabled() const
{
#ifdef KUMA_HAS_OPENSSL
    return ssl_flags_ != SSL_NONE;
#else
    return false;
#endif
}

bool TcpSocket::Impl::isReady()
{
    return getState() == State::OPEN
#ifdef KUMA_HAS_OPENSSL
        && (!sslEnabled() ||
        (ssl_handler_ && ssl_handler_->getState() == SslHandler::SslState::SSL_SUCCESS))
#endif
        ;
}

int TcpSocket::Impl::send(const void* data, size_t length)
{
    if(!isReady()) {
        KUMA_WARNXTRACE("send, invalid state="<<getState());
        return 0;
    }
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("send, invalid fd");
        return -1;
    }
    
    int ret = 0;
#ifdef KUMA_HAS_OPENSSL
    if(sslEnabled()) {
        ret = ssl_handler_->send(data, length);
    } else 
#endif
    {
        ret = (int)::send(fd_, (const char*)data, length, 0);
        if(0 == ret) {
            KUMA_WARNXTRACE("send, peer closed, err="<<getLastError());
            ret = -1;
        } else if(ret < 0) {
            if(getLastError() == EAGAIN ||
#ifdef KUMA_OS_WIN
               WSAEWOULDBLOCK
#else
               EWOULDBLOCK
#endif
               == getLastError()) {
                ret = 0;
            } else {
                KUMA_ERRXTRACE("send, failed, err="<<getLastError());
            }
        }
    }
    
    if(ret < 0) {
        cleanup();
        setState(State::CLOSED);
    } else if(ret < length) {
        auto loop = loop_.lock();
        if (loop && loop->isPollLT()) {
            loop->updateFd(fd_, KUMA_EV_NETWORK);
        }
    }
    //KUMA_INFOXTRACE("send, ret="<<ret<<", len="<<length);
    return ret;
}

int TcpSocket::Impl::send(iovec* iovs, int count)
{
    if(!isReady()) {
        KUMA_WARNXTRACE("send 2, invalid state="<<getState());
        return 0;
    }
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("send 2, invalid fd");
        return -1;
    }
    
    uint32_t total_len = 0;
    for (int i = 0; i < count; ++i) {
        total_len += iovs[i].iov_len;
    }
    if (total_len == 0) {
        return 0;
    }
    int ret = 0;
    uint32_t bytes_sent = 0;
#ifdef KUMA_HAS_OPENSSL
    if(sslEnabled()) {
        ret = ssl_handler_->send(iovs, count);
        if(ret > 0) {
            bytes_sent = ret;
        }
    } else 
#endif
    {
#ifdef KUMA_OS_WIN
        DWORD bytes_sent_t = 0;
        ret = ::WSASend(fd_, (LPWSABUF)iovs, count, &bytes_sent_t, 0, NULL, NULL);
        bytes_sent = bytes_sent_t;
        if(0 == ret) ret = bytes_sent;
#else
        ret = (int)::writev(fd_, iovs, count);
#endif
        if(0 == ret) {
            KUMA_WARNXTRACE("send 2, peer closed");
            ret = -1;
        } else if(ret < 0) {
            if(EAGAIN == getLastError() ||
#ifdef KUMA_OS_WIN
               WSAEWOULDBLOCK == getLastError() || WSA_IO_PENDING
#else
               EWOULDBLOCK
#endif
               == getLastError()) {
                ret = 0;
            } else {
                KUMA_ERRXTRACE("send 2, fail, err="<<getLastError());
            }
        } else {
            bytes_sent = ret;
        }
    }
    
    if(ret < 0) {
        cleanup();
        setState(State::CLOSED);
    } else if(ret < total_len) {
        auto loop = loop_.lock();
        if(loop && loop->isPollLT()) {
            loop->updateFd(fd_, KUMA_EV_NETWORK);
        }
    }
    
    //KUMA_INFOXTRACE("send, ret="<<ret<<", bytes_sent="<<bytes_sent);
    return ret<0?ret:bytes_sent;
}

int TcpSocket::Impl::receive(void* data, size_t length)
{
    if(!isReady()) {
        return 0;
    }
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("receive, invalid fd");
        return -1;
    }
    int ret = 0;
#ifdef KUMA_HAS_OPENSSL
    if(sslEnabled()) {
        ret = ssl_handler_->receive(data, length);
    } else 
#endif
    {
        ret = (int)::recv(fd_, (char*)data, length, 0);
        if(0 == ret) {
            KUMA_WARNXTRACE("receive, peer closed, err="<<getLastError());
            ret = -1;
        } else if(ret < 0) {
            if(EAGAIN == getLastError() ||
#ifdef WIN32
               WSAEWOULDBLOCK
#else
               EWOULDBLOCK
#endif
               == getLastError()) {
                ret = 0;
            } else {
                KUMA_ERRXTRACE("receive, failed, err="<<getLastError());
            }
        }
    }
    
    if(ret < 0) {
        cleanup();
        setState(State::CLOSED);
    }
    
    //KUMA_INFOXTRACE("receive, ret="<<ret);
    return ret;
}

KMError TcpSocket::Impl::close()
{
    KUMA_INFOXTRACE("close, state="<<getState());
    auto loop = loop_.lock();
    if (loop && !loop->stopped()) {
        loop->sync([this] {
            cleanup();
        });
    } else {
        cleanup();
    }
    setState(State::CLOSED);
    return KMError::NOERR;
}

KMError TcpSocket::Impl::pause()
{
    auto loop = loop_.lock();
    if (loop) {
        return loop->updateFd(fd_, KUMA_EV_ERROR);
    }
    return KMError::INVALID_STATE;
}

KMError TcpSocket::Impl::resume()
{
    auto loop = loop_.lock();
    if (loop) {
        return loop->updateFd(fd_, KUMA_EV_NETWORK);
    }
    return KMError::INVALID_STATE;
}

void TcpSocket::Impl::onConnect(KMError err)
{
    KUMA_INFOXTRACE("onConnect, err="<<int(err)<<", state="<<getState());
    if(KMError::NOERR == err) {
        setState(State::OPEN);
#ifdef KUMA_HAS_OPENSSL
        if(sslEnabled()) {
            err = startSslHandshake(SslRole::CLIENT);
            if(KMError::NOERR == err && ssl_handler_->getState() == SslHandler::SslState::SSL_HANDSHAKE) {
                return; // continue to SSL handshake
            }
        }
#endif
    }
    if(err != KMError::NOERR) {
        cleanup();
        setState(State::CLOSED);
    }
    auto connect_cb(std::move(connect_cb_));
    if(connect_cb) connect_cb(err);
}

void TcpSocket::Impl::onSend(KMError err)
{
    SOCKET_FD fd = fd_;
    auto loop = loop_.lock();
    if (loop && loop->isPollLT() && fd != INVALID_FD) {
        loop->updateFd(fd, KUMA_EV_READ | KUMA_EV_ERROR);
    }
    if(write_cb_ && isReady()) write_cb_(err);
}

void TcpSocket::Impl::onReceive(KMError err)
{
    if(read_cb_ && isReady()) read_cb_(err);
}

void TcpSocket::Impl::onClose(KMError err)
{
    KUMA_INFOXTRACE("onClose, err="<<int(err)<<", state="<<getState());
    cleanup();
    setState(State::CLOSED);
    if(error_cb_) error_cb_(err);
}

void TcpSocket::Impl::onResolved(KMError err, const sockaddr_storage &addr)
{
    auto loop = loop_.lock();
    if (loop) {
        loop->async([=]{ // addr is captured by value
            if (err == KMError::NOERR) {
                connect_i(addr, -1);
            } else {
                onConnect(err);
            }
        });
    }
}

void TcpSocket::Impl::ioReady(uint32_t events)
{
    switch(getState())
    {
        case State::CONNECTING:
        {
            if(events & KUMA_EV_ERROR) {
                KUMA_ERRXTRACE("ioReady, KUMA_EV_ERROR, events="<<events
                              <<", err="<<getLastError()<<", state="<<getState());
                onConnect(KMError::POLL_ERROR);
            } else {
                DESTROY_DETECTOR_SETUP();
                onConnect(KMError::NOERR);
                DESTROY_DETECTOR_CHECK_VOID();
                if((events & KUMA_EV_READ)) {
                    onReceive(KMError::NOERR);
                }
            }
            break;
        }
            
        case State::OPEN:
        {
#ifdef KUMA_HAS_OPENSSL
            if(ssl_handler_ && ssl_handler_->getState() == SslHandler::SslState::SSL_HANDSHAKE) {
                auto err = KMError::NOERR;
                if(events & KUMA_EV_ERROR) {
                    err = KMError::POLL_ERROR;
                } else {
                    SslHandler::SslState ssl_state = ssl_handler_->doSslHandshake();
                    if(SslHandler::SslState::SSL_ERROR == ssl_state) {
                        err = KMError::SSL_FAILED;
                    } else if(SslHandler::SslState::SSL_HANDSHAKE == ssl_state) {
                        return;
                    }
                }
                if(connect_cb_) {
                    auto connect_cb(std::move(connect_cb_));
                    DESTROY_DETECTOR_SETUP();
                    connect_cb(err);
                    DESTROY_DETECTOR_CHECK_VOID();
                } else if(err != KMError::NOERR) {
                    onClose(err);
                } else {
                    events |= KUMA_EV_WRITE; // notify writable
                }
                if(err != KMError::NOERR) {
                    return;
                }
            }
#endif
            if(events & KUMA_EV_READ) {// handle EPOLLIN firstly
                DESTROY_DETECTOR_SETUP();
                onReceive(KMError::NOERR);
                DESTROY_DETECTOR_CHECK_VOID();
            }
            if((events & KUMA_EV_ERROR) && getState() == State::OPEN) {
                KUMA_ERRXTRACE("ioReady, KUMA_EV_ERROR, events="<<events
                              <<", err="<<getLastError()<<", state="<<getState());
                onClose(KMError::POLL_ERROR);
                break;
            }
            if((events & KUMA_EV_WRITE) && getState() == State::OPEN) {
                onSend(KMError::NOERR);
            }
            break;
        }
        default:
            //KUMA_WARNXTRACE("ioReady, invalid state="<<getState()
            //	<<", events="<<events);
            break;
    }
}
