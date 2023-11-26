#include <jni.h>

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

class HttpRequestJNI final
{
public:
    HttpRequestJNI(JNIEnv *env, jobject jcb, const std::string &ver, kuma::EventLoop *loop)
            : event_loop_(loop)
            , http_(event_loop_, ver.c_str())
            , jcb_(env, jcb)
    {
        http_.setHeaderCompleteCallback([this] { onHeaderComplete(); });
        http_.setWriteCallback([this] (KMError err) { onSend(err); });
        http_.setDataCallback([this] (KMBuffer &buf) { onData(buf); });
        http_.setResponseCompleteCallback([this] { onResponseComplete(); });
        http_.setErrorCallback([this] (KMError err) { onClose(err); });

        event_token_ = event_loop_->createToken();
    }

    ~HttpRequestJNI()
    {
        event_token_.reset();
    }

    void setSslFlags(uint32_t flags)
    {
        http_.setSslFlags(flags);
    }

    void addHeader(const std::string &key, const std::string &value)
    {
        http_.addHeader(key.c_str(), value.c_str());
    }

    KMError sendRequest(const std::string &method, const std::string &url)
    {
        return http_.sendRequest(method.c_str(), url.c_str());
    }

    int sendData(const KMBuffer &kmbuf)
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

    void reset()
    {
        event_loop_->async([this] {
            http_.reset();
        }, &event_token_);
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
            auto ret = http_.sendData(kmbuf);
            if (ret > 0) {
                send_list_i_.pop();
            } else if (ret == 0) {
                send_blocked_ = true;
                break;
            } else {
                onClose(KMError::FAILED);
                break;
            }
        }
    }
    void close_i()
    {
        http_.close();
        event_token_.reset();
    }

private:
    void onHeaderComplete()
    {
        CALL_JAVA_METHOD_VOID(jcb_.obj(), onHeaderComplete, "()V");
    }

    void onSend(KMError err)
    {
        send_i();
        if (send_list_i_.empty()) {
            send_blocked_ = false;
            CALL_JAVA_METHOD_VOID(jcb_.obj(), onSend, "(I)V", (int)err);
        }
    }

    void onData(KMBuffer &kmbuf)
    {
        auto *env = orc::android::jni::AttachCurrentThreadIfNeeded();
        auto jarr = as_jbyteArray(env, kmbuf);
        CALL_JAVA_METHOD_VOID2(env, jcb_.obj(), onData, "([B)V",
                               jarr.obj());
    }

    void onResponseComplete()
    {
        CALL_JAVA_METHOD_VOID(jcb_.obj(), onResponseComplete, "()V");
    }

    void onClose(KMError err)
    {
        http_.close();

        CALL_JAVA_METHOD_VOID(jcb_.obj(), onClose, "(I)V", (int)err);
    }

protected:
    kuma::EventLoop* event_loop_;
    kuma::EventLoop::Token event_token_;
    orc::android::jni::ScopedJavaGlobalRef<jobject> jcb_;
    HttpRequest http_;
    std::mutex mutex_;
    bool send_blocked_{false};
    std::queue<KMBuffer> send_list_;
    std::queue<KMBuffer> send_list_i_;
};

KUMA_JNI_NS_END


extern "C"
JNIEXPORT jlong JNICALL
Java_com_kuma_impl_HttpRequest_nativeCreate(JNIEnv *env, jobject thiz, jstring jver) {
    auto str_ver = as_std_string(env, jver);
    auto *ws = new HttpRequestJNI(env, thiz, str_ver, get_main_loop());
    return reinterpret_cast<jlong>(ws);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_kuma_impl_HttpRequest_nativeSetSslFlags(JNIEnv *env, jobject thiz, jlong handle,
                                                 jint flags) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    if (!http) {
        return ;
    }
    http->setSslFlags(flags);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_kuma_impl_HttpRequest_nativeAddHeader(JNIEnv *env, jobject thiz, jlong handle, jstring jkey,
                                               jstring jval) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    if (!http) {
        return ;
    }
    auto str_key = as_std_string(env, jkey);
    if (str_key.empty()) {
        return ;
    }
    auto str_val = as_std_string(env, jval);
    http->addHeader(str_key, str_val);
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_kuma_impl_HttpRequest_nativeSendRequest(JNIEnv *env, jobject thiz, jlong handle,
                                                 jstring jmethod, jstring jurl) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    if (!http) {
        return (int)KMError::INVALID_STATE;
    }
    auto str_method = as_std_string(env, jmethod);
    if (str_method.empty()) {
        return (int)KMError::INVALID_PARAM;
    }
    auto str_url = as_std_string(env, jurl);
    return (int)http->sendRequest(str_method, str_url);
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_kuma_impl_HttpRequest_nativeSendString(JNIEnv *env, jobject thiz, jlong handle,
                                                jstring jstr) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    if (!http) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jstr);
    if (kmbuf.empty()) {
        return 0;
    }
    return http->sendData(kmbuf);
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_kuma_impl_HttpRequest_nativeSendArray(JNIEnv *env, jobject thiz, jlong handle,
                                               jbyteArray jarr) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    if (!http) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jarr);
    if (kmbuf.empty()) {
        return 0;
    }
    return http->sendData(kmbuf);
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_kuma_impl_HttpRequest_nativeSendBuffer(JNIEnv *env, jobject thiz, jlong handle,
                                                jobject jbuf) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    if (!http) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jbuf);
    if (kmbuf.empty()) {
        return 0;
    }
    return http->sendData(kmbuf);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_kuma_impl_HttpRequest_nativeReset(JNIEnv *env, jobject thiz, jlong handle) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    if (http) {
        http->reset();
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_kuma_impl_HttpRequest_nativeClose(JNIEnv *env, jobject thiz, jlong handle) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    if (http) {
        http->close();
    }
}
extern "C"
JNIEXPORT void JNICALL
Java_com_kuma_impl_HttpRequest_nativeDestroy(JNIEnv *env, jobject thiz, jlong handle) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    delete http;
}
extern "C"
JNIEXPORT void JNICALL
Java_com_kuma_impl_HttpRequest_nativeCloseAndDestroy(JNIEnv *env, jobject thiz, jlong handle) {
    auto *http = reinterpret_cast<HttpRequestJNI*>(handle);
    if (http) {
        http->closeAndDestroy();
    }
}