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

#include "BioHandler.h"
#include "util/kmtrace.h"
#include "util/util.h"

#include <openssl/x509v3.h>

using namespace kuma;

BioHandler::BioHandler()
{
    obj_key_ = "BioHandler";
}

BioHandler::~BioHandler()
{
    cleanup();
}

void BioHandler::cleanup()
{
    SslHandler::cleanup();
    if (net_bio_) {
        BIO_free(net_bio_);
        net_bio_ = nullptr;
    }
}

KMError BioHandler::init(SslRole ssl_role, SOCKET_FD fd, uint32_t ssl_flags)
{
    auto err = SslHandler::init(ssl_role, fd, ssl_flags);
    if (err != KMError::NOERR) {
        return err;
    }
    
    BIO *internal_bio = nullptr;
    BIO_new_bio_pair(&internal_bio, 0, &net_bio_, 0);
    SSL_set_bio(ssl_, internal_bio, internal_bio);
    if (isServer()) {
        SSL_set_accept_state(ssl_);
    } else {
        SSL_set_connect_state(ssl_);
    }
    return KMError::NOERR;
}

KMError BioHandler::attachSsl(SSL *ssl, BIO *nbio, SOCKET_FD fd)
{
    if (!ssl || !nbio || fd == INVALID_FD) {
        return KMError::INVALID_PARAM;
    }
    cleanup();
    fd_ = fd;
    ssl_ = ssl;
    net_bio_ = nbio;
    obj_key_ = "BioHandler_" + std::to_string(fd_);
    setState(SslState::SSL_SUCCESS);
    return KMError::NOERR;
}

KMError BioHandler::detachSsl(SSL* &ssl, BIO* &nbio)
{
    ssl = ssl_;
    ssl_ = nullptr;
    nbio = net_bio_;
    net_bio_ = nullptr;
    return KMError::NOERR;
}

int BioHandler::send(const void* data, size_t length)
{
    size_t bytes_total = 0;
    const uint8_t *ptr = static_cast<const uint8_t*>(data);
    do {
        auto ret = writeAppData(ptr + bytes_total, length - bytes_total);
        if (ret < 0) {
            KUMA_ERRXTRACE("send, failed to write app data");
            return ret;
        }
        bytes_total += ret;
        auto err = trySendSslData();
        if (km_is_fatal_error(err)) {
            KUMA_ERRXTRACE("send, failed to send SSL data, err=" << (int)err);
            return -1;
        }
        if (err != KMError::AGAIN) {
            break;
        }
    } while (bytes_total < length);
    return static_cast<int>(bytes_total);
}

int BioHandler::send(const iovec* iovs, int count)
{
    int bytes_sent = 0;
    for (int i=0; i < count; ++i) {
        int ret = send((const uint8_t*)iovs[i].iov_base, uint32_t(iovs[i].iov_len));
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

int BioHandler::receive(void* data, size_t length)
{
    size_t bytes_total = 0;
    uint8_t *ptr = static_cast<uint8_t*>(data);
    do {
        auto err = tryRecvSslData();
        if (km_is_fatal_error(err)) {
            KUMA_ERRXTRACE("receive, failed to recv SSL data, err=" << (int)err);
            return -1;
        }
        auto ret = readAppData(ptr + bytes_total, length - bytes_total);
        if (ret < 0) {
            KUMA_ERRXTRACE("receive, failed to read app data");
            return ret;
        }
        bytes_total += ret;
        if (err != KMError::AGAIN) {
            break;
        }
    } while (bytes_total < length);
    return static_cast<int>(bytes_total);
}

KMError BioHandler::close()
{
    cleanup();
    return KMError::NOERR;
}

BioHandler::SslState BioHandler::handshake()
{
    auto ret = trySslHandshake();
    if (ret == KMError::AGAIN) {
        return SslState::SSL_HANDSHAKE;
    } else if (ret == KMError::NOERR){
        KUMA_INFOXTRACE("handshake, success");
        return SslState::SSL_SUCCESS;
    } else {
        return SslState::SSL_ERROR;
    }
}

BioHandler::SslState BioHandler::doHandshake()
{
    SslState state = SslState::SSL_HANDSHAKE;
    do {
        if(!ssl_) {
            KUMA_ERRXTRACE("handshake, ssl_ is NULL");
            state = SslState::SSL_ERROR;
            break;
        }
        if (SSL_is_init_finished(ssl_)) {
            state = SslState::SSL_SUCCESS;
            break;
        }
        auto ret = SSL_do_handshake(ssl_);
        auto ssl_err = SSL_get_error(ssl_, ret);
        switch (ssl_err)
        {
            case SSL_ERROR_NONE:
                state = SslState::SSL_SUCCESS;
                break;
                
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_READ:
                state = SslState::SSL_HANDSHAKE;
                break;
                
            default:
            {
                const char* err_str = ERR_reason_error_string(ERR_get_error());
                KUMA_ERRXTRACE("handshake, error"
                               <<", ssl_status="<<ret
                               <<", ssl_err="<<ssl_err
                               <<", os_err="<<getLastError()
                               <<", err_msg="<<(err_str?err_str:""));
                SSL_free(ssl_);
                ssl_ = NULL;
                state = SslState::SSL_ERROR;
                break;
            }
        }
    } while(0);
    
    setState(state);
    if(SslState::SSL_ERROR == state) {
        cleanup();
    }
    return state;
}

int BioHandler::writeAppData(const void* data, size_t size)
{
    if(!ssl_) {
        KUMA_ERRXTRACE("writeAppData, ssl is NULL");
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
                KUMA_ERRXTRACE("writeAppData, SSL_write failed"
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
    //KUMA_INFOXTRACE("writeAppData, ret: "<<ret<<", len: "<<len);
    return static_cast<int>(offset);
}

int BioHandler::readAppData(void* data, size_t size)
{
    if(!ssl_) {
        KUMA_ERRXTRACE("readAppData, ssl is NULL");
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
            KUMA_INFOXTRACE("readAppData, SSL_ERROR_ZERO_RETURN, len="<<size);
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
            KUMA_ERRXTRACE("readAppData, SSL_read failed"
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
    
    //KUMA_INFOXTRACE("readAppData, ret: "<<ret);
    return ret;
}

int BioHandler::writeSslData(const void* data, size_t size)
{
    if (!net_bio_) {
        return -1;
    }
    auto ret = BIO_write(net_bio_, data, static_cast<int>(size));
    if (ret <= 0) {
        if (BIO_should_retry(net_bio_)) {
            ret = 0;
        } else {
            ret = -1;
        }
    }
    return ret;
}

int BioHandler::readSslData(void* data, size_t size)
{
    if (!net_bio_) {
        return -1;
    }
    auto ret = BIO_read(net_bio_, data, static_cast<int>(size));
    if (ret <= 0) {
        if (BIO_should_retry(net_bio_)) {
            ret = 0;
        } else {
            ret = -1;
        }
    }
    return ret;
}

KMError BioHandler::trySendSslData()
{
    if (!send_buf_.empty()) {
        auto ret = sendData(send_buf_);
        if (ret < 0) {
            KUMA_ERRXTRACE("trySendSslData, failed to send data");
            return KMError::SOCK_ERROR;
        }
        if (!send_buf_.empty()) {
            return KMError::NOERR; // send blocked
        }
    }
    int bytes_total = 0;
    int ssl_read = 0;
    send_buf_.expand(20 * 1024);
    do {
        ssl_read = readSslData(send_buf_);
        if (ssl_read > 0) {
            auto bytes_sent = sendData(send_buf_);
            if (bytes_sent >= 0) {
                bytes_total += bytes_sent;
                if (bytes_sent < ssl_read) {
                    return KMError::NOERR; // send blocked
                }
            }
            else {
                KUMA_ERRXTRACE("trySendSslData, failed to send data 2");
                return KMError::SOCK_ERROR;
            }
        }
        else if (ssl_read < 0) {
            KUMA_ERRXTRACE("trySendSslData, failed to read SSL data, ret=" << ssl_read);
            return KMError::SSL_FAILED;
        }
    } while (ssl_read > 0);
    
    return KMError::AGAIN; // want ssl write
}

KMError BioHandler::tryRecvSslData()
{
    if (!recv_buf_.empty()) {
        auto ret = writeSslData(recv_buf_);
        if (ret < 0) {
            KUMA_ERRXTRACE("tryRecvSslData, failed to write SSL data");
            return KMError::SSL_FAILED;
        }
        if (!recv_buf_.empty()) {
            return KMError::NOERR;
        }
    }
    
    int bytes_recv = 0;
    int bytes_total = 0;
    recv_buf_.expand(20 * 1024);
    do {
        bytes_recv = recvData(recv_buf_);
        if (bytes_recv > 0) {
            auto ssl_written = writeSslData(recv_buf_);
            if (ssl_written >= 0) {
                bytes_total += ssl_written;
                if (ssl_written < bytes_recv) {
                    return KMError::AGAIN; // want ssl read?
                }
            }
            else { // fatal error
                KUMA_ERRXTRACE("tryRecvSslData, failed to write SSL data 2");
                return KMError::SSL_FAILED;
            }
        }
        else if (bytes_recv < 0) {
            KUMA_ERRXTRACE("tryRecvSslData, failed to recv data");
            return KMError::SOCK_ERROR;
        }
    } while (bytes_recv > 0);
    
    return KMError::NOERR; // recv bocked
}

KMError BioHandler::trySslHandshake()
{
    KUMA_ASSERT(getState() == SslState::SSL_NONE ||
                getState() == SslState::SSL_HANDSHAKE);
    bool try_recv = true;
    bool try_send = true;
    
    do {
        if (try_recv) {
            auto ret = tryRecvSslData();
            if (km_is_fatal_error(ret)) {
                KUMA_ERRXTRACE("trySslHandshake, failed to recv SSL data, ret=" << (int)ret);
                return ret;
            }
            else if (ret != KMError::AGAIN) {
                try_recv = false; // recv blocked
            }
        }
        
        auto ssl_state = doHandshake();
        if (SslState::SSL_ERROR == ssl_state) {
            KUMA_ERRXTRACE("trySslHandshake, SSL handshake failed");
            return KMError::SSL_FAILED;
        }
        
        if (try_send) {
            auto ret = trySendSslData();
            if (km_is_fatal_error(ret)) {
                KUMA_ERRXTRACE("trySslHandshake, failed to send SSL data, ret=" << (int)ret);
                return ret;
            }
            else if (ret != KMError::AGAIN) {
                try_send = false; // send blocked
            }
            else {
                // try handshake again since SSL data may be sent out
                auto ssl_state = doHandshake();
                if (SslState::SSL_ERROR == ssl_state) {
                    KUMA_ERRXTRACE("trySslHandshake, SSL handshake failed 2");
                    return KMError::SSL_FAILED;
                }
                else if (SslState::SSL_SUCCESS == ssl_state) {
                    return KMError::NOERR;
                }
            }
        }
        
    } while (try_recv && try_send);
    
    return KMError::AGAIN;
}

int BioHandler::readSslData(SKBuffer &buf)
{
    auto ret = readSslData(buf.wr_ptr(), buf.space());
    if (ret > 0) {
        buf.bytes_written(ret);
    }
    return ret;
}

int BioHandler::writeSslData(SKBuffer &buf)
{
    auto ret = writeSslData(buf.ptr(), buf.size());
    if (ret > 0) {
        buf.bytes_read(ret);
    }
    return ret;
}

int BioHandler::sendData(SKBuffer &buf)
{
    auto ret = send_func_(buf.ptr(), buf.size());
    if (ret > 0) {
        buf.bytes_read(ret);
    }
    return ret;
}

int BioHandler::recvData(SKBuffer &buf)
{
    auto ret = recv_func_(buf.wr_ptr(), buf.space());
    if (ret > 0) {
        buf.bytes_written(ret);
    }
    return ret;
}

KMError BioHandler::sendBufferedData()
{
    return trySendSslData();
}

#endif // KUMA_HAS_OPENSSL
