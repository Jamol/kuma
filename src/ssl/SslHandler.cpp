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

#include "SslHandler.h"
#include "libkev/src/util/kmtrace.h"

#include <openssl/x509v3.h>

using namespace kuma;

void SslHandler::cleanup()
{
    if(ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = NULL;
    }
    setState(SslState::SSL_NONE);
}

KMError SslHandler::init(SslRole ssl_role, SOCKET_FD fd, uint32_t ssl_flags)
{
    if (fd == INVALID_FD) {
        return KMError::INVALID_PARAM;
    }
    cleanup();
    is_server_ = ssl_role == SslRole::SERVER;
    fd_ = fd;
    ssl_flags_ = ssl_flags;
    
    obj_key_ += "_" + std::to_string(fd_);
    
    SSL_CTX* ctx = NULL;
    if(is_server_) {
        ctx = OpenSslLib::defaultServerContext();
    } else {
        ctx = OpenSslLib::defaultClientContext();
    }
    if(NULL == ctx) {
        KM_ERRXTRACE("init, CTX is NULL");
        return KMError::SSL_ERROR;
    }
    ssl_ = SSL_new(ctx);
    if(!ssl_) {
        KM_ERRXTRACE("init, SSL_new failed");
        return KMError::SSL_ERROR;
    }
    OpenSslLib::setSSLData(ssl_, this);
    //SSL_set_mode(ssl_, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    //SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE);
#if defined(SSL_MODE_RELEASE_BUFFERS)
    SSL_set_mode(ssl_, SSL_MODE_RELEASE_BUFFERS);
#endif
    return KMError::NOERR;
}

KMError SslHandler::setAlpnProtocols(const AlpnProtos &protocols)
{
#if OPENSSL_VERSION_NUMBER >= 0x1000200fL && !defined(OPENSSL_NO_TLSEXT)
    if (ssl_ && SSL_set_alpn_protos(ssl_, &protocols[0], (unsigned int)protocols.size()) == 0) {
        return KMError::NOERR;
    }
    return KMError::SSL_ERROR;
#else
    return KMError::NOT_SUPPORTED;
#endif
}

KMError SslHandler::getAlpnSelected(std::string &protocol)
{
    if (!ssl_) {
        return KMError::INVALID_STATE;
    }
    const uint8_t *buf = nullptr;
    uint32_t len = 0;
    SSL_get0_alpn_selected(ssl_, &buf, &len);
    if (buf && len > 0) {
        protocol.assign((const char*)buf, len);
    } else {
        protocol.clear();
    }
    return KMError::NOERR;
}

KMError SslHandler::setServerName(const std::string &serverName)
{
#if OPENSSL_VERSION_NUMBER >= 0x1000105fL && !defined(OPENSSL_NO_TLSEXT)
    if (ssl_ && SSL_set_tlsext_host_name(ssl_, serverName.c_str())) {
        return KMError::NOERR;
    }
    return KMError::SSL_ERROR;
#else
    return KMError::NOT_SUPPORTED;
#endif
}

KMError SslHandler::setHostName(const std::string &hostName)
{
    if (ssl_) {
        auto param = SSL_get0_param(ssl_);
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_MULTI_LABEL_WILDCARDS);
        X509_VERIFY_PARAM_set1_host(param, hostName.c_str(), hostName.size());
        return KMError::NOERR;
    }
    return KMError::SSL_ERROR;
}

#endif

