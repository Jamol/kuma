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

#ifndef __SslHandler_H__
#define __SslHandler_H__

#ifdef KUMA_HAS_OPENSSL

#include "kmdefs.h"
#include "evdefs.h"
#include "OpenSslLib.h"

KUMA_NS_BEGIN

class SslHandler
{
public:
    enum class SslState {
        SSL_NONE        =  0,
        SSL_HANDSHAKE   =  1,
        SSL_SUCCESS     =  2,
        SSL_ERROR       = -1,
    };
    virtual ~SslHandler() {}
    virtual KMError setAlpnProtocols(const AlpnProtos &protocols) = 0;
    virtual KMError getAlpnSelected(std::string &proto) = 0;
    virtual KMError setServerName(const std::string &serverName) = 0;
    virtual KMError setHostName(const std::string &hostName) = 0;
    
    virtual KMError init(SslRole ssl_role, SOCKET_FD fd) = 0;
    virtual KMError attachSsl(SSL *ssl, BIO *nbio, SOCKET_FD fd) = 0;
    virtual KMError detachSsl(SSL* &ssl, BIO* &nbio) = 0;
    virtual SslState handshake() = 0;
    virtual int send(const void* data, size_t size) = 0;
    virtual int send(const iovec* iovs, int count) = 0;
    virtual int receive(void* data, size_t size) = 0;
    virtual KMError close() = 0;
    
    virtual KMError sendBufferedData() { return KMError::NOERR; }
    SslState getState() { return state_; }
    bool isServer() { return is_server_; }
    
protected:
    void setState(SslState state) { state_ = state; }
    
protected:
    SSL*        ssl_ = nullptr;
    SslState    state_ = SslState::SSL_NONE;
    bool        is_server_ = false;
};

KUMA_NS_END
#endif // KUMA_HAS_OPENSSL
#endif // __SslHandler_H__
