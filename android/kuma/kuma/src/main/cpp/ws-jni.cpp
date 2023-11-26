
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

class WebSocketJNI final
{
public:
    WebSocketJNI(JNIEnv *env, jobject jcb, const std::string &ver, kuma::EventLoop *loop)
    : event_loop_(loop)
    , ws_(event_loop_, ver.c_str())
    , jcb_(env, jcb)
    {
        ws_.setOpenCallback([this] (KMError err) { onOpen(err); });
        ws_.setDataCallback([this] (KMBuffer &buf, bool is_text, bool fin) {
            onData(buf, is_text, fin);
        });
        ws_.setWriteCallback([this] (KMError err) { onSend(err); });
        ws_.setErrorCallback([this] (KMError err) { onClose(err); });

        event_token_ = event_loop_->createToken();
    }

    ~WebSocketJNI()
    {
        event_token_.reset();
    }

    void setSslFlags(uint32_t flags)
    {
        ws_.setSslFlags(flags);
    }

    void setOrigin(const std::string &origin)
    {
        ws_.setOrigin(origin.c_str());
    }

    void addHeader(const std::string &key, const std::string &value)
    {
        ws_.addHeader(key.c_str(), value.c_str());
    }

    KMError connect(const std::string &ws_url)
    {
        return ws_.connect(ws_url.c_str());
    }

    int send(const KMBuffer &kmbuf, bool is_text)
    {
        if (send_blocked_) {
            return 0;
        }
        auto sz = kmbuf.size();
        bool should_notify = false;
        {
            std::lock_guard<std::mutex> g(mutex_);
            should_notify = send_list_.empty();
            send_list_.emplace(kmbuf, is_text);
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
            auto &pr = send_list_i_.front();
            auto ret = ws_.send(pr.first, pr.second);
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
        ws_.close();
        event_token_.reset();
    }

private:
    void onOpen(KMError err)
    {
        CALL_JAVA_METHOD_VOID(jcb_.obj(), onOpen, "(I)V", (int)err);
    }

    void onSend(KMError err)
    {
        send_i();
        if (send_list_i_.empty()) {
            send_blocked_ = false;
            CALL_JAVA_METHOD_VOID(jcb_.obj(), onSend, "(I)V", (int)err);
        }
    }

    void onData(KMBuffer &kmbuf, bool is_text, bool fin)
    {
        auto *env = orc::android::jni::AttachCurrentThreadIfNeeded();
        if (is_text) {
            auto jstr = as_jstring_utf16(env, kmbuf);
            CALL_JAVA_METHOD_VOID2(env, jcb_.obj(), onData, "(Ljava/lang/String;Z)V",
                                   jstr.obj(), fin);
        } else {
            auto jarr = as_jbyteArray(env, kmbuf);
            CALL_JAVA_METHOD_VOID2(env, jcb_.obj(), onData, "([BZ)V",
                                   jarr.obj(), fin);
        }
    }

    void onClose(KMError err)
    {
        ws_.close();

        CALL_JAVA_METHOD_VOID(jcb_.obj(), onClose, "(I)V", (int)err);
    }

protected:
    kuma::EventLoop* event_loop_;
    kuma::EventLoop::Token event_token_;
    orc::android::jni::ScopedJavaGlobalRef<jobject> jcb_;
    WebSocket ws_;
    std::mutex mutex_;
    bool send_blocked_{false};
    std::queue<std::pair<KMBuffer, bool>> send_list_;
    std::queue<std::pair<KMBuffer, bool>> send_list_i_;
};

KUMA_JNI_NS_END

////////////////////////////////////////////////////////////////////////////////
// WebSocket native api
extern "C" JNIEXPORT jlong JNICALL
Java_com_kuma_impl_WebSocket_nativeCreate(
        JNIEnv *env,
        jobject jcaller,
        jstring jver) {
    auto str_ver = as_std_string(env, jver);
    auto *ws = new WebSocketJNI(env, jcaller, str_ver, get_main_loop());
    return reinterpret_cast<jlong>(ws);
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_WebSocket_nativeSetSslFlags(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jint flags) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    if (!ws) {
        return ;
    }
    ws->setSslFlags(flags);
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_WebSocket_nativeSetOrigin(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jstr) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    if (!ws) {
        return ;
    }
    auto str_origin = as_std_string(env, jstr);
    ws->setOrigin(str_origin);
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_WebSocket_nativeAddHeader(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jkey,
        jstring jval) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    if (!ws) {
        return ;
    }
    auto str_key = as_std_string(env, jkey);
    if (str_key.empty()) {
        return ;
    }
    auto str_val = as_std_string(env, jval);
    ws->addHeader(str_key, str_val);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_kuma_impl_WebSocket_nativeOpen(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jurl) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    if (!ws) {
        return false;
    }
    auto str_url = as_std_string(env, jurl);
    if (str_url.empty()) {
        return false;
    }
    return ws->connect(str_url) != KMError::NOERR;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kuma_impl_WebSocket_nativeSendString(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jstring jstr) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    if (!ws) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jstr);
    if (kmbuf.empty()) {
        return 0;
    }
    return ws->send(kmbuf, true);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kuma_impl_WebSocket_nativeSendArray(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jbyteArray jarr) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    if (!ws) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jarr);
    if (kmbuf.empty()) {
        return 0;
    }
    return ws->send(kmbuf, false);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kuma_impl_WebSocket_nativeSendBuffer(
        JNIEnv *env,
        jobject jcaller,
        jlong handle,
        jobject jbuf) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    if (!ws) {
        return 0;
    }
    auto kmbuf = as_kmbuffer(env, jbuf);
    if (kmbuf.empty()) {
        return 0;
    }
    return ws->send(kmbuf, false);
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_WebSocket_nativeClose(
        JNIEnv *env,
        jobject jcaller,
        jlong handle) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    if (ws) {
        ws->close();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_WebSocket_nativeDestroy(
        JNIEnv *env,
        jobject jcaller,
        jlong handle) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    delete ws;
}

extern "C" JNIEXPORT void JNICALL
Java_com_kuma_impl_WebSocket_nativeCloseAndDestroy(
        JNIEnv *env,
        jobject jcaller,
        jlong handle) {
    auto *ws = reinterpret_cast<WebSocketJNI*>(handle);
    if (ws) {
        ws->closeAndDestroy();
    }
}