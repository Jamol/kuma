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

#ifdef KUMA_HAS_OPENSSL
# include "ssl/SslHandler.h"
#endif

using namespace kuma;

TcpSocketImpl::TcpSocketImpl(EventLoopImpl* loop)
: loop_(loop)
{
    KM_SetObjKey("TcpSocket");
}

TcpSocketImpl::~TcpSocketImpl()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
    cleanup();
}

void TcpSocketImpl::cleanup()
{
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
            loop_->unregisterFd(fd, true);
        } else {
            closeFd(fd);
        }
    }
}

int TcpSocketImpl::setSslFlags(uint32_t ssl_flags)
{
#ifdef KUMA_HAS_OPENSSL
    ssl_flags_ = ssl_flags;
    return KUMA_ERROR_NOERR;
#else
    return KUMA_ERROR_UNSUPPORT;
#endif
}

int TcpSocketImpl::bind(const char *bind_host, uint16_t bind_port)
{
    KUMA_INFOTRACE("bind, bind_host="<<bind_host<<", bind_port="<<bind_port);
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("bind, invalid state, state="<<getState());
        return KUMA_ERROR_INVALID_STATE;
    }
    if(fd_ != INVALID_FD) {
        cleanup();
    }
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST;//AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(km_set_sock_addr(bind_host, bind_port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
        return KUMA_ERROR_INVALID_PARAM;
    }
    fd_ = ::socket(ss_addr.ss_family, SOCK_STREAM, 0);
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("bind, socket failed, err="<<getLastError());
        return KUMA_ERROR_FAILED;
    }
    int addr_len = sizeof(ss_addr);
#ifdef KUMA_OS_MAC
    if(AF_INET == ss_addr.ss_family)
        addr_len = sizeof(sockaddr_in);
    else
        addr_len = sizeof(sockaddr_in6);
#endif
    int ret = ::bind(fd_, (struct sockaddr*)&ss_addr, addr_len);
    if(ret < 0) {
        KUMA_ERRXTRACE("bind, bind failed, err="<<getLastError());
        return KUMA_ERROR_FAILED;
    }
    return KUMA_ERROR_NOERR;
}

int TcpSocketImpl::connect(const char *host, uint16_t port, EventCallback cb, uint32_t timeout_ms)
{
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("connect, invalid state, state="<<getState());
        return KUMA_ERROR_INVALID_STATE;
    }
    connect_cb_ = std::move(cb);
    return connect_i(host, port, timeout_ms);
}

int TcpSocketImpl::connect_i(const char* host, uint16_t port, uint32_t timeout_ms)
{
    KUMA_INFOXTRACE("connect_i, host="<<host<<", port="<<port<<", this="<<this);
#ifndef KUMA_HAS_OPENSSL
    if (SslEnabled()) {
        KUMA_ERRXTRACE("connect_i, OpenSSL is disabled");
        return KUMA_ERROR_UNSUPPORT;
    }
#endif
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(km_set_sock_addr(host, port, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) != 0) {
        KUMA_ERRXTRACE("connect_i, failed to resolve host: "<<host);
        return KUMA_ERROR_INVALID_PARAM;
    }
    if(INVALID_FD == fd_) {
        fd_ = ::socket(ss_addr.ss_family, SOCK_STREAM, 0);
        if(INVALID_FD == fd_) {
            KUMA_ERRXTRACE("connect_i, socket failed, err="<<getLastError());
            return KUMA_ERROR_FAILED;
        }
    }
    setSocketOption();
    
    int addr_len = sizeof(ss_addr);
#ifdef KUMA_OS_MAC
    if(AF_INET == ss_addr.ss_family)
        addr_len = sizeof(sockaddr_in);
    else
        addr_len = sizeof(sockaddr_in6);
#endif
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
        return KUMA_ERROR_FAILED;
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
    
    loop_->registerFd(fd_, KUMA_EV_NETWORK, [this] (uint32_t ev) { ioReady(ev); });
    registered_ = true;
    return KUMA_ERROR_NOERR;
}

int TcpSocketImpl::attachFd(SOCKET_FD fd)
{
    KUMA_INFOXTRACE("attachFd, fd="<<fd<<", flags="<<ssl_flags_<<", state="<<getState());
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("attachFd, invalid state, state="<<getState());
        return KUMA_ERROR_INVALID_STATE;
    }
#ifndef KUMA_HAS_OPENSSL
    if (SslEnabled()) {
        KUMA_ERRXTRACE("attachFd, OpenSSL is disabled");
        return KUMA_ERROR_UNSUPPORT;
    }
#endif
    
    fd_ = fd;
    setSocketOption();
    setState(State::OPEN);
#ifdef KUMA_HAS_OPENSSL
    if(SslEnabled()) {
        int ret = startSslHandshake(AS_SERVER);
        if(ret != KUMA_ERROR_NOERR) {
            return ret;
        }
    }
#endif
    loop_->registerFd(fd_, KUMA_EV_NETWORK, [this] (uint32_t ev) { ioReady(ev); });
    registered_ = true;
    return KUMA_ERROR_NOERR;
}

int TcpSocketImpl::detachFd(SOCKET_FD &fd)
{
    KUMA_INFOXTRACE("detachFd, fd="<<fd_<<", state="<<getState());
    fd = fd_;
    fd_ = INVALID_FD;
    if(registered_) {
        registered_ = false;
        loop_->unregisterFd(fd, false);
    }
    cleanup();
    setState(State::CLOSED);
    return KUMA_ERROR_NOERR;
}

#ifdef KUMA_HAS_OPENSSL
int TcpSocketImpl::setAlpnProtocols(const AlpnProtos &protocols)
{
    alpn_protos_ = protocols;
    return KUMA_ERROR_NOERR;
}

int TcpSocketImpl::getAlpnSelected(std::string &proto)
{
    if (!SslEnabled()) {
        return KUMA_ERROR_INVALID_PROTO;
    }
    if (ssl_handler_) {
        return ssl_handler_->getAlpnSelected(proto);
    } else {
        return KUMA_ERROR_INVALID_STATE;
    }
}

int TcpSocketImpl::attachFd(SOCKET_FD fd, SSL* ssl)
{
    KUMA_INFOXTRACE("attachFd, with ssl, fd="<<fd<<", flags="<<ssl_flags_<<", state="<<getState());
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("attachFd, invalid state, state="<<getState());
        return KUMA_ERROR_INVALID_STATE;
    }
    
    fd_ = fd;
    setSocketOption();
    setState(State::OPEN);
    
    if(SslEnabled()) {
        if (ssl) {
            ssl_handler_ = new SslHandler();
            ssl_handler_->attachSsl(ssl);
        } else {
            int ret = startSslHandshake(AS_SERVER);
            if(ret != KUMA_ERROR_NOERR) {
                return ret;
            }
        }
    }

    loop_->registerFd(fd_, KUMA_EV_NETWORK, [this] (uint32_t ev) { ioReady(ev); });
    registered_ = true;
    return KUMA_ERROR_NOERR;
}

int TcpSocketImpl::detachFd(SOCKET_FD &fd, SSL* &ssl)
{
    KUMA_INFOXTRACE("detachFd, with ssl, fd="<<fd_<<", state="<<getState());
    fd = fd_;
    fd_ = INVALID_FD;
    if(registered_) {
        registered_ = false;
        loop_->unregisterFd(fd, false);
    }
    if (ssl_handler_) {
        ssl_handler_->detachSsl(ssl);
    }
    cleanup();
    setState(State::CLOSED);
    return KUMA_ERROR_NOERR;
}

int TcpSocketImpl::startSslHandshake(SslRole ssl_role)
{
    KUMA_INFOXTRACE("startSslHandshake, ssl_role="<<ssl_role<<", fd="<<fd_<<", state="<<getState());
    if(INVALID_FD == fd_) {
        KUMA_ERRXTRACE("startSslHandshake, invalid fd");
        return KUMA_ERROR_INVALID_STATE;
    }
    if(ssl_handler_) {
        ssl_handler_->close();
        delete ssl_handler_;
        ssl_handler_ = nullptr;
    }
    ssl_handler_ = new SslHandler();
    int ret = ssl_handler_->attachFd(fd_, ssl_role);
    if(ret != KUMA_ERROR_NOERR) {
        return ret;
    }
    if (ssl_role == SslRole::AS_CLIENT && !alpn_protos_.empty()) {
        ssl_handler_->setAlpnProtocols(alpn_protos_);
    }
    ssl_flags_ |= SSL_ENABLE;
    SslHandler::SslState ssl_state = ssl_handler_->doSslHandshake();
    if(SslHandler::SslState::SSL_ERROR == ssl_state) {
        return KUMA_ERROR_SSL_FAILED;
    }
    return KUMA_ERROR_NOERR;
}
#endif

void TcpSocketImpl::setSocketOption()
{
    if(INVALID_FD == fd_) {
        return ;
    }
    
#ifdef KUMA_OS_LINUX
    fcntl(fd_, F_SETFD, FD_CLOEXEC);
#endif
    
    // nonblock
#ifdef KUMA_OS_WIN
    int mode = 1;
    ::ioctlsocket(fd_,FIONBIO,(ULONG*)&mode);
#else
    int flag = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flag | O_NONBLOCK | O_ASYNC);
#endif
    
    if(0) {
        int opt_val = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt_val, sizeof(int));
    }
    
    int opt_val = 1;
    if(setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, (char*)&opt_val, sizeof(int)) != 0) {
        KUMA_WARNXTRACE("setSocketOption, failed to set TCP_NODELAY, fd="<<fd_<<", err="<<getLastError());
    }
}

bool TcpSocketImpl::SslEnabled()
{
#ifdef KUMA_HAS_OPENSSL
    return ssl_flags_ != SSL_NONE;
#else
    return false;
#endif
}

bool TcpSocketImpl::isReady()
{
    return getState() == State::OPEN
#ifdef KUMA_HAS_OPENSSL
        && (!SslEnabled() ||
        (ssl_handler_ && ssl_handler_->getState() == SslHandler::SslState::SSL_SUCCESS))
#endif
        ;
}

int TcpSocketImpl::send(const uint8_t* data, size_t length)
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
    if(SslEnabled()) {
        ret = ssl_handler_->send(data, length);
    } else 
#endif
    {
        ret = (int)::send(fd_, (char*)data, length, 0);
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
        if(loop_->isPollLT()) {
            loop_->updateFd(fd_, KUMA_EV_NETWORK);
        }
    }
    //KUMA_INFOXTRACE("send, ret="<<ret<<", len="<<len);
    return ret;
}

int TcpSocketImpl::send(iovec* iovs, int count)
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
    for (uint32_t i = 0; i < count; ++i) {
        total_len += iovs[i].iov_len;
    }
    if (total_len == 0) {
        return 0;
    }
    int ret = 0;
    uint32_t bytes_sent = 0;
#ifdef KUMA_HAS_OPENSSL
    if(SslEnabled()) {
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
        if(loop_->isPollLT()) {
            loop_->updateFd(fd_, KUMA_EV_NETWORK);
        }
    }
    
    //KUMA_INFOXTRACE("send, ret="<<ret<<", bytes_sent="<<bytes_sent);
    return ret<0?ret:bytes_sent;
}

int TcpSocketImpl::receive(uint8_t* data, size_t length)
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
    if(SslEnabled()) {
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

int TcpSocketImpl::close()
{
    KUMA_INFOXTRACE("close, state="<<getState());
    loop_->runInEventLoopSync([this] {
        cleanup();
        setState(State::CLOSED);
    });
    return KUMA_ERROR_NOERR;
}

int TcpSocketImpl::pause()
{
    return loop_->updateFd(fd_, KUMA_EV_ERROR);
}

int TcpSocketImpl::resume()
{
    return loop_->updateFd(fd_, KUMA_EV_NETWORK);
}

void TcpSocketImpl::onConnect(int err)
{
    KUMA_INFOXTRACE("onConnect, err="<<err<<", state="<<getState());
    if(0 == err) {
        setState(State::OPEN);
#ifdef KUMA_HAS_OPENSSL
        if(SslEnabled()) {
            err = startSslHandshake(AS_CLIENT);
            if(KUMA_ERROR_NOERR == err && ssl_handler_->getState() == SslHandler::SslState::SSL_HANDSHAKE) {
                return; // continue to SSL handshake
            }
        }
#endif
    }
    if(err != KUMA_ERROR_NOERR) {
        cleanup();
        setState(State::CLOSED);
    }
    auto connect_cb(std::move(connect_cb_));
    if(connect_cb) connect_cb(err);
}

void TcpSocketImpl::onSend(int err)
{
    SOCKET_FD fd = fd_;
    if (loop_->isPollLT() && fd != INVALID_FD) {
        loop_->updateFd(fd, KUMA_EV_READ | KUMA_EV_ERROR);
    }
    if(write_cb_ && isReady()) write_cb_(err);
}

void TcpSocketImpl::onReceive(int err)
{
    if(read_cb_ && isReady()) read_cb_(err);
}

void TcpSocketImpl::onClose(int err)
{
    KUMA_INFOXTRACE("onClose, err="<<err<<", state="<<getState());
    cleanup();
    setState(State::CLOSED);
    if(error_cb_) error_cb_(err);
}

void TcpSocketImpl::ioReady(uint32_t events)
{
    switch(getState())
    {
        case State::CONNECTING:
        {
            if(events & KUMA_EV_ERROR) {
                KUMA_ERRXTRACE("ioReady, KUMA_EV_ERROR, events="<<events
                              <<", err="<<getLastError()<<", state="<<getState());
                onConnect(KUMA_ERROR_POLLERR);
            } else {
                bool destroyed = false;
                destroy_flag_ptr_ = &destroyed;
                onConnect(KUMA_ERROR_NOERR);
                if(destroyed) {
                    return ;
                }
                destroy_flag_ptr_ = nullptr;
                if((events & KUMA_EV_READ)) {
                    onReceive(0);
                }
            }
            break;
        }
            
        case State::OPEN:
        {
#ifdef KUMA_HAS_OPENSSL
            if(ssl_handler_ && ssl_handler_->getState() == SslHandler::SslState::SSL_HANDSHAKE) {
                int err = KUMA_ERROR_NOERR;
                if(events & KUMA_EV_ERROR) {
                    err = KUMA_ERROR_POLLERR;
                } else {
                    SslHandler::SslState ssl_state = ssl_handler_->doSslHandshake();
                    if(SslHandler::SslState::SSL_ERROR == ssl_state) {
                        err = KUMA_ERROR_SSL_FAILED;
                    } else if(SslHandler::SslState::SSL_HANDSHAKE == ssl_state) {
                        return;
                    }
                }
                if(connect_cb_) {
                    auto connect_cb(std::move(connect_cb_));
                    connect_cb(err);
                } else if(err != KUMA_ERROR_NOERR) {
                    onClose(err);
                } else {
                    events |= KUMA_EV_WRITE; // notify writable
                }
                if(err != KUMA_ERROR_NOERR) {
                    return;
                }
            }
#endif
            bool destroyed = false;
            destroy_flag_ptr_ = &destroyed;
            if(events & KUMA_EV_READ) {// handle EPOLLIN firstly
                onReceive(0);
            }
            if(destroyed) {
                return;
            }
            destroy_flag_ptr_ = nullptr;
            if((events & KUMA_EV_ERROR) && getState() == State::OPEN) {
                KUMA_ERRXTRACE("ioReady, KUMA_EV_ERROR, events="<<events
                              <<", err="<<getLastError()<<", state="<<getState());
                onClose(KUMA_ERROR_POLLERR);
                break;
            }
            if((events & KUMA_EV_WRITE) && getState() == State::OPEN) {
                onSend(0);
            }
            break;
        }
        default:
            //KUMA_WARNXTRACE("ioReady, invalid state="<<getState()
            //	<<", events="<<events);
            break;
    }
}
