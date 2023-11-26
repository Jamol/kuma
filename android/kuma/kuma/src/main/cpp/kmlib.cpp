#include <jni.h>
#include <string>
#include <thread>

#include "kmapi.h"
#include "utils/km-jni.h"
#include "libkev/src/utils/kmtrace.h"

void jni_init();
void jni_fini();

kuma::EventLoop main_loop;
std::thread net_thread;
bool stop_loop = false;

static void trace_func(int level, std::string &&msg)
{
    switch (level)
    {
        case kev::TRACE_LEVEL_ERROR:
            KLOGX(ANDROID_LOG_ERROR, "[kuma]", msg);
            break;
        case kev::TRACE_LEVEL_WARN:
            KLOGX(ANDROID_LOG_WARN, "[kuma]", msg);
            break;
        case kev::TRACE_LEVEL_DEBUG:
            KLOGX(ANDROID_LOG_DEBUG, "[kuma]", msg);
            break;
        case kev::TRACE_LEVEL_VERBOS:
            KLOGX(ANDROID_LOG_VERBOSE, "[kuma]", msg);
            break;
        default:
            KLOGX(ANDROID_LOG_INFO, "[kuma]", msg);
            break;
    }
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved)
{
    auto ret = orc::android::jni::InitGlobalJniVariables(jvm);
    if (ret == -1) {
        return -1;
    }
    kev::setTraceFunc(trace_func);
    jni_init();
    return ret;
}

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *jvm, void *reserved)
{
    jni_fini();
}

kuma::EventLoop* get_main_loop()
{
    return &main_loop;
}

void jni_init()
{
    try {
        net_thread = std::thread([] {
            KLOGI("main thread start");
            if (!main_loop.init()) {
                return;
            }
            main_loop.loop();
            KLOGI("main thread exit...");
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
