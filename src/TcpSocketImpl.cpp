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
#include "SocketBase.h"
#ifdef KUMA_OS_WIN
# include "poll/IocpSocket.h"
#endif
#include "ssl/BioHandler.h"
#include "ssl/SioHandler.h"

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
    if (socket_) {
        socket_->close();
        socket_.reset();
    }
#ifdef KUMA_HAS_OPENSSL
    ssl_handler_.reset();
#endif
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

SOCKET_FD TcpSocket::Impl::getFd() const
{
    return socket_->getFd();
}

EventLoopPtr TcpSocket::Impl::eventLoop() const
{
    return loop_.lock();
}

KMError TcpSocket::Impl::bind(const std::string &bind_host, uint16_t bind_port)
{
    if (!socket_ && !createSocket()) {
        return KMError::FAILED;
    }
    return socket_->bind(bind_host, bind_port);
}

KMError TcpSocket::Impl::connect(const std::string &host, uint16_t port, EventCallback cb, uint32_t timeout_ms)
{
    connect_cb_ = std::move(cb);
#ifdef KUMA_HAS_OPENSSL
    if (!km_is_ip_address(host.c_str()) && sslEnabled()) {
        ssl_host_name_ = host;
    }
#endif
    if (!socket_ && !createSocket()) {
        return KMError::INVALID_STATE;
    }
    return socket_->connect(host, port, [this](KMError err) {
        onConnect(err);
    }, timeout_ms);
}

KMError TcpSocket::Impl::attachFd(SOCKET_FD fd)
{
    KUMA_INFOXTRACE("attachFd, fd=" << fd << ", flags=" << ssl_flags_);
    if (!createSocket()) {
        return KMError::INVALID_STATE;
    }
    auto err = socket_->attachFd(fd);
    if (err != KMError::NOERR) {
        return err;
    }

#ifdef KUMA_HAS_OPENSSL
    if (sslEnabled()) {
        auto ret = startSslHandshake(SslRole::SERVER);
        if (ret != KMError::NOERR) {
            return ret;
        }
    }
#endif
    return KMError::NOERR;
}

KMError TcpSocket::Impl::detachFd(SOCKET_FD &fd)
{
    if (!socket_) {
        return KMError::INVALID_STATE;
    }
#ifdef KUMA_HAS_OPENSSL
    if (sslEnabled()) {
        return KMError::SSL_FAILED;
    }
#endif
    auto err = socket_->detachFd(fd);
    cleanup();
    return err;
}

KMError TcpSocket::Impl::attach(Impl &&other)
{
    if (eventLoop() != other.eventLoop()) {
        KUMA_ERRXTRACE("attach, different event loop");
        return KMError::INVALID_PARAM;
    }
    if (!other.socket_) {
        KUMA_ERRXTRACE("attach, invalid socket");
        return KMError::INVALID_PARAM;
    }
    ssl_flags_ = other.ssl_flags_;
    socket_ = std::move(other.socket_);
    socket_->setReadCallback([this](KMError err) {
        onReceive(err);
    });
    socket_->setWriteCallback([this](KMError err) {
        onSend(err);
    });
    socket_->setErrorCallback([this](KMError err) {
        onClose(err);
    });
#ifdef KUMA_HAS_OPENSSL
    is_bio_handler_ = other.is_bio_handler_;
    ssl_handler_ = std::move(other.ssl_handler_);
    if (ssl_handler_) {
        if (is_bio_handler_) {
            auto bio_handler = (BioHandler*)ssl_handler_.get();
            bio_handler->setSendFunc([this](const void *data, size_t length) -> int {
                return sendData(data, length);
            });
            bio_handler->setRecvFunc([this](void *data, size_t length) -> int {
                return recvData(data, length);
            });
        }
    }
#endif
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
    }
    else {
        return KMError::INVALID_STATE;
    }
}

KMError TcpSocket::Impl::setSslServerName(std::string serverName)
{
    ssl_server_name_ = std::move(serverName);
    return KMError::NOERR;
}

KMError TcpSocket::Impl::attachFd(SOCKET_FD fd, SSL *ssl, BIO *nbio)
{
    KUMA_INFOXTRACE("attachFd, with ssl, fd=" << fd << ", flags=" << ssl_flags_);
    if (!createSocket()) {
        return KMError::INVALID_STATE;
    }
    auto err = socket_->attachFd(fd);
    if (err != KMError::NOERR) {
        return err;
    }

    if (sslEnabled()) {
        if (ssl) {
            if (!createSslHandler()) {
                return KMError::SSL_FAILED;
            }
            return ssl_handler_->attachSsl(ssl, nbio, fd);
        }
        else {
            auto ret = startSslHandshake(SslRole::SERVER);
            if (ret != KMError::NOERR) {
                return ret;
            }
        }
    }

    return KMError::NOERR;
}

KMError TcpSocket::Impl::detachFd(SOCKET_FD &fd, SSL* &ssl, BIO* &nbio)
{
    if (!socket_) {
        return KMError::INVALID_STATE;
    }
    auto err = socket_->detachFd(fd);
    if (err != KMError::NOERR) {
        return err;
    }
    if (ssl_handler_) {
        ssl_handler_->detachSsl(ssl, nbio);
    }
    else {
        ssl = nullptr;
        nbio = nullptr;
    }
    cleanup();
    return KMError::NOERR;
}

KMError TcpSocket::Impl::startSslHandshake(SslRole ssl_role)
{
    KUMA_INFOXTRACE("startSslHandshake, ssl_role=" << int(ssl_role) << ", fd=" << getFd());
    if (!socket_->isReady()) {
        KUMA_ERRXTRACE("startSslHandshake, invalid fd");
        return KMError::INVALID_STATE;
    }

    if (!createSslHandler()) {
        return KMError::SSL_FAILED;
    }
    auto ret = ssl_handler_->init(ssl_role, getFd());
    if (ret != KMError::NOERR) {
        KUMA_ERRXTRACE("startSslHandshake, failed to init SSL handler");
        return ret;
    }
    ssl_flags_ |= SSL_ENABLE;
    if (ssl_role == SslRole::CLIENT) {
        if (!alpn_protos_.empty()) {
            ssl_handler_->setAlpnProtocols(alpn_protos_);
        }
        if (!ssl_server_name_.empty()) {
            ssl_handler_->setServerName(ssl_server_name_);
        } else if (!ssl_host_name_.empty()) {
            ssl_handler_->setServerName(ssl_host_name_);
        }
        if (!ssl_host_name_.empty() && (ssl_flags_ & SSL_VERIFY_HOST_NAME)) {
            ssl_handler_->setHostName(ssl_host_name_);
        }
    }

    auto ssl_state = ssl_handler_->handshake();
    if (ssl_state == SslHandler::SslState::SSL_ERROR) {
        return KMError::SSL_FAILED;
    }
    return KMError::NOERR;
}
#endif

bool TcpSocket::Impl::sslEnabled() const
{
#ifdef KUMA_HAS_OPENSSL
    return ssl_flags_ != SSL_NONE;
#else
    return false;
#endif
}

bool TcpSocket::Impl::isReady() const
{
    return socket_ && socket_->isReady()
#ifdef KUMA_HAS_OPENSSL
        && (!sslEnabled() ||
        (ssl_handler_ && ssl_handler_->getState() == SslHandler::SslState::SSL_SUCCESS))
#endif
        ;
}

int TcpSocket::Impl::send(const void* data, size_t length)
{
    if (!isReady()) {
        KUMA_WARNXTRACE("send, invalid state");
        return 0;
    }

    int ret = 0;
#ifdef KUMA_HAS_OPENSSL
    if (sslEnabled()) {
        ret = ssl_handler_->send(data, length);
        if(!is_bio_handler_ && ret >= 0 && static_cast<size_t>(ret) < length) {
            socket_->notifySendBlocked();
        }
    }
    else
#endif
    {
        ret = sendData(data, length);
    }
    if (ret < 0) {
        cleanup();
    }
    return ret;
}

int TcpSocket::Impl::send(iovec* iovs, int count)
{
    if (!isReady()) {
        KUMA_WARNXTRACE("send 2, invalid state");
        return 0;
    }

    int ret = 0;
#ifdef KUMA_HAS_OPENSSL
    if (sslEnabled()) {
        size_t bytes_total = 0;
        for (int i = 0; i < count; ++i) {
            bytes_total += iovs[i].iov_len;
        }
        if (bytes_total == 0) {
            return 0;
        }
        ret = ssl_handler_->send(iovs, count);
        if(!is_bio_handler_ && ret >= 0 && static_cast<size_t>(ret) < bytes_total) {
            socket_->notifySendBlocked();
        }
    }
    else
#endif
    {
        ret = sendData(iovs, count);
    }
    if (ret < 0) {
        cleanup();
    }
    return ret;
}

int TcpSocket::Impl::receive(void* data, size_t length)
{
    if (!isReady()) {
        return 0;
    }

    int ret = 0;
#ifdef KUMA_HAS_OPENSSL
    if (sslEnabled()) {
        ret = ssl_handler_->receive(data, length);
    }
    else
#endif
    {
        ret = recvData(data, length);
    }
    if (ret < 0) {
        cleanup();
    }
    return ret;
}

KMError TcpSocket::Impl::close()
{
    KUMA_INFOXTRACE("close");
    auto loop = eventLoop();
    if (loop && !loop->stopped()) {
        loop->sync([this] {
            cleanup();
        });
    }
    else {
        cleanup();
    }
    return KMError::NOERR;
}

KMError TcpSocket::Impl::pause()
{
    if (!isReady()) {
        return KMError::INVALID_STATE;
    }
    return socket_->pause();
}

KMError TcpSocket::Impl::resume()
{
    if (!isReady()) {
        return KMError::INVALID_STATE;
    }
    return socket_->resume();
}

void TcpSocket::Impl::onConnect(KMError err)
{
    KUMA_INFOXTRACE("onConnect, err=" << int(err));
    if (KMError::NOERR == err) {
#ifdef KUMA_HAS_OPENSSL
        if (sslEnabled()) {
            err = startSslHandshake(SslRole::CLIENT);
            if (KMError::NOERR == err && ssl_handler_->getState() == SslHandler::SslState::SSL_HANDSHAKE) {
                return; // continue to SSL handshake
            }
        }
#endif
    }
    if (err != KMError::NOERR) {
        cleanup();
    }
    auto connect_cb(std::move(connect_cb_));
    if (connect_cb) connect_cb(err);
}

void TcpSocket::Impl::onSend(KMError err)
{
#ifdef KUMA_HAS_OPENSSL
    if (sslEnabled() && ssl_handler_) {
        err = checkSslHandshake(KMError::NOERR);
        if (err != KMError::NOERR) {
            return;
        }
        err = ssl_handler_->sendBufferedData();
        if (km_is_fatal_error(err)) {
            KUMA_ERRXTRACE("onSend, failed to send SSL data, err=" << (int)err);
            onClose(err);
            return;
        }
    }
#endif

    if (write_cb_ && isReady()) write_cb_(err);
}

void TcpSocket::Impl::onReceive(KMError err)
{
#ifdef KUMA_HAS_OPENSSL
    err = checkSslHandshake(KMError::NOERR);
    if (err != KMError::NOERR) {
        return;
    }
#endif
    if (read_cb_ && isReady()) read_cb_(err);
}

void TcpSocket::Impl::onClose(KMError err)
{
    KUMA_INFOXTRACE("onClose, err=" << int(err));
    cleanup();
    if (error_cb_) error_cb_(err);
}

bool TcpSocket::Impl::createSocket()
{
    auto loop = eventLoop();
    if (loop) {
#ifdef KUMA_OS_WIN
        if (loop->getPollType() == PollType::IOCP) {
            socket_.reset(new IocpSocket(loop));
        }
        else
#endif
        {
            socket_.reset(new SocketBase(loop));
        }
        socket_->setReadCallback([this](KMError err) {
            onReceive(err);
        });
        socket_->setWriteCallback([this](KMError err) {
            onSend(err);
        });
        socket_->setErrorCallback([this](KMError err) {
            onClose(err);
        });
        return true;
    }
    return false;
}
#ifdef KUMA_HAS_OPENSSL
bool TcpSocket::Impl::createSslHandler()
{
    auto loop = eventLoop();
    if (loop) {
        if (loop->getPollType() == PollType::IOCP) {
            auto bio_handler = new BioHandler();
            bio_handler->setSendFunc([this](const void *data, size_t length) -> int {
                return sendData(data, length);
            });
            bio_handler->setRecvFunc([this](void *data, size_t length) -> int {
                return recvData(data, length);
            });
            ssl_handler_.reset(bio_handler);
            is_bio_handler_ = true;
        } else {
            auto sio_handler = new SioHandler();
            ssl_handler_.reset(sio_handler);
            is_bio_handler_ = false;
        }
        return true;
    }
    return false;
}

KMError TcpSocket::Impl::checkSslHandshake(KMError err)
{
    if (!sslEnabled() || ssl_handler_->getState() != SslHandler::SslState::SSL_HANDSHAKE) {
        return KMError::NOERR;
    }

    if (err == KMError::NOERR) {
        auto ssl_state = ssl_handler_->handshake();
        if (ssl_state == SslHandler::SslState::SSL_HANDSHAKE) {
            return KMError::AGAIN; // continue handshake
        } else if (ssl_state == SslHandler::SslState::SSL_SUCCESS) {
            err = KMError::NOERR;
        } else {
            err = KMError::SSL_FAILED;
        }
    }
    KUMA_INFOXTRACE("checkSslHandshake, completed, err=" << int(err));
    if (connect_cb_) {
        auto connect_cb(std::move(connect_cb_));
        DESTROY_DETECTOR_SETUP();
        connect_cb(err);
        DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
    }
    else if (err != KMError::NOERR) {
        onClose(err);
    }

    return err;
}
#endif

int TcpSocket::Impl::sendData(const void* data, size_t length)
{
    return socket_->send(data, length);
}

int TcpSocket::Impl::sendData(iovec* iovs, int count)
{
    return socket_->send(iovs, count);
}

int TcpSocket::Impl::recvData(void *data, size_t length)
{
    return socket_->receive(data, length);
}
