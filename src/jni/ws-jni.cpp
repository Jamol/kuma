
#include <stdio.h>
#include <memory>
#include <string>
#include <jni.h>

#include "kmapi-jni.h"

using namespace kuma;

typedef struct {
    bool initialized = false;
    jclass clazz;
    jmethodID mid_on_connect;
    jmethodID mid_on_data_string;
    jmethodID mid_on_data_array;
    jmethodID mid_on_close;
} ws_callback_t;

ws_callback_t ws_callback;

class WebSocketJNI {
public:

    WebSocketJNI(EventLoop* loop, jobject obj)
        : loop_(loop), ws_(nullptr), obj_(obj) {

    }
    
    ~WebSocketJNI() {
        if (ws_) {
            ws_->close();
            delete ws_;
            ws_ = nullptr;
        }
        if(obj_) {
            JNIEnv* env = get_current_jni_env();
            if(env) {
                env->DeleteGlobalRef(obj_);
            }
        }
    }

    int connect(const char* ws_url) {
        ws_ = new WebSocket(loop_);
        ws_->setDataCallback([this](uint8_t* data, uint32_t len) { on_data(data, len); });
        ws_->setErrorCallback([this](int err) { on_error(err); });
        int ret = ws_->connect(ws_url, [this](int err) { on_connect(err); });
        return ret;
    }

    int send(std::string str) {
        if (ws_) {
            return ws_->send((uint8_t*)str.c_str(), str.length());
        }
        return -1;
    }

    int send(const uint8_t* data, uint32_t len) {
        if (ws_) {
            return ws_->send(data, len);
        }
        return -1;
    }

    int close() {
        if (ws_) {
            ws_->close();
            delete ws_;
            ws_ = nullptr;
        }
        return 0;
    }

    void on_connect(int err) {
        JNIEnv* env = get_current_jni_env();
        if (env) {
            env->CallVoidMethod(obj_, ws_callback.mid_on_connect, err);
        }
    }

    void on_data(uint8_t* data, uint32_t len) {
        JNIEnv* env = get_current_jni_env();
        if (env) {
            jbyteArray jdata = as_byte_array(env, data, len);
            env->CallVoidMethod(obj_, ws_callback.mid_on_data_array, jdata);
        }
    }

    void on_error(int err) {
        JNIEnv* env = get_current_jni_env();
        if (env) {
            env->CallVoidMethod(obj_, ws_callback.mid_on_close, err);
        }
    }

private:
    EventLoop* loop_;
    WebSocket* ws_;
    jobject obj_;
};

extern "C" JNIEXPORT jint JNICALL Java_com_jamol_kuma_WebSocket_connect(JNIEnv *env, jobject thiz, jstring ws_url)
{
    jobject obj = (jobject)env->NewGlobalRef(thiz);
    if (!ws_callback.initialized) {
        jclass jc = env->GetObjectClass(thiz);
        if (!jc) {
            LOGE("WebSocket.connect, cannot find java class");
            return -1;
        }
        jclass clazz = (jclass)env->NewGlobalRef(jc);
        if (!clazz) {
            LOGE("WebSocket.connect, cannot get global ref to class");
            return -1;
        }
        ws_callback.clazz = clazz;
        ws_callback.mid_on_connect = env->GetMethodID(clazz, "onConnect", "(I)V");
        if (!ws_callback.mid_on_connect) {
            LOGE("WebSocket.connect, cannot find method id for onConnect");
            return -1;
        }
        ws_callback.mid_on_data_string = env->GetMethodID(clazz, "onData", "(Ljava/lang/String;)V");
        if (!ws_callback.mid_on_data_string) {
            LOGE("WebSocket.connect, cannot find method id for onDataString");
            return -1;
        }
        ws_callback.mid_on_data_array = env->GetMethodID(clazz, "onData", "([B)V");
        if (!ws_callback.mid_on_data_array) {
            LOGE("WebSocket.connect, cannot find method id for onDataArray");
            return -1;
        }
        ws_callback.mid_on_close = env->GetMethodID(clazz, "onClose", "()V");
        if (!ws_callback.mid_on_close) {
            LOGE("WebSocket.connect, cannot find method id for onClose");
            return -1;
        }
        ws_callback.initialized = true;
    }
    WebSocketJNI* ws = new WebSocketJNI(get_main_loop(), obj);
    set_handle(env, obj, ws);
    const char *str_url = env->GetStringUTFChars(ws_url, 0);
    int ret = ws->connect(str_url);
    env->ReleaseStringUTFChars(ws_url, str_url);
    return ret;
}

extern "C" JNIEXPORT jint JNICALL Java_com_jamol_kuma_WebSocket_sendString(JNIEnv *env, jobject obj, jlong handle, jstring jstr)
{
    if (0 == handle) {
        return 0;
    }
    WebSocketJNI* ws = reinterpret_cast<WebSocketJNI*>(handle);
    const char *str = env->GetStringUTFChars(jstr, 0);
    int ret = ws->send(str);
    env->ReleaseStringUTFChars(jstr, str);
    return ret;
}

extern "C" JNIEXPORT jint JNICALL Java_com_jamol_kuma_WebSocket_sendArray(JNIEnv *env, jobject obj, jlong handle, jbyteArray jdata)
{
    if (0 == handle) {
        return 0;
    }
    WebSocketJNI* ws = reinterpret_cast<WebSocketJNI*>(handle);
    uint32_t len = env->GetArrayLength(jdata);
    jbyte* data = env->GetByteArrayElements(jdata, NULL);
    int ret = ws->send((const uint8_t*)data, len);
    env->ReleaseByteArrayElements(jdata, data, JNI_ABORT);
    return ret;
}

extern "C" JNIEXPORT jint JNICALL Java_com_jamol_kuma_WebSocket_Close(JNIEnv *env, jobject obj, jlong handle)
{
    if (0 == handle) {
        return 0;
    }
    WebSocketJNI* ws = reinterpret_cast<WebSocketJNI*>(handle);
    int ret = ws->close();
    delete ws;
    return ret;
}

void ws_fini()
{
    if (ws_callback.initialized){
        JNIEnv* env = get_current_jni_env();
        if (env) {
            env->DeleteGlobalRef(ws_callback.clazz);
            ws_callback.initialized = false;
        }
    }
}
