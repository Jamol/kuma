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

using namespace kuma;

SSL_CTX* OpenSslLib::ssl_ctx_client_ = NULL;
SSL_CTX* OpenSslLib::ssl_ctx_server_ = NULL;
std::mutex* OpenSslLib::ssl_locks_ = NULL;

bool OpenSslLib::init(const char* path)
{
    if(ssl_ctx_client_ || ssl_ctx_server_) {
        return true;
    }
    
    std::string cert_path;
    if(path == NULL) {
        cert_path = getCurrentModulePath();
    } else {
        cert_path = path;
        if(cert_path.empty()) {
            cert_path = getCurrentModulePath();
        } else if(cert_path.at(cert_path.length() - 1) != PATH_SEPARATOR) {
            cert_path += PATH_SEPARATOR;
        }
    }
    
    std::string server_cert_file = cert_path + "server.cer";
    std::string server_key_file = cert_path + "server.key";
    std::string client_cert_file;// = cert_path + "cleint.cer";
    std::string client_key_file;// = cert_path + "client.key";
    std::string ca_cert_file = cert_path + "ca.cer";
    
    SSL_library_init();
    //OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    bool server_ctx_ok = false;
    bool client_ctx_ok = false;
    do {
        ssl_ctx_server_ = SSL_CTX_new(SSLv23_server_method());
        if(NULL == ssl_ctx_server_) {
            KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_new failed, err="<<ERR_reason_error_string(ERR_get_error()));
            break;
        }
        
        //const long flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
        //SSL_CTX_set_options(ssl_ctx_server_, flags);
        SSL_CTX_set_options(ssl_ctx_server_, SSL_OP_NO_SSLv2);
        SSL_CTX_set_mode(ssl_ctx_server_, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        SSL_CTX_set_mode(ssl_ctx_server_, SSL_MODE_ENABLE_PARTIAL_WRITE);
        
        if(!server_cert_file.empty() && !server_key_file.empty()) {
            if(SSL_CTX_use_certificate_file(ssl_ctx_server_, server_cert_file.c_str(), SSL_FILETYPE_PEM) != 1) {
                KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_use_certificate_file failed, file="<<server_cert_file
                               <<", err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
            if(SSL_CTX_use_PrivateKey_file(ssl_ctx_server_, server_key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
                KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_use_PrivateKey_file failed, file:"<<server_key_file
                               <<", err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
            if(SSL_CTX_check_private_key(ssl_ctx_server_) != 1) {
                KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_check_private_key failed, err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
        }
        
        /*
         SSL_CTX_set_verify(ssl_ctx_client_, SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verifyCallback);
         app_verify_arg arg0;
         SSL_CTX_set_cert_verify_callback(ssl_ctx_server_, appVerifyCallback, &arg0);
         
         int session_id_context = 1;
         if(SSL_CTX_set_session_id_context(ssl_ctx_server_, (unsigned char *)&session_id_context, sizeof(session_id_context)) != 1)
         {
         }
         */
        server_ctx_ok = true;
    } while(0);
    
    do {
        ssl_ctx_client_ = SSL_CTX_new(SSLv23_client_method());
        if(NULL == ssl_ctx_client_) {
            KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_new failed, err="<<ERR_reason_error_string(ERR_get_error()));
            break;
        }
        
        SSL_CTX_set_verify(ssl_ctx_client_, SSL_VERIFY_PEER, verifyCallback);
        //SSL_CTX_set_verify_depth(ssl_ctx_client_, 4);
        //app_verify_arg arg1;
        //SSL_CTX_set_cert_verify_callback(ssl_ctx_client_, appVerifyCallback, &arg1);
        
        //const long flags = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION;
        //SSL_CTX_set_options(ssl_ctx_client_, flags);
        SSL_CTX_set_options(ssl_ctx_client_, SSL_OP_NO_SSLv2);
        SSL_CTX_set_mode(ssl_ctx_client_, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        SSL_CTX_set_mode(ssl_ctx_client_, SSL_MODE_ENABLE_PARTIAL_WRITE);
        
        // set AES256_SHA cipher for client.
        //if(SSL_CTX_set_cipher_list(ssl_ctx_client_,"AES256-SHA") != 1)
        //{
        //	KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_set_cipher_list failed, err="<<ERR_reason_error_string(ERR_get_error()));
        //}
        
        if(!client_cert_file.empty() && !client_key_file.empty()) {
            if(SSL_CTX_use_certificate_file(ssl_ctx_client_, client_cert_file.c_str(), SSL_FILETYPE_PEM) != 1) {
                KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_use_certificate_file failed, file="<<client_cert_file
                               <<", err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
            if(SSL_CTX_use_PrivateKey_file(ssl_ctx_client_, client_key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
                KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_use_PrivateKey_file failed, file="<<client_key_file
                               <<", err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
            if(SSL_CTX_check_private_key(ssl_ctx_client_) != 1) {
                KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_check_private_key failed, err="<<ERR_reason_error_string(ERR_get_error()));
                break;
            }
        }
        
        if(!ca_cert_file.empty() &&
           SSL_CTX_load_verify_locations(ssl_ctx_client_, ca_cert_file.c_str(), NULL) != 1) {
            KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_load_verify_locations failed, file="<<ca_cert_file
                           <<", err="<<ERR_reason_error_string(ERR_get_error()));
            break;
        }
        if(SSL_CTX_set_default_verify_paths(ssl_ctx_client_) != 1) {
            KUMA_WARNTRACE("OpenSslLib::init, SSL_CTX_set_default_verify_paths failed, err="
                           <<ERR_reason_error_string(ERR_get_error()));
            break;
        }
        client_ctx_ok = true;
    } while(0);
    
    if(!server_ctx_ok && ssl_ctx_server_) {
        SSL_CTX_free(ssl_ctx_server_);
        ssl_ctx_server_ = NULL;
    }
    if(!client_ctx_ok && ssl_ctx_client_) {
        SSL_CTX_free(ssl_ctx_client_);
        ssl_ctx_client_ = NULL;
    }
    if(!server_ctx_ok && !client_ctx_ok) {
        return false;
    }
    
    ssl_locks_ = new std::mutex[CRYPTO_num_locks()];
    CRYPTO_set_id_callback(threadIdCallback);
    CRYPTO_set_locking_callback(lockingCallback);
    
    // PRNG
    RAND_poll();
    while(RAND_status() == 0) {
        unsigned short rand_ret = rand() % 65536;
        RAND_seed(&rand_ret, sizeof(rand_ret));
    }
    
    return true;
}

void OpenSslLib::fini()
{
    
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

#endif // KUMA_HAS_OPENSSL
