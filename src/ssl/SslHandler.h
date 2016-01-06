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

#ifndef __SslHandler_H__
#define __SslHandler_H__

#ifdef KUMA_HAS_OPENSSL

#include "kmdefs.h"
#include "evdefs.h"
#include "OpenSslLib.h"

struct iovec;

KUMA_NS_BEGIN

class SslHandler
{
public:
    enum SslState{
        SSL_NONE        = 0,
        SSL_HANDSHAKE   = 1,
        SSL_SUCCESS     = 2,
        SSL_ERROR       = -1,
    };
    
public:
    SslHandler();
    ~SslHandler();
    
    int attachFd(SOCKET_FD fd, bool is_server);
    SslState doSslHandshake();
    int send(const uint8_t* data, uint32_t size);
    int send(const iovec* iovs, uint32_t count, bool& eagain);
    int receive(uint8_t* data, uint32_t size);
    int close();
    
    bool isServer() { return is_server_; }
    
    SslState getState() { return state_; }
    
protected:
    const char* getObjKey();
    
private:
    SslState sslConnect();
    SslState sslAccept();
    void setState(SslState state) { state_ = state; }
    void cleanup();
    
private:
    SOCKET_FD   fd_;
    SSL*        ssl_;
    SslState    state_;
    bool        is_server_;
};

KUMA_NS_END

#endif // KUMA_HAS_OPENSSL

#endif
