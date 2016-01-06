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

KUMA_NS_BEGIN

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
}

int SslHandler::attachFd(SOCKET_FD fd, bool is_server)
{
    cleanup();
    SSL_CTX* ctx = NULL;
    is_server_ = is_server;
    fd_ = fd;
    if(is_server_) {
        ctx = OpenSslLib::getServerContext();
    } else {
        ctx = OpenSslLib::getClientContext();
    }
    if(NULL == ctx) {
        KUMA_ERRXTRACE("attachFd, CTX is NULL");
        return KUMA_ERROR_SSL_FAILED;
    }
    ssl_ = SSL_new(ctx);
    if(NULL == ssl_) {
        KUMA_ERRXTRACE("attachFd, SSL_new failed");
        return KUMA_ERROR_SSL_FAILED;
    }
    int ret = SSL_set_fd(ssl_, fd_);
    if(0 == ret) {
        KUMA_ERRXTRACE("attachFd, SSL_set_fd failed, err="<<ERR_reason_error_string(ERR_get_error()));
        SSL_free(ssl_);
        ssl_ = NULL;
        return KUMA_ERROR_SSL_FAILED;
    }
    return KUMA_ERROR_NOERR;
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

int SslHandler::send(const uint8_t* data, uint32_t size)
{
    if(NULL == ssl_) {
        KUMA_ERRXTRACE("send, ssl is NULL");
        return -1;
    }
    
    int ret = SSL_write(ssl_, data, size);
    int ssl_err = SSL_get_error(ssl_, ret);
    switch (ssl_err)
    {
        case SSL_ERROR_NONE:
            break;
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            ret = 0;
            break;
        default:
        {
            const char* err_str = ERR_reason_error_string(ERR_get_error());
            KUMA_ERRXTRACE( "send, SSL_write failed, fd="<<fd_
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
    }
    //KUMA_INFOXTRACE("send, ret: "<<ret<<", len: "<<len);
    return ret;
}

int SslHandler::send(const iovec* iovs, uint32_t count)
{
    uint32_t bytes_sent = 0;
    for (uint32_t i=0; i < count; ++i) {
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

int SslHandler::receive(uint8_t* data, uint32_t size)
{
    if(NULL == ssl_) {
        KUMA_ERRXTRACE("receive, ssl is NULL");
        return -1;
    }
    
    int ret = SSL_read(ssl_, data, size);
    int ssl_err = SSL_get_error(ssl_, ret);
    switch (ssl_err)
    {
        case SSL_ERROR_NONE:
            break;
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            ret = 0;
            break;
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

int SslHandler::close()
{
    cleanup();
    return KUMA_ERROR_NOERR;
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

KUMA_NS_END

#endif // KUMA_HAS_OPENSSL
