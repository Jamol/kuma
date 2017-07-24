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

#ifndef __OpenSslLib_H__
#define __OpenSslLib_H__

#ifdef KUMA_HAS_OPENSSL

#include "kmdefs.h"

#include <mutex>
#include <vector>
#include <memory>

#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

KUMA_NS_BEGIN

using AlpnProtos = std::vector<uint8_t>;

class OpenSslLib
{
public:
    static bool init(const std::string &path);
    static void fini();
    
    static int verifyCallback(int ok, X509_STORE_CTX *ctx);
    static int appVerifyCallback(X509_STORE_CTX *ctx, void *arg);
    
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    static unsigned long threadIdCallback(void);
    static void lockingCallback(int mode, int n, const char *file, int line);

    static CRYPTO_dynlock_value* dynlockCreateCallback(const char *file, int line);
    static void dynlockLockingCallback(int mode, CRYPTO_dynlock_value* l, const char *file, int line);
    static void dynlockDestroyCallback(CRYPTO_dynlock_value* l, const char *file, int line);
#endif
    
#if OPENSSL_VERSION_NUMBER >= 0x1000200fL && !defined(OPENSSL_NO_TLSEXT)
    static int alpnCallback(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *_in, unsigned int inlen, void *arg);
#endif
    
#if OPENSSL_VERSION_NUMBER >= 0x1000105fL && !defined(OPENSSL_NO_TLSEXT)
    static int serverNameCallback(SSL *ssl, int *ad, void *arg);
#endif
    
    static int passwdCallback(char *buf, int size, int rwflag, void *userdata);
    
    static SSL_CTX* defaultClientContext();
    static SSL_CTX* defaultServerContext();
    static SSL_CTX* getSSLContext(const char *hostName);
    
    static int setSSLData(SSL* ssl, void *data);
    static void* getSSLData(SSL* ssl);
    
private:
    static bool doInit(const std::string &path);
    static void doFini();
    static SSL_CTX* createSSLContext(const SSL_METHOD *method, const std::string &ca, const std::string &cert, const std::string &key, bool clientMode);
    
protected:
    using SSL_CTX_ptr = std::unique_ptr<SSL_CTX, decltype(&::SSL_CTX_free)>;
    static bool                 initialized_;
    static std::uint32_t        init_ref_;
    static std::string          certs_path_;
    
    static SSL_CTX*             ssl_ctx_client_; // default client SSL context
    static std::once_flag       once_flag_client_;
    static SSL_CTX*             ssl_ctx_server_; // default server SSL context
    static std::once_flag       once_flag_server_;
    
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    static std::mutex*          ssl_locks_;
#endif
    
    static int                  ssl_index_;
};

KUMA_NS_END

#endif // KUMA_HAS_OPENSSL

#endif
