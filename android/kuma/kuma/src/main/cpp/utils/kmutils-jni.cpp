
#include <jni.h>
#include "kmutils-jni.h"

#include "jvm.h"

#include <codecvt>
#include <string>
#include <iostream>

using orc::android::jni::ScopedJavaLocalRef;
using orc::android::jni::ScopedJavaGlobalRef;


KUMA_JNI_NS_BEGIN

ScopedJavaLocalRef<jbyteArray> as_jbyteArray(JNIEnv* env, const void* buf, int len) {
    jbyteArray jbuf = env->NewByteArray(len);
    env->SetByteArrayRegion(jbuf, 0, len, reinterpret_cast<const jbyte*>(buf));
    return {env, jbuf};
}

ScopedJavaLocalRef<jlongArray> as_jlongArray(JNIEnv* env, const jlong* buf, int len)
{
    jlongArray jbuf = env->NewLongArray(len);
    env->SetLongArrayRegion(jbuf, 0, len, buf);
    return {env, jbuf};
}

std::vector<uint8_t> as_std_vector(JNIEnv* env, jbyteArray array) {
    std::vector<uint8_t> vec;
    if (!array) {
        return vec;
    }
    auto len = env->GetArrayLength(array);
    if (len < 0) {
        return vec;
    }
    vec.resize(len);
    env->GetByteArrayRegion(array, 0, len, reinterpret_cast<jbyte*>(&vec[0]));
    return vec;
}

std::vector<uint64_t> as_std_vector(JNIEnv* env, jlongArray array)
{
    std::vector<uint64_t> vec;
    if (!array) {
        return vec;
    }
    auto len = env->GetArrayLength(array);
    if (len < 0) {
        return vec;
    }
    vec.resize(len);
    env->GetLongArrayRegion(array, 0, len, reinterpret_cast<jlong*>(&vec[0]));
    return vec;
}

std::string as_std_string(JNIEnv* env, jstring jstr)
{
    if (!jstr) {
        return "";
    }
    auto clen = env->GetStringUTFLength(jstr);
    if (clen == 0)
        return "";
    std::string str(clen, '\0');
    auto jlen = env->GetStringLength(jstr);
    env->GetStringUTFRegion(jstr, 0, jlen, const_cast<char*>(str.data()));

    JNI_CHECK_ENV_RETURN(env, "", "GetStringUTFRegion in as_std_string");

    return str;
}

std::string as_std_string2(JNIEnv* env, jstring jstr)
{
    if (!jstr) {
        return "";
    }
    const char *cstr= env->GetStringUTFChars(jstr, nullptr);
    JNI_CHECK_ENV_RETURN(env, "", "GetStringUTFChars in as_std_string2");
    auto sz = env->GetStringUTFLength(jstr);
    JNI_CHECK_ENV_RETURN(env, "", "GetStringUTFLength in as_std_string2");
    auto len = static_cast<size_t>(sz);
    std::string str(cstr, len);
    env->ReleaseStringUTFChars(jstr, cstr);

    return str;
}

ScopedJavaLocalRef<jstring> as_jstring(JNIEnv* env, const std::string &str)
{
    auto jstr = env->NewStringUTF(str.c_str());
    if (env->ExceptionCheck()) {
        KM_ERRTRACE("[jni] as_jstring exception, str=" << str);
        env->ExceptionDescribe();
        env->ExceptionClear();
        return {env, env->NewStringUTF("")};
    }
    return {env, jstr};
}

ScopedJavaLocalRef<jstring> as_jstring_utf16(JNIEnv* env, const std::string &str)
{
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convertor;
    std::u16string u16str;
    try {
        u16str = convertor.from_bytes(str);
    } catch (std::exception &e) {
        KM_ERRTRACE("as_jstring_utf16, exception, str=" << str << ", e=" << e.what());
        return as_jstring(env, "");
    } catch (...) {
        KM_ERRTRACE("as_jstring_utf16, unknown exception, str=" << str);
        return as_jstring(env, "");
    }
    auto jstr = env->NewString((const jchar*)u16str.c_str(), u16str.size());
    if (env->ExceptionCheck()) {
        KM_ERRTRACE("[jni] as_jstring_utf16 exception, str=" << str);
        env->ExceptionDescribe();
        env->ExceptionClear();
        return as_jstring(env, "");
    }
    return {env, jstr};
}

KMBuffer as_kmbuffer(JNIEnv* env, jbyteArray jarr)
{
    if (!jarr) {
        return {};
    }
    auto len = env->GetArrayLength(jarr);
    if (len < 0) {
        return {};
    }
    auto *buf = env->GetByteArrayElements(jarr, nullptr);
    if (!buf) {
        return {};
    }
    jarr = reinterpret_cast<jbyteArray>(env->NewGlobalRef(jarr));
    auto deleter = [jarr](void* buf, size_t) {
        auto *env = orc::android::jni::AttachCurrentThreadIfNeeded();
        env->ReleaseByteArrayElements(jarr, static_cast<jbyte *>(buf), JNI_ABORT);
        env->DeleteGlobalRef(jarr);
    };
    KMBuffer kbuf(buf, len, len, 0, std::move(deleter));
    return kbuf;
}

KMBuffer as_kmbuffer2(JNIEnv* env, jbyteArray jarr)
{
    if (!jarr) {
        return {};
    }
    auto len = env->GetArrayLength(jarr);
    if (len < 0) {
        return {};
    }
    KMBuffer kbuf(len);
    env->GetByteArrayRegion(jarr, 0, len, reinterpret_cast<jbyte*>(kbuf.writePtr()));
    kbuf.bytesWritten(len);
    return kbuf;
}

KMBuffer as_kmbuffer(JNIEnv* env, jstring jstr)
{
    if (!jstr) {
        return {};
    }
    auto clen = env->GetStringUTFLength(jstr);
    if (clen == 0)
        return {};
    KMBuffer kbuf(clen);
    auto jlen = env->GetStringLength(jstr);
    env->GetStringUTFRegion(jstr, 0, jlen, reinterpret_cast<char*>(kbuf.writePtr()));
    kbuf.bytesWritten(clen);

    JNI_CHECK_ENV_RETURN(env, {}, "GetStringUTFRegion in as_std_string");

    return kbuf;
}

KMBuffer as_kmbuffer(JNIEnv* env, jobject jbuf)
{
    if (!jbuf) {
        return {};
    }
    auto *buf = (jbyte*)env->GetDirectBufferAddress(jbuf);
    jlong len = env->GetDirectBufferCapacity(jbuf);
    if (!buf || len <=0) {
        return {};
    }
    orc::android::jni::ScopedJavaGlobalRef<jobject> obj{env, jbuf};
    auto deleter = [obj{std::move(obj)}](void*, size_t) {};
    KMBuffer kbuf(buf, len, len, 0, std::move(deleter));
    return kbuf;
}

ScopedJavaLocalRef<jstring> as_jstring_utf16(JNIEnv* env, const KMBuffer &kmbuf)
{
    auto *first = static_cast<char*>(kmbuf.readPtr());
    auto *last = first + kmbuf.size();
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convertor;
    std::u16string u16str;
    try {
        if (!kmbuf.isChained()) {
            u16str = convertor.from_bytes(first, last);
        } else {
            std::string str;
            auto it = kmbuf.begin();
            for (;it != kmbuf.end(); ++it) {
                if (!it->empty()) {
                    str.append(reinterpret_cast<const char*>(it->readPtr()), it->size());
                }
            }
            u16str = convertor.from_bytes(str);
        }
    } catch (std::exception &e) {
        std::string str(first, last);
        KM_ERRTRACE("as_jstring_utf16, exception, str=" << str << ", e=" << e.what());
        return as_jstring(env, "");
    } catch (...) {
        std::string str(first, last);
        KM_ERRTRACE("as_jstring_utf16, unknown exception, str=" << str);
        return as_jstring(env, "");
    }
    auto jstr = env->NewString((const jchar*)u16str.c_str(), (jsize)u16str.size());
    if (env->ExceptionCheck()) {
        std::string str(first, last);
        KM_ERRTRACE("[jni] as_jstring_utf16 exception, str=" << str);
        env->ExceptionDescribe();
        env->ExceptionClear();
        return as_jstring(env, "");
    }
    return {env, jstr};
}

ScopedJavaLocalRef<jbyteArray> as_jbyteArray(JNIEnv* env, const KMBuffer &kmbuf)
{
    auto len = static_cast<jsize>(kmbuf.chainLength());
    jbyteArray jarr = env->NewByteArray(len);
    if (!kmbuf.isChained()) {
        env->SetByteArrayRegion(jarr, 0, len, reinterpret_cast<const jbyte *>(kmbuf.readPtr()));
    } else {
        int8_t* arr_ptr = env->GetByteArrayElements(jarr, nullptr);
        size_t off = 0;
        auto it = kmbuf.begin();
        for (;it != kmbuf.end(); ++it) {
            if (!it->empty()) {
                memcpy(arr_ptr+off, it->readPtr(), it->size());
                off += it->size();
            }
        }
        env->ReleaseByteArrayElements(jarr, arr_ptr, 0);
    }
    return {env, jarr};
}

KUMA_JNI_NS_END
