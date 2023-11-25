#include <jni.h>
#include <string>
#include <thread>

#include "kmapi.h"
#include "utils/km-jni.h"

KUMA_NS_USING

void jni_init();
void jni_fini();

EventLoop main_loop;
std::thread net_thread;
bool stop_loop = false;

static void trace_func(int level, const char* log)
{
    //LOGI(log);
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
    //setTraceFunc(trace_func);
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

    JNIEnv *env = nullptr;
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
                KM_ERRTRACE("callback_handler: failed to get JNI environment, assuming native thread");
                status = java_vm->AttachCurrentThread(&env, NULL);
                if (status < 0) {
                    KM_ERRTRACE("callback_handler: failed to attach current thread");
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
