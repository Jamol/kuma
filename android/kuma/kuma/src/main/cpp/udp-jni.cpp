#include "utils/km-jni.h"
#include "utils/kmutils-jni.h"
#include "utils/scoped_java_ref.h"
#include "kmapi.h"

KUMA_NS_USING
KUMA_JNI_NS_USING

kuma::EventLoop* get_main_loop();

KUMA_JNI_NS_BEGIN

class UdpSocketJNI final
{
public:
    UdpSocketJNI(JNIEnv *env, jobject jcb, kuma::EventLoop *loop)
            : event_loop_(loop)
            , udp_(event_loop_)
            , jcb_(env, jcb)
    {
        udp_.setReadCallback([this] (KMError err) { onReceive(err); });
        udp_.setErrorCallback([this] (KMError err) { onError(err); });

        event_token_ = event_loop_->createToken();
    }

    ~UdpSocketJNI()
    {
        event_token_.reset();
    }

    KMError bind(const std::string &host, uint16_t port, uint32_t flags)
    {
        return udp_.bind(host.c_str(), port, flags);
    }

    KMError connect(const std::string &host, uint16_t port)
    {
        return udp_.connect(host.c_str(), port);
    }

    int send(const KMBuffer &kmbuf, std::string host, uint16_t port)
    {
        auto sz = kmbuf.chainLength();
        event_loop_->async([=,host{std::move(host)}] {
            udp_.send(kmbuf, host.c_str(), port);
        }, &event_token_);
        return (int)sz;
    }

    KMError mcastJoin(const std::string &mcast_addr, uint16_t mcast_port)
    {
        return udp_.mcastJoin(mcast_addr.c_str(), mcast_port);
    }

    KMError mcastLeave(const std::string &mcast_addr, uint16_t mcast_port)
    {
        return udp_.mcastLeave(mcast_addr.c_str(), mcast_port);
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
    void close_i()
    {
        udp_.close();
        event_token_.reset();
    }

private:
    void onReceive(KMError err)
    {
        auto *env = orc::android::jni::AttachCurrentThreadIfNeeded();
        char buf[16384] = {0};
        char ip[128] = {0};
        uint16_t port = 0;
        do {
            int bytes_read = udp_.receive((uint8_t*)buf, sizeof(buf), ip, sizeof(ip), port);
            if(bytes_read > 0) {
                auto jarr = as_jbyteArray(env, buf, bytes_read);
                auto jstr = as_jstring_utf16(env, ip);
                CALL_JAVA_METHOD_VOID2(env, jcb_.obj(), onData, "([BLjava/lang/String;I)V",
                                       jarr.obj(), jstr.obj(), port);
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
        udp_.close();

        CALL_JAVA_METHOD_VOID(jcb_.obj(), onError, "(I)V", (int)err);
    }

protected:
    kuma::EventLoop* event_loop_;
    kuma::EventLoop::Token event_token_;
    orc::android::jni::ScopedJavaGlobalRef<jobject> jcb_;
    UdpSocket udp_;
};

KUMA_JNI_NS_END

////////////////////////////////////////////////////////////////////////////////
// UdpSocket native api
extern "C" JNIEXPORT jlong JNICALL
Java_com_kuma_impl_UdpSocket_nativeCreate(
        JNIEnv *env,
        jobject jcaller) {
    auto *udp = new UdpSocketJNI(env, jcaller, get_main_loop());
    return reinterpret_cast<jlong>(udp);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kuma_impl_UdpSocket_nativeBind(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jhost,
        jint port,
        jint flags) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    if (!udp) {
        return false;
    }
    auto str_host = as_std_string(env, jhost);
    if (str_host.empty()) {
        return false;
    }
    return udp->bind(str_host, port, flags) != KMError::NOERR;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kuma_impl_UdpSocket_nativeConnect(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jhost,
        jint port) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    if (!udp) {
        return false;
    }
    auto str_host = as_std_string(env, jhost);
    if (str_host.empty()) {
        return false;
    }
    return udp->connect(str_host, port) != KMError::NOERR;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kuma_impl_UdpSocket_nativeSendString(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jstr,
        jstring jhost,
        jint port) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    if (!udp) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jstr);
    if (kmbuf.empty()) {
        return 0;
    }
    auto str_host = as_std_string(env, jhost);
    return udp->send(kmbuf, std::move(str_host), port);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kuma_impl_UdpSocket_nativeSendArray(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jbyteArray jarr,
        jstring jhost,
        jint port) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    if (!udp) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jarr);
    if (kmbuf.empty()) {
        return 0;
    }
    auto str_host = as_std_string(env, jhost);
    return udp->send(kmbuf, std::move(str_host), port);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kuma_impl_UdpSocket_nativeSendBuffer(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jobject jbuf,
        jstring jhost,
        jint port) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    if (!udp) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jbuf);
    if (kmbuf.empty()) {
        return 0;
    }
    auto str_host = as_std_string(env, jhost);
    return udp->send(kmbuf, std::move(str_host), port);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_kuma_impl_UdpSocket_nativeMcastJoin(JNIEnv *env,
         jobject thiz,
         jlong handle,
         jstring jaddr,
         jint port) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    if (!udp) {
        return false;
    }
    auto str_addr = as_std_string(env, jaddr);
    return udp->mcastJoin(str_addr, port) != KMError::NOERR;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_kuma_impl_UdpSocket_nativeMcastLeave(JNIEnv *env,
         jobject thiz,
         jlong handle,
         jstring jaddr,
         jint port) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    if (!udp) {
        return false;
    }
    auto str_addr = as_std_string(env, jaddr);
    return udp->mcastLeave(str_addr, port) != KMError::NOERR;
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_UdpSocket_nativeClose(
        JNIEnv *env,
        jobject jcaller,
        jlong handle) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    if (udp) {
        udp->close();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_UdpSocket_nativeDestroy(
        JNIEnv *env,
        jobject jcaller,
        jlong handle) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    delete udp;
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_UdpSocket_nativeCloseAndDestroy(
        JNIEnv *env,
        jobject jcaller,
        jlong handle) {
    auto *udp = reinterpret_cast<UdpSocketJNI*>(handle);
    if (udp) {
        udp->closeAndDestroy();
    }
}