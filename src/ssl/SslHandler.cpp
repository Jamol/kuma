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

#include "SslHandler.h"
#include "util/kmtrace.h"

using namespace kuma;

SslHandler::SslHandler()
: fd_(INVALID_FD)
, ssl_(NULL)
, state_(SSL_NONE)
, is_server_(false)
{
    
}

SslHandler::~SslHandler()
{
    cleanup();
}

const char* SslHandler::getObjKey()
{
    return "SslHandler";
}

void SslHandler::cleanup()
{
    if(ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = NULL;
    }
    setState(SSL_NONE);
}

KMError SslHandler::setAlpnProtocols(const AlpnProtos &protocols)
{
#if OPENSSL_VERSION_NUMBER >= 0x1000200fL && !defined(OPENSSL_NO_TLSEXT)
    if (ssl_ && SSL_set_alpn_protos(ssl_, &protocols[0], (unsigned int)protocols.size()) == 0) {
        return KMError::NOERR;
    }
    return KMError::SSL_FAILED;
#else
    return KMError::UNSUPPORT;
#endif
}

KMError SslHandler::getAlpnSelected(std::string &proto)
{
    if (!ssl_) {
        return KMError::INVALID_STATE;
    }
    const uint8_t *buf = nullptr;
    uint32_t len = 0;
    SSL_get0_alpn_selected(ssl_, &buf, &len);
    if (buf && len > 0) {
        proto.assign((const char*)buf, len);
    } else {
        proto.clear();
    }
    return KMError::NOERR;
}

KMError SslHandler::setServerName(const std::string &serverName)
{
#if OPENSSL_VERSION_NUMBER >= 0x1000105fL && !defined(OPENSSL_NO_TLSEXT)
    if (ssl_ && SSL_set_tlsext_host_name(ssl_, serverName.c_str())) {
        return KMError::NOERR;
    }
    return KMError::SSL_FAILED;
#else
    return KMError::UNSUPPORT;
#endif
}

KMError SslHandler::attachFd(SOCKET_FD fd, SslRole ssl_role)
{
    cleanup();
    is_server_ = ssl_role == SslRole::SERVER;
    fd_ = fd;

    SSL_CTX* ctx = NULL;
    if(is_server_) {
        ctx = OpenSslLib::defaultServerContext();
    } else {
        ctx = OpenSslLib::defaultClientContext();
    }
    if(NULL == ctx) {
        KUMA_ERRXTRACE("attachFd, CTX is NULL");
        return KMError::SSL_FAILED;
    }
    ssl_ = SSL_new(ctx);
    if(NULL == ssl_) {
        KUMA_ERRXTRACE("attachFd, SSL_new failed");
        return KMError::SSL_FAILED;
    }
    //SSL_set_mode(ssl_, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    //SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE);
    int ret = SSL_set_fd(ssl_, fd_);
    if(0 == ret) {
        KUMA_ERRXTRACE("attachFd, SSL_set_fd failed, err="<<ERR_reason_error_string(ERR_get_error()));
        SSL_free(ssl_);
        ssl_ = NULL;
        return KMError::SSL_FAILED;
    }
    return KMError::NOERR;
}

KMError SslHandler::attachSsl(SOCKET_FD fd, SSL* ssl)
{
    cleanup();
    ssl_ = ssl;
    fd_ = fd;
    if (ssl_) {
        setState(SSL_SUCCESS);
    }
    return KMError::NOERR;
}

KMError SslHandler::detachSsl(SSL* &ssl)
{
    ssl = ssl_;
    ssl_ = nullptr;
    return KMError::NOERR;
}

SslHandler::SslState SslHandler::sslConnect()
{
    if(NULL == ssl_) {
        KUMA_ERRXTRACE("sslConnect, ssl_ is NULL");
        return SSL_ERROR;
    }
    
    int ret = SSL_connect(ssl_);
    int ssl_err = SSL_get_error (ssl_, ret);
    switch (ssl_err)
    {
        case SSL_ERROR_NONE:
            return SSL_SUCCESS;
            
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ:
            return SSL_HANDSHAKE;
            
        default:
        {
            const char* err_str = ERR_reason_error_string(ERR_get_error());
            KUMA_ERRXTRACE("sslConnect, error, fd="<<fd_
                           <<", ssl_status="<<ret
                           <<", ssl_err="<<ssl_err
                           <<", os_err="<<getLastError()
                           <<", err_msg="<<(err_str?err_str:""));
            SSL_free(ssl_);
            ssl_ = NULL;
            return SSL_ERROR;
        }
    }
    
    return SSL_HANDSHAKE;
}

SslHandler::SslState SslHandler::sslAccept()
{
    if(NULL == ssl_) {
        KUMA_ERRXTRACE("sslAccept, ssl_ is NULL");
        return SSL_ERROR;
    }
    
    int ret = SSL_accept(ssl_);
    int ssl_err = SSL_get_error(ssl_, ret);
    switch (ssl_err)
    {
        case SSL_ERROR_NONE:
            return SSL_SUCCESS;
            
        case SSL_ERROR_WANT_WRITE:
        case SSL_ERROR_WANT_READ:
            return SSL_HANDSHAKE;
            
        default:
        {
            const char* err_str = ERR_reason_error_string(ERR_get_error());
            KUMA_ERRXTRACE("sslAccept, error, fd="<<fd_
                           <<", ssl_status="<<ret
                           <<", ssl_err="<<ssl_err
                           <<", os_err="<<getLastError()
                           <<", err_msg="<<(err_str?err_str:""));
            SSL_free(ssl_);
            ssl_ = NULL;
            return SSL_ERROR;
        }
    }
    
    return SSL_HANDSHAKE;
}

int SslHandler::send(const void* data, size_t size)
{
    if(NULL == ssl_) {
        KUMA_ERRXTRACE("send, ssl is NULL");
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
                KUMA_ERRXTRACE("send, SSL_write failed, fd="<<fd_
                               <<", ssl_status="<<ret
                               <<", ssl_err="<<ssl_err
                               <<", errno="<<getLastError()
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
    //KUMA_INFOXTRACE("send, ret: "<<ret<<", len: "<<len);
    return int(offset);
}

int SslHandler::send(const iovec* iovs, int count)
{
    uint32_t bytes_sent = 0;
    for (int i=0; i < count; ++i) {
        int ret = SslHandler::send((const uint8_t*)iovs[i].iov_base, uint32_t(iovs[i].iov_len));
        if(ret < 0) {
            return ret;
        } else {
            bytes_sent += ret;
            if(ret < iovs[i].iov_len) {
                break;
            }
        }
    }
    return bytes_sent;
}

int SslHandler::receive(void* data, size_t size)
{
    if(NULL == ssl_) {
        KUMA_ERRXTRACE("receive, ssl is NULL");
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
            KUMA_INFOXTRACE("receive, SSL_ERROR_ZERO_RETURN, len="<<size);
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
            KUMA_ERRXTRACE("receive, SSL_read failed, fd="<<fd_
                           <<", ssl_status="<<ret
                           <<", ssl_err="<<ssl_err
                           <<", os_err="<<getLastError()
                           <<", err_msg="<<(err_str?err_str:""));
            ret = -1;
            break;
        }
    }

    if(ret < 0) {
        cleanup();
    }
    
    //KUMA_INFOXTRACE("receive, ret: "<<ret);
    return ret;
}

KMError SslHandler::close()
{
    cleanup();
    return KMError::NOERR;
}

SslHandler::SslState SslHandler::doSslHandshake()
{
    SslState state = SSL_HANDSHAKE;
    if(is_server_) {
        state = sslAccept();
    } else {
        state = sslConnect();
    }
    setState(state);
    if(SSL_ERROR == state) {
        cleanup();
    }
    return state;
}

#endif // KUMA_HAS_OPENSSL
