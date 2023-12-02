#include "utils/km-jni.h"
#include "utils/kmutils-jni.h"
#include "utils/scoped_java_ref.h"
#include "kmapi.h"

#include <queue>
#include <mutex>

KUMA_NS_USING
KUMA_JNI_NS_USING

kuma::EventLoop* get_main_loop();

KUMA_JNI_NS_BEGIN

class TcpSocketJNI final
{
public:
    TcpSocketJNI(JNIEnv *env, jobject jcb, kuma::EventLoop *loop)
            : event_loop_(loop)
            , tcp_(event_loop_)
            , jcb_(env, jcb)
    {
        tcp_.setReadCallback([this] (KMError err) { onReceive(err); });
        tcp_.setWriteCallback([this] (KMError err) { onSend(err); });
        tcp_.setErrorCallback([this] (KMError err) { onError(err); });

        event_token_ = event_loop_->createToken();
    }

    ~TcpSocketJNI()
    {
        event_token_.reset();
    }

    void setSslFlags(uint32_t flags)
    {
        tcp_.setSslFlags(flags);
    }

    void setSslServerName(const std::string &serverName)
    {
        tcp_.setSslServerName(serverName.c_str());
    }

    KMError bind(const std::string &host, uint16_t port)
    {
        return tcp_.bind(host.c_str(), port);
    }

    KMError connect(const std::string &host, uint16_t port, uint32_t timeout_ms)
    {
        return tcp_.connect(host.c_str(), port, [this](KMError err) {
            onConnect(err);
        }, timeout_ms);
    }
    KMError startSslHandshake(SslRole ssl_role)
    {
        return tcp_.startSslHandshake(ssl_role, [this](KMError err) {
            onSslHandshake(err);
        });
    }

    int send(const KMBuffer &kmbuf)
    {
        if (send_blocked_) {
            return 0;
        }
        auto sz = kmbuf.size();
        bool should_notify = false;
        {
            std::lock_guard<std::mutex> g(mutex_);
            should_notify = send_list_.empty();
            send_list_.emplace(kmbuf);
        }
        if (should_notify) {
            event_loop_->async([this] {
                send_i();
            }, &event_token_);
        }
        return (int)sz;
    }

    void close()
    {
        event_loop_->async([this] {
            close_i();
        }, &event_token_);
    }

    void closeAndDestroy()
    {
        event_loop_->async([this] {
            close_i();
            delete this;
        }, &event_token_);
    }

private:
    void send_i()
    {
        if (send_list_i_.empty()) {
            std::lock_guard<std::mutex> g(mutex_);
            send_list_i_.swap(send_list_);
        }
        while (!send_list_i_.empty()) {
            auto &kmbuf = send_list_i_.front();
            auto ret = tcp_.send(kmbuf);
            if (ret > 0) {
                send_list_i_.pop();
            } else if (ret == 0) {
                send_blocked_ = true;
                break;
            } else {
                onError(KMError::FAILED);
                break;
            }
        }
    }
    void close_i()
    {
        tcp_.close();
        event_token_.reset();
    }

private:
    void onConnect(KMError err)
    {
        CALL_JAVA_METHOD_VOID(jcb_.obj(), onConnect, "(I)V", (int)err);
    }

    void onSslHandshake(KMError err)
    {
        CALL_JAVA_METHOD_VOID(jcb_.obj(), onSslHandshake, "(I)V", (int)err);
    }

    void onSend(KMError err)
    {
        send_i();
        if (send_list_i_.empty()) {
            send_blocked_ = false;
            CALL_JAVA_METHOD_VOID(jcb_.obj(), onSend, "(I)V", (int)err);
        }
    }

    void onReceive(KMError err)
    {
        auto *env = orc::android::jni::AttachCurrentThreadIfNeeded();
        char buf[16384] = {0};
        do {
            int bytes_read = tcp_.receive((uint8_t*)buf, sizeof(buf));
            if(bytes_read > 0) {
                auto jarr = as_jbyteArray(env, buf, bytes_read);
                CALL_JAVA_METHOD_VOID2(env, jcb_.obj(), onData, "([B)V", jarr.obj());
            } else if (0 == bytes_read) {
                break;
            } else {
                onError(KMError::SOCK_ERROR);
                break;
            }
        } while (true);
    }

    void onError(KMError err)
    {
        tcp_.close();
        CALL_JAVA_METHOD_VOID(jcb_.obj(), onError, "(I)V", (int)err);
    }

protected:
    kuma::EventLoop* event_loop_;
    kuma::EventLoop::Token event_token_;
    orc::android::jni::ScopedJavaGlobalRef<jobject> jcb_;
    TcpSocket tcp_;
    std::mutex mutex_;
    bool send_blocked_{false};
    std::queue<KMBuffer> send_list_;
    std::queue<KMBuffer> send_list_i_;
};

KUMA_JNI_NS_END

////////////////////////////////////////////////////////////////////////////////
// TcpSocket native api
extern "C" JNIEXPORT jlong JNICALL
Java_com_kuma_impl_TcpSocket_nativeCreate(
        JNIEnv *env,
        jobject jcaller) {
    auto *tcp = new TcpSocketJNI(env, jcaller, get_main_loop());
    return reinterpret_cast<jlong>(tcp);
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_TcpSocket_nativeSetSslFlags(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jint flags) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (!tcp) {
        return ;
    }
    tcp->setSslFlags(flags);
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_TcpSocket_nativeSetSslServerName(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jstr) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (!tcp) {
        return ;
    }
    auto server_name = as_std_string(env, jstr);
    tcp->setSslServerName(server_name);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kuma_impl_TcpSocket_nativeBind(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jhost,
        jint port) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (!tcp) {
        return false;
    }
    auto str_host = as_std_string(env, jhost);
    if (str_host.empty()) {
        return false;
    }
    return tcp->bind(str_host, port) != KMError::NOERR;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kuma_impl_TcpSocket_nativeConnect(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jhost,
        jint port,
        jint timeout_ms) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (!tcp) {
        return false;
    }
    auto str_host = as_std_string(env, jhost);
    if (str_host.empty()) {
        return false;
    }
    return tcp->connect(str_host, port, timeout_ms) != KMError::NOERR;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_kuma_impl_TcpSocket_nativeStartSslHandshake(JNIEnv *env,
        jobject thiz,
        jlong handle,jint role) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (!tcp) {
        return false;
    }
    return tcp->startSslHandshake((SslRole)role) != KMError::NOERR;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kuma_impl_TcpSocket_nativeSendString(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jstr) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (!tcp) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jstr);
    if (kmbuf.empty()) {
        return 0;
    }
    return tcp->send(kmbuf);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kuma_impl_TcpSocket_nativeSendArray(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jbyteArray jarr) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (!tcp) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jarr);
    if (kmbuf.empty()) {
        return 0;
    }
    return tcp->send(kmbuf);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kuma_impl_TcpSocket_nativeSendBuffer(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jobject jbuf) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (!tcp) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jbuf);
    if (kmbuf.empty()) {
        return 0;
    }
    return tcp->send(kmbuf);
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_TcpSocket_nativeClose(
        JNIEnv *env,
        jobject jcaller,
        jlong handle) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (tcp) {
        tcp->close();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_TcpSocket_nativeDestroy(
        JNIEnv *env,
        jobject jcaller,
        jlong handle) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    delete tcp;
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_TcpSocket_nativeCloseAndDestroy(
        JNIEnv *env,
        jobject jcaller,
        jlong handle) {
    auto *tcp = reinterpret_cast<TcpSocketJNI*>(handle);
    if (tcp) {
        tcp->closeAndDestroy();
    }
}