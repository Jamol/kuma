#pragma once

#include <sstream>
#include <jni.h>
#include "jvm.h"

#define KUMA_JNI_NS_BEGIN namespace kuma { namespace jni {
#define KUMA_JNI_NS_END }}
#define KUMA_JNI_NS_USING using namespace kuma::jni;

#include <android/log.h>
#define KUMA_TAG  "[kuma-jni]"

#define KLOGX(l, t, x) \
    do { \
        std::ostringstream __ss_not_easy_conflict_42__; \
        __ss_not_easy_conflict_42__ << x; \
        __android_log_print(l, t, "%s", __ss_not_easy_conflict_42__.str().c_str()); \
    } while(0)

#define KLOGI(s) KLOGX(ANDROID_LOG_INFO, KUMA_TAG, s)
#define KLOGW(s) KLOGX(ANDROID_LOG_WARN, KUMA_TAG, s)
#define KLOGE(s) KLOGX(ANDROID_LOG_ERROR, KUMA_TAG, s)
#define KLOGV(s) KLOGX(ANDROID_LOG_VERBOSE, KUMA_TAG, s)
#define KLOGD(s) KLOGX(ANDROID_LOG_DEBUG, KUMA_TAG, s)


#define JNI_CHECK_ENV(env, msg) \
    if (env->ExceptionCheck()) { \
        KLOGE("[jni] Check failed: " << msg);\
        env->ExceptionDescribe(); \
        env->ExceptionClear(); \
    }

#define JNI_CHECK_ENV_RETURN(env, ret, msg) \
    if (env->ExceptionCheck()) { \
        KLOGE("[jni] Check failed: " << msg);\
        env->ExceptionDescribe(); \
        env->ExceptionClear(); \
        return ret; \
    }

// env, jobject, method name, method signature
#define IMPLEMENT_JAVA_OBJECT_METHOD_ID(v, o, m, s) \
    static jmethodID jm_##m = nullptr; \
    if (!jm_##m) { \
        auto clazz = v->GetObjectClass(o); \
        jm_##m = v->GetMethodID(clazz, #m, s); \
        v->DeleteLocalRef(clazz);\
        JNI_CHECK_ENV(v, "GetMethodID " #m " " s)\
    }

// env, jobject, err, method name, method signature
#define IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(v, o, e, m, s) \
    static jmethodID jm_##m = nullptr; \
    if (!jm_##m) { \
        auto clazz = v->GetObjectClass(o); \
        jm_##m = v->GetMethodID(clazz, #m, s); \
        v->DeleteLocalRef(clazz);\
        JNI_CHECK_ENV_RETURN(v, e, "GetMethodID " #m " " s)\
    }

#define IMPLEMENT_JAVA_CLASS_FIELD_ID(v, c, f, s) \
    static jfieldID jf_##f = nullptr; \
    if (!jf_##f) { \
        jf_##f = v->GetFieldID(c, #f, s); \
        JNI_CHECK_ENV(v, "GetFieldID " #f " " s)\
    }

#define IMPLEMENT_JAVA_CLASS_FIELD_ID_R(v, c, f, s, e) \
    static jfieldID jf_##f = nullptr; \
    if (!jf_##f) { \
        jf_##f = v->GetFieldID(c, #f, s); \
        JNI_CHECK_ENV_RETURN(v, e, "GetFieldID " #f " " s)\
    }

#define GET_JAVA_OBJECT_FIELD_VALUE_INT_R(v, o, f, r, e) \
    do { \
        static jfieldID jf_##f = nullptr; \
        if (!jf_##f) { \
            auto clazz = v->GetObjectClass(o); \
            jf_##f = v->GetFieldID(clazz, #f, "I"); \
            JNI_CHECK_ENV_RETURN(v, e, "GetFieldID " #f " I")\
        }\
        jint ret_cc = v->GetIntField(o, jf_##f);\
        JNI_CHECK_ENV_RETURN(v, e, "GetIntField " #f)\
        r = (int)ret_cc; \
    } while(0)

#define DECLARE_JAVA_METHOD_STRING(m) \
static bool m(JNIEnv *env, jobject jobj, std::string &ret) \
{ \
    IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(env, jobj, false, m, "()Ljava/lang/String;");\
    auto jstr = static_cast<jstring>(env->CallObjectMethod(jobj, jm_##m));\
    JNI_CHECK_ENV_RETURN(env, false, "String CallObjectMethod " #m); \
    ret = as_std_string(env, jstr); \
    env->DeleteLocalRef(jstr);\
    return true; \
}

#define DECLARE_JAVA_METHOD_INT(m) \
static bool m(JNIEnv *env, jobject jobj, int &ret) \
{ \
    IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(env, jobj, false, m, "()I");\
    ret = env->CallIntMethod(jobj, jm_##m);\
    JNI_CHECK_ENV_RETURN(env, false, "CallIntMethod " #m); \
    return true; \
}

#define DECLARE_JAVA_METHOD_LONG(m) \
static bool m(JNIEnv *env, jobject jobj, jlong &ret) \
{ \
    IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(env, jobj, false, m, "()J");\
    ret = env->CallLongMethod(jobj, jm_##m);\
    JNI_CHECK_ENV_RETURN(env, false, "CallLongMethod " #m); \
    return true; \
}

#define DECLARE_JAVA_METHOD_FLOAT(m) \
static bool m(JNIEnv *env, jobject jobj, float &ret) \
{ \
    IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(env, jobj, false, m, "()F");\
    ret = env->CallFloatMethod(jobj, jm_##m);\
    JNI_CHECK_ENV_RETURN(env, false, "CallFloatMethod " #m); \
    return true; \
}

#define DECLARE_JAVA_METHOD_OBJECT(m, s) \
static bool m(JNIEnv *env, jobject jobj, jobject &ret) \
{ \
    IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(env, jobj, false, m, s);\
    ret = static_cast<jobject>(env->CallObjectMethod(jobj, jm_##m));\
    JNI_CHECK_ENV_RETURN(env, false, "CallObjectMethod " #m); \
    return true; \
}

#define DECLARE_JAVA_METHOD_BOOL(m) \
static bool m(JNIEnv *env, jobject jobj, bool &ret) \
{ \
    IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(env, jobj, false, m, "()Z");\
    ret = env->CallBooleanMethod(jobj, jm_##m);\
    JNI_CHECK_ENV_RETURN(env, false, "CallBooleanMethod " #m); \
    return true; \
}



#define CALL_JAVA_METHOD_VOID(o, m, s, ...) \
    do { \
        auto *env_cc = orc::android::jni::AttachCurrentThreadIfNeeded(); \
        CALL_JAVA_METHOD_VOID2(env_cc, o, m, s, ##__VA_ARGS__); \
    } while(0)

#define CALL_JAVA_METHOD_VOID2(v, o, m, s, ...) \
    do { \
        IMPLEMENT_JAVA_OBJECT_METHOD_ID(v, o, m, s); \
        if (jm_##m) { \
            v->CallVoidMethod(o, jm_##m, ##__VA_ARGS__); \
            JNI_CHECK_ENV(v, "CallVoidMethod " #m " " s) \
        } else {\
            KLOGE("[jni] Failed to call " << (#m) << ", jmethodID is null"); \
        } \
    } while(0)

#define CALL_JAVA_METHOD_VOID_R(o, e, m, s, ...) \
    do { \
        auto *env_cc = orc::android::jni::AttachCurrentThreadIfNeeded(); \
        CALL_JAVA_METHOD_VOID_R2(env_cc, o, e, m, s, ##__VA_ARGS__); \
    } while(0)

#define CALL_JAVA_METHOD_VOID_R2(v, o, e, m, s, ...) \
    do { \
        IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(v, o, e, m, s); \
        if (jm_##m) { \
            v->CallVoidMethod(o, jm_##m, ##__VA_ARGS__); \
            JNI_CHECK_ENV_RETURN(v, e, "CallVoidMethod " #m " " s) \
        } else {\
            KLOGE("[jni] Failed to call " << (#m) << ", jmethodID is null"); \
            return e; \
        } \
    } while(0)

#define CALL_JAVA_METHOD_INT_R(o, r, e, m, s, ...) \
    do { \
        auto *env_cc = orc::android::jni::AttachCurrentThreadIfNeeded(); \
        CALL_JAVA_METHOD_INT_R2(env_cc, o, r, e, m, s, ##__VA_ARGS__); \
    } while(0)

#define CALL_JAVA_METHOD_INT_R2(v, o, r, e, m, s, ...) \
    do { \
        IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(v, o, e, m, s); \
        if (jm_##m) { \
            jint ret_cc = v->CallIntMethod(o, jm_##m, ##__VA_ARGS__); \
            JNI_CHECK_ENV_RETURN(v, e, "CallIntMethod " #m " " s); \
            r = (int)ret_cc; \
        } else {\
            KLOGE("[jni] Failed to call " << (#m) << ", jmethodID is null"); \
            return e; \
        } \
    } while(0)

#define CALL_JAVA_METHOD_BOOL_R(o, r, e, m, s, ...) \
    do { \
        auto *env_cc = orc::android::jni::AttachCurrentThreadIfNeeded(); \
        CALL_JAVA_METHOD_BOOL_R2(env_cc, o, r, e, m, s, ##__VA_ARGS__); \
    } while(0)

#define CALL_JAVA_METHOD_BOOL_R2(v, o, r, e, m, s, ...) \
    do { \
        IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(v, o, e, m, s); \
        if (jm_##m) { \
            auto ret_cc = v->CallBooleanMethod(o, jm_##m, ##__VA_ARGS__); \
            JNI_CHECK_ENV_RETURN(v, e, "CallBooleanMethod " #m " " s); \
            r = (bool)ret_cc; \
        } else {\
            KLOGE("[jni] Failed to call " << (#m) << ", jmethodID is null"); \
            return e; \
        } \
    } while(0)

#define CALL_JAVA_METHOD_FLOAT_R(o, r, e, m, s, ...) \
    do { \
        auto *env_cc = orc::android::jni::AttachCurrentThreadIfNeeded(); \
        CALL_JAVA_METHOD_FLOAT_R2(env_cc, o, r, e, m, s, ##__VA_ARGS__)); \
    } while(0)

#define CALL_JAVA_METHOD_FLOAT_R2(v, o, r, e, m, s, ...) \
    do { \
        IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(v, o, e, m, s); \
        if (jm_##m) { \
            auto ret_cc = v->CallFloatMethod(o, jm_##m, ##__VA_ARGS__); \
            JNI_CHECK_ENV_RETURN(v, e, "CallFloatMethod " #m " " s); \
            r = (float)ret_cc; \
        } else {\
            KLOGE("[jni] Failed to call " << (#m) << ", jmethodID is null"); \
            return e; \
        } \
    } while(0)

#define CALL_JAVA_METHOD_STRING_R(o, r, e, m, s, ...) \
    do { \
        auto *env_cc = orc::android::jni::AttachCurrentThreadIfNeeded(); \
        CALL_JAVA_METHOD_STRING_R2(env_cc, o, r, e, m, s, ##__VA_ARGS__); \
    } while(0)

#define CALL_JAVA_METHOD_STRING_R2(v, o, r, e, m, s, ...) \
    do { \
        jobject jstr = nullptr; \
        CALL_JAVA_METHOD_OBJECT_R(v, o, jstr, e, m, s, ##__VA_ARGS__); \
        if (jstr == nullptr) { \
            return e; \
        } \
        r = as_std_string(v, (jstring)jstr); \
        v->DeleteLocalRef(jstr);\
    } while(0)

#define CALL_JAVA_METHOD_OBJECT_R(v, o, r, e, m, s, ...) \
    do { \
        IMPLEMENT_JAVA_OBJECT_METHOD_ID_R(v, o, e, m, s); \
        if (jm_##m) { \
            r = v->CallObjectMethod(o, jm_##m, ##__VA_ARGS__); \
            JNI_CHECK_ENV_RETURN(v, e, "CallVoidMethod " #m " " s) \
        } else {\
            KLOGE("[jni] Failed to call " << (#m) << ", jmethodID is null"); \
            return e; \
        } \
    } while(0)

KUMA_JNI_NS_BEGIN


KUMA_JNI_NS_END
