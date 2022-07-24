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

#ifdef KUMA_HAS_OPENSSL

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

#include "SioHandler.h"
#include "libkev/src/utils/kmtrace.h"
#include "libkev/src/utils/skutils.h"

#include <openssl/x509v3.h>

using namespace kuma;

SioHandler::SioHandler()
{
    obj_key_ = "SioHandler";
}

SioHandler::~SioHandler()
{
    cleanup();
}

KMError SioHandler::init(SslRole ssl_role, SOCKET_FD fd, uint32_t ssl_flags)
{
    auto err = SslHandler::init(ssl_role, fd, ssl_flags);
    if (err != KMError::NOERR) {
        return err;
    }
    
    int ret = SSL_set_fd(ssl_, static_cast<int>(fd_));
    if(0 == ret) {
        KM_ERRXTRACE("init, SSL_set_fd failed, err="<<ERR_reason_error_string(ERR_get_error()));
        SSL_free(ssl_);
        ssl_ = NULL;
        return KMError::SSL_ERROR;
    }
    return KMError::NOERR;
}

KMError SioHandler::attachSsl(SSL *ssl, BIO *nbio, SOCKET_FD fd)
{
    if (!ssl || nbio || fd == INVALID_FD) {
        return KMError::INVALID_PARAM;
    }
    cleanup();
    ssl_ = ssl;
    fd_ = fd;
    obj_key_ = "SioHandler_" + std::to_string(fd_);
    setState(SslState::SSL_SUCCESS);
    return KMError::NOERR;
}

KMError SioHandler::detachSsl(SSL* &ssl, BIO* &nbio)
{
    ssl = ssl_;
    ssl_ = nullptr;
    nbio = nullptr;
    return KMError::NOERR;
}

SioHandler::SslState SioHandler::sslConnect()
{
    if(!ssl_) {
        KM_ERRXTRACE("sslConnect, ssl_ is NULL");
        return SslState::SSL_ERROR;
    }
    
    int ret = SSL_connect(ssl_);
    int ssl_err = SSL_get_error (ssl_, ret);
    switch (ssl_err)
    {
        case SSL_ERROR_NONE:
            return SslState::SSL_SUCCESS;
            
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ:
            return SslState::SSL_HANDSHAKE;
            
        default:
        {
            const char* err_str = ERR_reason_error_string(ERR_get_error());
            KM_ERRXTRACE("sslConnect, error, fd="<<fd_
                           <<", ssl_status="<<ret
                           <<", ssl_err="<<ssl_err
                           <<", os_err="<<kev::SKUtils::getLastError()
                           <<", err_msg="<<(err_str?err_str:""));
            SSL_free(ssl_);
            ssl_ = NULL;
            return SslState::SSL_ERROR;
        }
    }
    
    return SslState::SSL_HANDSHAKE;
}

SioHandler::SslState SioHandler::sslAccept()
{
    if(!ssl_) {
        KM_ERRXTRACE("sslAccept, ssl_ is NULL");
        return SslState::SSL_ERROR;
    }
    
    int ret = SSL_accept(ssl_);
    int ssl_err = SSL_get_error(ssl_, ret);
    switch (ssl_err)
    {
        case SSL_ERROR_NONE:
            return SslState::SSL_SUCCESS;
            
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ:
            return SslState::SSL_HANDSHAKE;
            
        default:
        {
            const char* err_str = ERR_reason_error_string(ERR_get_error());
            KM_ERRXTRACE("sslAccept, error, fd="<<fd_
                           <<", ssl_status="<<ret
                           <<", ssl_err="<<ssl_err
                           <<", os_err="<<kev::SKUtils::getLastError()
                           <<", err_msg="<<(err_str?err_str:""));
            SSL_free(ssl_);
            ssl_ = NULL;
            return SslState::SSL_ERROR;
        }
    }
    
    return SslState::SSL_HANDSHAKE;
}

int SioHandler::send(const void* data, size_t size)
{
    if(!ssl_) {
        KM_ERRXTRACE("send, ssl is NULL");
        return -1;
    }
    ERR_clear_error();
    size_t offset = 0;
    
    // loop send until read/write want since we enabled partial write
    while (offset < size) {
        int ret = SSL_write(ssl_, (const char*)data + offset, (int)(size - offset));
        int ssl_err = SSL_get_error(ssl_, ret);
        switch (ssl_err)
        {
            case SSL_ERROR_NONE:
                break;
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                ret = 0;
                break;
            case SSL_ERROR_SYSCALL:
                if(errno == EAGAIN || errno == EINTR) {
                    ret = 0;
                    break;
                }
                // fallthru to log error
            default:
            {
                const char* err_str = ERR_reason_error_string(ERR_get_error());
                KM_ERRXTRACE("send, SSL_write failed, fd="<<fd_
                               <<", ssl_status="<<ret
                               <<", ssl_err="<<ssl_err
                               <<", errno="<<kev::SKUtils::getLastError()
                               <<", err_msg="<<(err_str?err_str:""));
                ret = -1;
                break;
            }
        }
        
        if(ret < 0) {
            cleanup();
            return ret;
        }
        offset += ret;
        if (ret == 0) {
            break;
        }
    }
    //KM_INFOXTRACE("send, ret: "<<ret<<", len: "<<len);
    return int(offset);
}

int SioHandler::send(const iovec* iovs, int count)
{
    uint32_t bytes_sent = 0;
    for (int i=0; i < count; ++i) {
        int ret = SioHandler::send((const uint8_t*)iovs[i].iov_base, uint32_t(iovs[i].iov_len));
        if(ret < 0) {
            return ret;
        } else {
            bytes_sent += ret;
            if(static_cast<size_t>(ret) < iovs[i].iov_len) {
                break;
            }
        }
    }
    return bytes_sent;
}

int SioHandler::send(const KMBuffer &buf)
{
    int bytes_sent = 0;
    for (auto it = buf.begin(); it != buf.end(); ++it) {
        int ret = SioHandler::send((const uint8_t*)it->readPtr(), it->length());
        if (ret < 0) {
            return ret;
        }
        bytes_sent += ret;
        if (static_cast<size_t>(ret) < it->length()) {
            return bytes_sent;
        }
    }
    return bytes_sent;
}

int SioHandler::receive(void* data, size_t size)
{
    if(!ssl_) {
        KM_ERRXTRACE("receive, ssl is NULL");
        return -1;
    }
    ERR_clear_error();
    int ret = SSL_read(ssl_, data, (int)size);
    int ssl_err = SSL_get_error(ssl_, ret);
    switch (ssl_err)
    {
        case SSL_ERROR_NONE:
            break;
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            ret = 0;
            break;
        case SSL_ERROR_ZERO_RETURN:
            ret = -1;
            KM_INFOXTRACE("receive, SSL_ERROR_ZERO_RETURN, len="<<size);
            break;
        case SSL_ERROR_SYSCALL:
            if(errno == EAGAIN || errno == EINTR) {
                ret = 0;
                break;
            }
            // fallthru to log error
        default:
        {
            const char* err_str = ERR_reason_error_string(ERR_get_error());
            KM_ERRXTRACE("receive, SSL_read failed, fd="<<fd_
                           <<", ssl_status="<<ret
                           <<", ssl_err="<<ssl_err
                           <<", os_err="<<kev::SKUtils::getLastError()
                           <<", err_msg="<<(err_str?err_str:""));
            ret = -1;
            break;
        }
    }

    if(ret < 0) {
        cleanup();
    }
    
    //KM_INFOXTRACE("receive, ret: "<<ret);
    return ret;
}

KMError SioHandler::close()
{
    cleanup();
    return KMError::NOERR;
}

SioHandler::SslState SioHandler::handshake()
{
    if (getState() == SslState::SSL_SUCCESS) {
        return SslState::SSL_SUCCESS;
    }
    SslState state = SslState::SSL_HANDSHAKE;
    if(is_server_) {
        state = sslAccept();
    } else {
        state = sslConnect();
    }
    setState(state);
    if (SslState::SSL_SUCCESS == state) {
        KM_INFOXTRACE("handshake, success, fd="<<fd_);
    }
    else if(SslState::SSL_ERROR == state) {
        cleanup();
    }
    return state;
}

#endif // KUMA_HAS_OPENSSL
