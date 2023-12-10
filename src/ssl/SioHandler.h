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

#ifndef __SioHandler_H__
#define __SioHandler_H__

#ifdef KUMA_HAS_OPENSSL

#include "SslHandler.h"

#ifdef KUMA_OS_WIN
namespace kev {
    struct iovec;
}
#else
struct iovec;
#endif

KUMA_NS_BEGIN


class SioHandler : public SslHandler
{
public:
    SioHandler();
    ~SioHandler();
    
    KMError init(SslRole ssl_role, SOCKET_FD fd, uint32_t ssl_flags) override;
    KMError attachSsl(SSL *ssl, BIO *nbio, SOCKET_FD fd) override;
    KMError detachSsl(SSL* &ssl, BIO* &nbio) override;
    SslState handshake() override;
    int send(const void* data, size_t size) override;
    int send(const iovec* iovs, int count) override;
    int send(const KMBuffer &buf) override;
    int receive(void* data, size_t size, KMError *last_error) override;
    KMError close() override;
    
protected:
    SslState sslConnect();
    SslState sslAccept();
};

KUMA_NS_END

#endif // KUMA_HAS_OPENSSL

#endif
