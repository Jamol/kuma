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
