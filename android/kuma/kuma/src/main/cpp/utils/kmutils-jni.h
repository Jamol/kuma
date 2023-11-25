#pragma once

#include "kmbuffer.h"
#include "km-jni.h"
#include "scoped_java_ref.h"

#include <string>
#include <vector>

KUMA_JNI_NS_BEGIN

orc::android::jni::ScopedJavaLocalRef<jbyteArray> as_jbyteArray(JNIEnv* env, const void* buf, int len);
orc::android::jni::ScopedJavaLocalRef<jlongArray> as_jlongArray(JNIEnv* env, const jlong* buf, int len);
std::vector<uint8_t> as_std_vector(JNIEnv* env, jbyteArray array);
std::vector<uint64_t> as_std_vector(JNIEnv* env, jlongArray array);
std::string as_std_string(JNIEnv* env, jstring jstr);
orc::android::jni::ScopedJavaLocalRef<jstring> as_jstring(JNIEnv* env, const std::string &str);
orc::android::jni::ScopedJavaLocalRef<jstring> as_jstring_utf16(JNIEnv* env, const std::string &str);

KMBuffer as_kmbuffer(JNIEnv* env, jstring jstr);
KMBuffer as_kmbuffer(JNIEnv* env, jbyteArray jarr);
KMBuffer as_kmbuffer(JNIEnv* env, jobject jbuf);
orc::android::jni::ScopedJavaLocalRef<jstring> as_jstring_utf16(JNIEnv* env, const KMBuffer &kmbuf);
orc::android::jni::ScopedJavaLocalRef<jbyteArray> as_jbyteArray(JNIEnv* env, const KMBuffer &kmbuf);

KUMA_JNI_NS_END
