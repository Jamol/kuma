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

#ifdef KUMA_HAS_OPENSSL

#include "kmdefs.h"

#include <mutex>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

KUMA_NS_BEGIN

using AlpnProtos = std::vector<uint8_t>;

class OpenSslLib
{
public:
    static bool init(const char* path);
    static void fini();
    
    static int verifyCallback(int ok, X509_STORE_CTX *ctx);
    static int appVerifyCallback(X509_STORE_CTX *ctx, void *arg);
    static unsigned long threadIdCallback(void);
    static void lockingCallback(int mode, int n, const char *file, int line);
    static int alpnCallback(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *_in, unsigned int inlen, void *_protocols);
    static int passwdCallback(char *buf, int size, int rwflag, void *userdata);
    
    static SSL_CTX* getClientContext();
    static SSL_CTX* getServerContext();
    
private:
    static bool init(const std::string &path);
    static SSL_CTX* createSSLContext(const SSL_METHOD *method, const std::string &ca, const std::string &cert, const std::string &key, bool clientMode);
    
protected:
    using SSL_CTX_ptr = std::unique_ptr<SSL_CTX, decltype(&::SSL_CTX_free)>;
    static bool             initialized_;
    static std::once_flag   once_flag_init_;
    static std::string      certs_path_;
    static SSL_CTX*         ssl_ctx_client_;
    static std::once_flag   once_flag_client_;
    static SSL_CTX*         ssl_ctx_server_;
    static std::once_flag   once_flag_server_;
    static std::mutex*      ssl_locks_;
};

KUMA_NS_END

#endif // KUMA_HAS_OPENSSL

#endif
