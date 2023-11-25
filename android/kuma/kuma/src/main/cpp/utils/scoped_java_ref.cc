
#include "utils/scoped_java_ref.h"
#include "utils/jvm.h"

namespace orc {
namespace android {
namespace jni {

    namespace {
        const int kDefaultLocalFrameCapacity = 16;
    }

    ScopedJavaLocalFrame::ScopedJavaLocalFrame(JNIEnv *env) : env_(env) {
        int failed = env_->PushLocalFrame(kDefaultLocalFrameCapacity);
    }

    ScopedJavaLocalFrame::ScopedJavaLocalFrame(JNIEnv *env, int capacity)
            : env_(env) {
        int failed = env_->PushLocalFrame(capacity);
    }

    ScopedJavaLocalFrame::~ScopedJavaLocalFrame() {
        env_->PopLocalFrame(nullptr);
    }


    JNIEnv *JavaRef<jobject>::SetNewLocalRef(JNIEnv *env, jobject obj) {
        if (!env) {
            env = orc::android::jni::AttachCurrentThreadIfNeeded();
        }
        if (obj)
            obj = env->NewLocalRef(obj);
        if (obj_)
            env->DeleteLocalRef(obj_);
        obj_ = obj;
        return env;
    }

    void JavaRef<jobject>::SetNewGlobalRef(JNIEnv *env, jobject obj) {
        if (!env) {
            env = orc::android::jni::AttachCurrentThreadIfNeeded();
        }
        if (obj)
            obj = env->NewGlobalRef(obj);
        if (obj_)
            env->DeleteGlobalRef(obj_);
        obj_ = obj;
    }

    void JavaRef<jobject>::ResetLocalRef(JNIEnv *env) {
        if (obj_) {
            env->DeleteLocalRef(obj_);
            obj_ = nullptr;
        }
    }

    void JavaRef<jobject>::ResetGlobalRef() {
        if (obj_) {
            orc::android::jni::AttachCurrentThreadIfNeeded()->DeleteGlobalRef(obj_);
            obj_ = nullptr;
        }
    }

    jobject JavaRef<jobject>::ReleaseInternal() {
        jobject obj = obj_;
        obj_ = nullptr;
        return obj;
    }

} // namespace jni
} // namespace android
} // namespace orc


