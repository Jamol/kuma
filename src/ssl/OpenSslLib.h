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

#ifndef __OpenSslLib_H__
#define __OpenSslLib_H__

#include "kmdefs.h"

#include <mutex>

#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/engine.h>

KUMA_NS_BEGIN

class OpenSslLib
{
public:
    static bool init();
    
    static int verifyCallback(int ok, X509_STORE_CTX *ctx);
    static int appVerifyCallback(X509_STORE_CTX *ctx, void *arg);
    static unsigned long threadIdCallback(void);
    static void lockingCallback(int mode, int n, const char *file, int line);
    
    static SSL_CTX* getClientContext() { return ssl_ctx_client_; }
    static SSL_CTX* getServerContext() { return ssl_ctx_server_; }
    
protected:
    static SSL_CTX*     ssl_ctx_client_;
    static SSL_CTX*     ssl_ctx_server_;
    static std::mutex*  ssl_locks_;
};

KUMA_NS_END

#endif
