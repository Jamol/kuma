
#include <stdio.h>
#include <thread>
#include <jni.h>

#include "kmapi-jni.h"

using namespace kuma;

void jni_init();
void jni_fini();

EventLoop main_loop;
std::thread net_thread;
bool stop_loop = false;

static void trace_func(int level, const char* log)
{
    LOGI(log);
}

JavaVM* java_vm = nullptr;
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved)
{
    java_vm = jvm;
    JNIEnv *env = nullptr;
    int ret = jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_4);
    if (ret != JNI_OK || env == nullptr) {
        return -1;
    }
    setTraceFunc(trace_func);
    jni_init();
    return JNI_VERSION_1_4;
}

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *jvm, void *reserved)
{
    jni_fini();
}

JNIEnv* get_jni_env()
{
    if (nullptr == java_vm) {
        return nullptr;
    }
    
    JNIEnv *env = NULL;
    // get jni environment
    jint ret = java_vm->GetEnv((void**)&env, JNI_VERSION_1_4);
    
    switch (ret) {
    case JNI_OK:
        // Success!
        return env;

    case JNI_EDETACHED:
        // Thread not attached

        // TODO : If calling AttachCurrentThread() on a native thread
        // must call DetachCurrentThread() in future.
        // see: http://developer.android.com/guide/practices/design/jni.html

        if (java_vm->AttachCurrentThread(&env, NULL) < 0) {
            return nullptr;
        } else {
            // Success : Attached and obtained JNIEnv!
            return env;
        }

    case JNI_EVERSION:
        // Cannot recover from this error
    default:
        return nullptr;
    }
}

JNIEnv* get_current_jni_env()
{
    if (nullptr == java_vm) {
        return nullptr;
    }

    JNIEnv *env = nullptr;
    if (java_vm->GetEnv((void**)&env, JNI_VERSION_1_4) == JNI_OK) {
        return env;
    }
    return nullptr;
}

EventLoop* get_main_loop()
{
    return &main_loop;
}

jfieldID get_handle_field(JNIEnv *env, jobject obj)
{
    jclass c = env->GetObjectClass(obj);
    // J is the type signature for long:
    return env->GetFieldID(c, "nativeHandle", "J");
}

void set_handle(JNIEnv *env, jobject obj, void *t)
{
    jlong handle = reinterpret_cast<jlong>(t);
    env->SetLongField(obj, get_handle_field(env, obj), handle);
}

jbyteArray as_byte_array(JNIEnv* env, uint8_t* buf, int len) {
    jbyteArray jbuf = env->NewByteArray(len);
    env->SetByteArrayRegion(jbuf, 0, len, reinterpret_cast<jbyte*>(buf));
    return jbuf;
}

uint8_t* as_uint8_array(JNIEnv* env, jbyteArray array) {
    int len = env->GetArrayLength(array);
    uint8_t* buf = new uint8_t[len];
    env->GetByteArrayRegion(array, 0, len, reinterpret_cast<jbyte*>(buf));
    return buf;
}

void jni_init()
{
    try {
        net_thread = std::thread([] {
            if (!main_loop.init()) {
                return;
            }

            JNIEnv *env;
            bool attached = false;
            int status = java_vm->GetEnv((void **)&env, JNI_VERSION_1_4);
            if (status < 0) {
                LOGE("callback_handler: failed to get JNI environment, assuming native thread");
                status = java_vm->AttachCurrentThread(&env, NULL);
                if (status < 0) {
                    LOGE("callback_handler: failed to attach current thread");
                }
                attached = true;
            }

            main_loop.loop();

            if (attached) {
                java_vm->DetachCurrentThread();
            }
        });
    }
    catch (...) {

    }
}

void jni_fini()
{
    stop_loop = true;
    main_loop.stop();
    try {
        if (net_thread.joinable()) {
            net_thread.join();
        }
    }
    catch (...) {

    }
}
