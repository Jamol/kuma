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

#ifdef KUMA_HAS_OPENSSL

#include "OpenSslLib.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "SslHandler.h"

#include <string>
#include <thread>
#include <sstream>
#include <vector>

using namespace kuma;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
struct CRYPTO_dynlock_value {
    std::mutex lock;
};

#define X509_STORE_CTX_get0_cert(ctx) ctx->cert
#endif

bool OpenSslLib::initialized_ = false;
std::string OpenSslLib::certs_path_;
std::uint32_t OpenSslLib::init_ref_ { 0 };

SSL_CTX* OpenSslLib::ssl_ctx_client_ = nullptr;
std::once_flag OpenSslLib::once_flag_client_;
SSL_CTX* OpenSslLib::ssl_ctx_server_ = nullptr;
std::once_flag OpenSslLib::once_flag_server_;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
std::mutex* OpenSslLib::ssl_locks_ = nullptr;
#endif

int OpenSslLib::ssl_index_ = -1;

namespace {
    const AlpnProtos alpnProtos {
        2, 'h', '2',
        8, 'h', 't', 't', 'p', '/', '1', '.', '1'
    };
    
    std::mutex& getOpenSslMutex()
    {
        static std::mutex m;
        return m;
    }
}

bool OpenSslLib::init(const std::string &cfg_path)
{
    std::lock_guard<std::mutex> g(getOpenSslMutex());
    if (initialized_) {
        ++init_ref_;
        return true;
    }
    if (doInit(cfg_path)) {
        initialized_ = true;
        ++init_ref_;
        return true;
    }
    return false;
}

bool OpenSslLib::doInit(const std::string &cfg_path)
{
    certs_path_ = cfg_path;
    if(certs_path_.empty()) {
        certs_path_ = getExecutablePath();
    }
    certs_path_ += "/cert";
    if(certs_path_.at(certs_path_.length() - 1) != PATH_SEPARATOR) {
        certs_path_ += PATH_SEPARATOR;
    }
    
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    if (OPENSSL_init_ssl(0, NULL) == 0) {
        return false;
    }
    ERR_clear_error();
#else
    if (CRYPTO_get_locking_callback() == NULL) {
        ssl_locks_ = new std::mutex[CRYPTO_num_locks()];
        CRYPTO_set_id_callback(threadIdCallback);
        CRYPTO_set_locking_callback(lockingCallback);

        CRYPTO_set_dynlock_create_callback(&dynlockCreateCallback);
        CRYPTO_set_dynlock_lock_callback(&dynlockLockingCallback);
        CRYPTO_set_dynlock_destroy_callback(&dynlockDestroyCallback);
    }
    
    if (SSL_library_init() != 1) {
        return false;
    }
    //OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
#endif
    
    ERR_load_BIO_strings();
    
    // PRNG
    RAND_poll();
    while(RAND_status() == 0) {
        unsigned short rand_ret = rand() % 65536;
        RAND_seed(&rand_ret, sizeof(rand_ret));
    }
    ssl_index_ = SSL_get_ex_new_index(0, (void*)"SSL data index", NULL, NULL, NULL);
    return true;
}

void OpenSslLib::fini()
{
    std::lock_guard<std::mutex> g(getOpenSslMutex());
    if (--init_ref_ == 0) {
        doFini();
        initialized_ = false;
    }
}

void OpenSslLib::doFini()
{
    // will automatically release the resource on openssl 1.1
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    if (ssl_locks_) {
        CRYPTO_set_id_callback(nullptr);
        CRYPTO_set_locking_callback(nullptr);

        CRYPTO_set_dynlock_create_callback(nullptr);
        CRYPTO_set_dynlock_lock_callback(nullptr);
        CRYPTO_set_dynlock_destroy_callback(nullptr);
        delete [] ssl_locks_;
        ssl_locks_ = nullptr;
    }
    EVP_cleanup();
    CRYPTO_cleanup_all_ex_data();
    ERR_free_strings();
#endif
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
        
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        if (SSL_CTX_set_ecdh_auto(ssl_ctx, 1) != 1) {
            KUMA_WARNTRACE("SSL_CTX_set_ecdh_auto failed, err="<<ERR_reason_error_string(ERR_get_error()));
        }
#endif
        
        long flags = SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;
        flags |= SSL_OP_NO_COMPRESSION;
        // SSL_OP_SAFARI_ECDHE_ECDSA_BUG
        SSL_CTX_set_options(ssl_ctx, flags);
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);
#if defined(SSL_MODE_RELEASE_BUFFERS)
        SSL_CTX_set_mode(ssl_ctx, SSL_MODE_RELEASE_BUFFERS);
#endif
        
#if 1
        if (clientMode) {
            const char* kDefaultCipherList =
            "ECDHE-ECDSA-AES256-GCM-SHA384:"
            "ECDHE-RSA-AES256-GCM-SHA384:"
            "ECDHE-ECDSA-CHACHA20-POLY1305:"
            "ECDHE-RSA-CHACHA20-POLY1305:"
            "ECDHE-ECDSA-AES128-GCM-SHA256:"
            "ECDHE-RSA-AES128-GCM-SHA256:"
            "ECDHE-ECDSA-AES256-SHA384:"
            "ECDHE-RSA-AES256-SHA384:"
            "ECDHE-ECDSA-AES128-SHA256:"
            "ECDHE-RSA-AES128-SHA256";
            if (SSL_CTX_set_cipher_list(ssl_ctx, kDefaultCipherList) != 1) {
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
            SSL_CTX_set_default_passwd_cb(ssl_ctx, passwdCallback);
            SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, ssl_ctx);
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
#if OPENSSL_VERSION_NUMBER >= 0x1000200fL && !defined(OPENSSL_NO_TLSEXT)
            SSL_CTX_set_alpn_select_cb(ssl_ctx, alpnCallback, (void*)&alpnProtos);
#endif
            
#if OPENSSL_VERSION_NUMBER >= 0x1000105fL && !defined(OPENSSL_NO_TLSEXT)
            SSL_CTX_set_tlsext_servername_callback(ssl_ctx, serverNameCallback);
            SSL_CTX_set_tlsext_servername_arg(ssl_ctx, ssl_ctx);
#endif
        }
        //SSL_CTX_set_max_send_fragment(ssl_ctx, 8192);
        ctx_ok = true;
    } while(0);
    if(!ctx_ok && ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = nullptr;
    }
    return ssl_ctx;
}

SSL_CTX* OpenSslLib::defaultClientContext()
{
    if (!ssl_ctx_client_) {
        std::call_once(once_flag_client_, []{
            std::string certFile;// = certs_path + "cleint.pem";
            std::string keyFile;// = certs_path + "client.key";
            std::string caFile = certs_path_ + "ca.pem";
            ssl_ctx_client_ = createSSLContext(SSLv23_client_method(), caFile, certFile, keyFile, true);
        });
    }
    return ssl_ctx_client_;
}

SSL_CTX* OpenSslLib::defaultServerContext()
{
    if (!ssl_ctx_server_) {
        std::call_once(once_flag_server_, []{
            std::string certFile = certs_path_ + "server.pem";
            std::string keyFile = certs_path_ + "server.key";
            std::string caFile;
            ssl_ctx_server_ = createSSLContext(SSLv23_server_method(), caFile, certFile, keyFile, false);
        });
    }
    return ssl_ctx_server_;
}

SSL_CTX* OpenSslLib::getSSLContext(const char *hostName)
{
    return defaultServerContext();
}

int OpenSslLib::setSSLData(SSL* ssl, void *data)
{
    return SSL_set_ex_data(ssl, ssl_index_, data);
}

void* OpenSslLib::getSSLData(SSL* ssl)
{
    return SSL_get_ex_data(ssl, ssl_index_);
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
    SSL* ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
    //SSL_CTX* ssl_ctx = ::SSL_get_SSL_CTX(ssl);
    uint32_t ssl_flags = 0;
    auto ssl_data = getSSLData(ssl);
    if (ssl_data) {
        auto handler = reinterpret_cast<SslHandler*>(ssl_data);
        ssl_flags = handler->getSslFlags();
    }
    auto x509_current_cert = X509_STORE_CTX_get_current_cert(ctx);
    auto x509_err = X509_STORE_CTX_get_error(ctx);
    if(x509_current_cert) {
        char *s, buf[1024];
        s = X509_NAME_oneline(X509_get_subject_name(x509_current_cert), buf, sizeof(buf));
        if(s != NULL) {
            auto x509_err_depth = X509_STORE_CTX_get_error_depth(ctx);
            if(ok) {
                KUMA_INFOTRACE("verifyCallback ok, depth="<<x509_err_depth<<", subject="<<buf);
                if(X509_NAME_oneline(X509_get_issuer_name(x509_current_cert), buf, sizeof(buf))) {
                    KUMA_INFOTRACE("verifyCallback, issuer="<<buf);
                }
            } else {
                KUMA_ERRTRACE("verifyCallback failed, depth="<<x509_err_depth
                              <<", err="<<x509_err<<", subject="<<buf);
            }
        }
    }
    
    if (0 == ok) {
        KUMA_INFOTRACE("verifyCallback, err="<<X509_verify_cert_error_string(x509_err));
        switch (x509_err)
        {
                //case X509_V_ERR_CERT_NOT_YET_VALID:
            case X509_V_ERR_CERT_HAS_EXPIRED:
                if (ssl_flags & SSL_ALLOW_EXPIRED_CERT) {
                    ok = 1;
                }
                break;
            case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
                if (ssl_flags & SSL_ALLOW_SELF_SIGNED_CERT) {
                    KUMA_INFOTRACE("verifyCallback, ... ignored, err="<<x509_err);
                    ok = 1;
                }
                break;
            case X509_V_ERR_CERT_REVOKED:
                if (ssl_flags & SSL_ALLOW_REVOKED_CERT) {
                    ok = 1;
                }
                break;
            case X509_V_ERR_INVALID_CA:
                if (ssl_flags & SSL_ALLOW_ANY_ROOT) {
                    ok = 1;
                }
                break;
            case X509_V_ERR_CERT_UNTRUSTED:
                if (ssl_flags & SSL_ALLOW_UNTRUSTED_CERT) {
                    ok = 1;
                }
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
        auto x509_cert = X509_STORE_CTX_get0_cert(ctx);
        if(x509_cert) {
            s = X509_NAME_oneline(X509_get_subject_name(x509_cert), buf, 256);
        }
        if(s != NULL) {
            auto x509_err_depth = X509_STORE_CTX_get_error_depth(ctx);
            KUMA_INFOTRACE("appVerifyCallback, depth="<<x509_err_depth<<", "<<buf);
        }
        return 1;
    }
    
    ok = X509_verify_cert(ctx);
    
    return ok;
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L
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
    UNUSED(file);
    UNUSED(line);
    
    if (mode & CRYPTO_LOCK) {
        ssl_locks_[n].lock();
    }
    else {
        ssl_locks_[n].unlock();
    }
}

CRYPTO_dynlock_value* OpenSslLib::dynlockCreateCallback(const char *file, int line)
{
    UNUSED(file);
    UNUSED(line);

    return new CRYPTO_dynlock_value;
}

void OpenSslLib::dynlockLockingCallback(int mode, CRYPTO_dynlock_value* l, const char *file, int line)
{
    UNUSED(file);
    UNUSED(line);

    if (mode & CRYPTO_LOCK)
        l->lock.lock();
    else
        l->lock.unlock();
}

void OpenSslLib::dynlockDestroyCallback(CRYPTO_dynlock_value* l, const char *file, int line)
{
    UNUSED(file);
    UNUSED(line);

    delete l;
}
#endif

#if OPENSSL_VERSION_NUMBER >= 0x1000200fL && !defined(OPENSSL_NO_TLSEXT)
int OpenSslLib::alpnCallback(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *_in, unsigned int inlen, void *arg)
{
    const AlpnProtos *protos = (AlpnProtos*)arg;
    
    if (SSL_select_next_proto((unsigned char**) out, outlen, &(*protos)[0], (unsigned int)protos->size(), _in, inlen) != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_NOACK;
    }
    return SSL_TLSEXT_ERR_OK;
}
#endif

#if OPENSSL_VERSION_NUMBER >= 0x1000105fL && !defined(OPENSSL_NO_TLSEXT)
int OpenSslLib::serverNameCallback(SSL *ssl, int *ad, void *arg)
{
    UNUSED(ad);
    
    if (!ssl) {
        return SSL_TLSEXT_ERR_NOACK;
    }
    
    const char *serverName = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (serverName) {
        SSL_CTX *ssl_ctx_old = reinterpret_cast<SSL_CTX*>(arg);
        SSL_CTX *ssl_ctx_new = getSSLContext(serverName);
        if (ssl_ctx_new != ssl_ctx_old) {
            SSL_set_SSL_CTX(ssl, ssl_ctx_new);
        }
    }
    return SSL_TLSEXT_ERR_NOACK;
}
#endif

int OpenSslLib::passwdCallback(char *buf, int size, int rwflag, void *userdata)
{
    return 0;
}

#endif // KUMA_HAS_OPENSSL
