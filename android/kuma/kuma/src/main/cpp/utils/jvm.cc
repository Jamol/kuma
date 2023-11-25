#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <pthread.h>
#include "utils/jvm.h"

#include <string>


namespace orc {
namespace android {
namespace jni {

    static JavaVM *g_jvm = nullptr;

    static pthread_once_t g_jni_ptr_once = PTHREAD_ONCE_INIT;

    static pthread_key_t g_jni_ptr;

    JavaVM *GetJVM() {
        return g_jvm;
    }

    JNIEnv *GetEnv() {
        void *env = nullptr;
        jint status = g_jvm->GetEnv(&env, JNI_VERSION_1_6);
        return reinterpret_cast<JNIEnv *>(env);
    }

    static void ThreadDestructor(void *prev_jni_ptr) {
        if (!GetEnv())
            return;

        jint status = g_jvm->DetachCurrentThread();
    }

    static void CreateJNIPtrKey() {
        pthread_key_create(&g_jni_ptr, &ThreadDestructor);
    }

    jint InitGlobalJniVariables(JavaVM *jvm) {
        g_jvm = jvm;

        pthread_once(&g_jni_ptr_once, &CreateJNIPtrKey);

        JNIEnv *jni = nullptr;
        if (jvm->GetEnv(reinterpret_cast<void **>(&jni), JNI_VERSION_1_6) != JNI_OK)
            return -1;

        return JNI_VERSION_1_6;
    }

    std::string GetThreadId() {
        char buf[21];
        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(syscall(__NR_gettid)));
        return std::string(buf);
    }

    static std::string GetThreadName() {
        char name[17] = {0};
        if (prctl(PR_GET_NAME, name) != 0)
            return std::string("<noname>");
        return std::string(name);
    }

    JNIEnv *AttachCurrentThreadIfNeeded() {
        JNIEnv *jni = GetEnv();
        if (jni)
            return jni;

        std::string name(GetThreadName() + " - " + GetThreadId());
        JavaVMAttachArgs args;
        args.version = JNI_VERSION_1_6;
        args.name = &name[0];
        args.group = nullptr;
#ifdef _JAVASOFT_JNI_H_  // Oracle's jni.h violates the JNI spec!
        void* env = nullptr;
#else
        JNIEnv *env = nullptr;
#endif
        if (g_jvm->AttachCurrentThread(&env, &args) < 0) {
            return nullptr;
        }
        jni = reinterpret_cast<JNIEnv*>(env);
        pthread_setspecific(g_jni_ptr, jni);
        return jni;
    }

} // namespace jni
} // namespace android
} // namespace orc