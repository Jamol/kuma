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

#include "OpenSslLib.h"
#include "util/kmtrace.h"
#include "util/util.h"

#include <string>
#include <thread>
#include <sstream>
#include <vector>

using namespace kuma;

bool OpenSslLib::initialized_ = false;
std::string OpenSslLib::certs_path_;
SSL_CTX* OpenSslLib::ssl_ctx_client_ = nullptr;
std::once_flag OpenSslLib::once_flag_client_;
SSL_CTX* OpenSslLib::ssl_ctx_server_ = nullptr;
std::once_flag OpenSslLib::once_flag_server_;
std::mutex* OpenSslLib::ssl_locks_ = nullptr;

KUMA_NS_BEGIN
static const AlpnProtos alpnProtos {2, 'h', '2'};
KUMA_NS_END

bool OpenSslLib::init(const char* path)
{
    if (initialized_) {
        return true;
    }
    if(!path) {
        certs_path_ = getCurrentModulePath();
    } else {
        certs_path_ = path;
        if(certs_path_.empty()) {
            certs_path_ = getCurrentModulePath();
        } else if(certs_path_.at(certs_path_.length() - 1) != PATH_SEPARATOR) {
            certs_path_ += PATH_SEPARATOR;
        }
    }
    
    SSL_library_init();
    //OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    ssl_locks_ = new std::mutex[CRYPTO_num_locks()];
    CRYPTO_set_id_callback(threadIdCallback);
    CRYPTO_set_locking_callback(lockingCallback);
    
    // PRNG
    RAND_poll();
    while(RAND_status() == 0) {
        unsigned short rand_ret = rand() % 65536;
        RAND_seed(&rand_ret, sizeof(rand_ret));
    }
    initialized_ = true;
    return true;
}

SSL_CTX* OpenSslLib::createSSLContext(const SSL_METHOD *method, const std::string &caFile, const std::string &certFile, const std::string &keyFile, bool clientMode)
{
    SSL_CTX *ssl_ctx = nullptr;
    bool ctx_ok = false;
    do {
        ssl_ctx = SSL_CTX_new(method);
        if(!ssl_ctx) {
            KUMA_WARNTRACE("SSL_CTX_new failed, err="<<ERR_reason_error_string(ERR_get_error()));
            break;
        }
        
#if 1
        if (SSL_CTX_set_ecdh_auto(ssl_ctx, 1) != 1) {
            KUMA_WARNTRACE("SSL_CTX_set_ecdh_auto failed, err="<<ERR_reason_error_string(ERR_get_error()));
        }
#endif
        
        long flags = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_COMPRESSION;
        // SSL_OP_SAFARI_ECDHE_ECDSA_BUG
        SSL_CTX_set_options(ssl_ctx, flags);
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
        
#if 0
        if (clientMode) {
            const char* cipherList =
            "ECDHE-RSA-AES128-GCM-SHA256"
            "ECDHE-RSA-AES256-GCM-SHA384"
            "ECDHE-ECDSA-AES128-GCM-SHA256"
            "ECDHE-ECDSA-AES256-GCM-SHA384"
            "DHE-RSA-AES128-GCM-SHA256"
            "DHE-DSS-AES128-GCM-SHA256"
            "DHE-RSA-AES256-GCM-SHA384"
            "DHE-DSS-AES256-GCM-SHA384"
            ;
            if (SSL_CTX_set_cipher_list(ssl_ctx, "ECDHE-RSA-AES128-GCM-SHA256") != 1) {
                KUMA_WARNTRACE("SSL_CTX_set_cipher_list failed, err="<<ERR_reason_error_string(ERR_get_error()));
            }
        }
#endif
        
        if(!certFile.empty() && !keyFile.empty()) {
            //if(SSL_CTX_use_certificate_file(ssl_ctx, certFile.c_str(), SSL_FILETYPE_PEM) != 1) {
            if(SSL_CTX_use_certificate_chain_file(ssl_ctx, certFile.c_str()) != 1) {
                KUMA_WARNTRACE("SSL_CTX_use_certificate_chain_file failed, file="<<certFile<<", err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
            if(SSL_CTX_use_PrivateKey_file(ssl_ctx, keyFile.c_str(), SSL_FILETYPE_PEM) != 1) {
                KUMA_WARNTRACE("SSL_CTX_use_PrivateKey_file failed, file="<<keyFile<<", err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
            if(SSL_CTX_check_private_key(ssl_ctx) != 1) {
                KUMA_WARNTRACE("SSL_CTX_check_private_key failed, err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
        }
        
        if (!caFile.empty()) {
            if(SSL_CTX_load_verify_locations(ssl_ctx, caFile.c_str(), NULL) != 1) {
                KUMA_WARNTRACE("SSL_CTX_load_verify_locations failed, file="<<caFile<<", err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
            if(SSL_CTX_set_default_verify_paths(ssl_ctx) != 1) {
                KUMA_WARNTRACE("SSL_CTX_set_default_verify_paths failed, err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, verifyCallback);
            //SSL_CTX_set_verify_depth(ssl_ctx, 4);
            //app_verify_arg arg1;
            //SSL_CTX_set_cert_verify_callback(ssl_ctx, appVerifyCallback, &arg1);
        }
        if (!clientMode) {
            SSL_CTX_set_alpn_select_cb(ssl_ctx, alpnCallback, (void*)&alpnProtos);
        }
        ctx_ok = true;
    } while(0);
    if(!ctx_ok && ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
    }
    return ssl_ctx;
}

SSL_CTX* OpenSslLib::getClientContext()
{
    if (!ssl_ctx_client_) {
        std::call_once(once_flag_client_, []{
            std::string certFile;// = certs_path + "cleint.cer";
            std::string keyFile;// = certs_path + "client.key";
            std::string caFile = certs_path_ + "ca.cer";
            ssl_ctx_client_ = createSSLContext(SSLv23_client_method(), caFile, certFile, keyFile, true);
        });
    }
    return ssl_ctx_client_;
}

SSL_CTX* OpenSslLib::getServerContext()
{
    if (!ssl_ctx_server_) {
        std::call_once(once_flag_server_, []{
            std::string certFile = certs_path_ + "server.cer";
            std::string keyFile = certs_path_ + "server.key";
            std::string caFile;
            ssl_ctx_server_ = createSSLContext(SSLv23_server_method(), caFile, certFile, keyFile, false);
        });
    }
    return ssl_ctx_server_;
}

void OpenSslLib::fini()
{
    EVP_cleanup();
    ERR_free_strings();
}

struct app_verify_arg
{
    char *string;
    int app_verify;
    int allow_proxy_certs;
    char *proxy_auth;
    char *proxy_cond;
};

int OpenSslLib::verifyCallback(int ok, X509_STORE_CTX *ctx)
{
    if(NULL == ctx) {
        return -1;
    }
    if(ctx->current_cert) {
        char *s, buf[1024];
        s = X509_NAME_oneline(X509_get_subject_name(ctx->current_cert), buf, sizeof(buf));
        if(s != NULL) {
            if(ok) {
                KUMA_INFOTRACE("verifyCallback ok, depth="<<ctx->error_depth<<", subject="<<buf);
                if(X509_NAME_oneline(X509_get_issuer_name(ctx->current_cert), buf, sizeof(buf))) {
                    KUMA_INFOTRACE("verifyCallback, issuer="<<buf);
                }
            } else {
                KUMA_ERRTRACE("verifyCallback failed, depth="<<ctx->error_depth
                              <<", err="<<ctx->error<<", subject="<<buf);
            }
        }
    }
    
    if (0 == ok) {
        KUMA_INFOTRACE("verifyCallback, err="<<X509_verify_cert_error_string(ctx->error));
        switch (ctx->error)
        {
                //case X509_V_ERR_CERT_NOT_YET_VALID:
                //case X509_V_ERR_CERT_HAS_EXPIRED:
            case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
                KUMA_INFOTRACE("verifyCallback, ... ignored, err="<<ctx->error);
                ok = 1;
                break;
        }
    }
    
    return ok;
}

int OpenSslLib::appVerifyCallback(X509_STORE_CTX *ctx, void *arg)
{
    if(!ctx || !arg) {
        return -1;
    }
    
    int ok = 1;
    struct app_verify_arg *cb_arg = (struct app_verify_arg *)arg;
    
    if (cb_arg->app_verify) {
        char *s = NULL, buf[256];
        if(ctx->cert) {
            s = X509_NAME_oneline(X509_get_subject_name(ctx->cert), buf, 256);
        }
        if(s != NULL) {
            KUMA_INFOTRACE("appVerifyCallback, depth="<<ctx->error_depth<<", "<<buf);
        }
        return 1;
    }
    
    ok = X509_verify_cert(ctx);
    
    return ok;
}

unsigned long OpenSslLib::threadIdCallback(void)
{
#if 0
    unsigned long ret = 0;
    std::thread::id thread_id = std::this_thread::get_id();
    std::stringstream ss;
    ss << thread_id;
    ss >> ret;
    return ret;
#else
    return getCurrentThreadId();
#endif
}

void OpenSslLib::lockingCallback(int mode, int n, const char *file, int line)
{
    if (mode & CRYPTO_LOCK) {
        ssl_locks_[n].lock();
    }
    else {
        ssl_locks_[n].unlock();
    }
}

int OpenSslLib::alpnCallback(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *_in, unsigned int inlen,
                          void *arg)
{
    const AlpnProtos *protos = (AlpnProtos*)arg;
    
    if (SSL_select_next_proto((unsigned char**) out, outlen, &(*protos)[0], (unsigned int)protos->size(), _in, inlen) != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_NOACK;
    }
    return SSL_TLSEXT_ERR_OK;
}

#endif // KUMA_HAS_OPENSSL
