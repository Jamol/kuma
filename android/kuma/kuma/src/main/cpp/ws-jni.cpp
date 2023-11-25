
#include "utils/km-jni.h"
#include "utils/kmutils-jni.h"
#include "utils/scoped_java_ref.h"
#include "kmapi.h"

KUMA_NS_USING
KUMA_JNI_NS_USING

EventLoop* get_main_loop();

KUMA_JNI_NS_BEGIN

class WebSocketJNI
{
public:
    WebSocketJNI(JNIEnv *env, jobject jcb)
    : ws_(get_main_loop())
    , jcb_(env, jcb)
    {
        ws_.setOpenCallback([this] (KMError err) { onOpen(err); });
        ws_.setDataCallback([this] (KMBuffer &buf, bool is_text, bool fin) {
            onData(buf, is_text, fin);
        });
        ws_.setWriteCallback([this] (KMError err) { onSend(err); });
        ws_.setErrorCallback([this] (KMError err) { onClose(err); });
    }

    KMError connect(const std::string &ws_url)
    {
        return ws_.connect(ws_url.c_str());
    }

    int send(const void *data, size_t len, bool is_text)
    {
        return ws_.send(data, len, is_text);
    }

    int send(const KMBuffer &kmbuf, bool is_text)
    {
        return ws_.send(kmbuf, is_text);
    }

    KMError close()
    {
        return ws_.close();
    }

    void onOpen(KMError err)
    {
        CALL_JAVA_METHOD_VOID(jcb_.obj(), onOpen, "(I)V", (int)err);
    }

    void onSend(KMError err)
    {
        CALL_JAVA_METHOD_VOID(jcb_.obj(), onSend, "(I)V", (int)err);
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
    orc::android::jni::ScopedJavaGlobalRef<jobject> jcb_;
    WebSocket ws_;
};

KUMA_JNI_NS_END

////////////////////////////////////////////////////////////////////////////////
// WebSocket native api
extern "C" JNIEXPORT jlong JNICALL
Java_com_kuma_impl_WebSocket_nativeCreate(
        JNIEnv *env,
        jobject jcaller) {
    auto *ws = new WebSocketJNI(env, jcaller);
    return reinterpret_cast<jlong>(ws);
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
    delete ws;
}