
#include <stdio.h>
#include <jni.h>

#include "kmapi.h"

using namespace kuma;

#include <android/log.h>
#define KUMA_TAG  "kuma-jni"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, KUMA_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, KUMA_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, KUMA_TAG, __VA_ARGS__)

EventLoop* get_main_loop();
JNIEnv* get_jni_env(void);
void set_handle(JNIEnv *env, jobject obj, void *t);
jbyteArray as_byte_array(JNIEnv* env, uint8_t* buf, int len);
uint8_t* as_uint8_array(JNIEnv* env, jbyteArray array);
