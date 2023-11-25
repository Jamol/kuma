

#ifndef __ORC_ANDROID_JNI_JVM_H__
#define __ORC_ANDROID_JNI_JVM_H__

#include <jni.h>

namespace orc {
namespace android {
namespace jni {

    jint InitGlobalJniVariables(JavaVM *jvm);

    JNIEnv *GetEnv();

    JavaVM *GetJVM();

    JNIEnv *AttachCurrentThreadIfNeeded();

} // namespace jni
} // namespace android
} // namespace orc


#endif //__ORC_ANDROID_JNI_JVM_H__
