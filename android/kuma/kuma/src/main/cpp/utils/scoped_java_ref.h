
#ifndef __ORC_ANDROID_JNI_SCOPED_JAVA_REF_H__
#define __ORC_ANDROID_JNI_SCOPED_JAVA_REF_H__

#include <jni.h>
#include <stddef.h>
#include <type_traits>
#include <utility>

namespace orc {
namespace android {
namespace jni {

    class ScopedJavaLocalFrame {
    public:
        explicit ScopedJavaLocalFrame(JNIEnv *env);

        ScopedJavaLocalFrame(JNIEnv *env, int capacity);

        ~ScopedJavaLocalFrame();

    private:
        ScopedJavaLocalFrame(const ScopedJavaLocalFrame &) = delete;

        void operator=(const ScopedJavaLocalFrame &) = delete;

    private:
        JNIEnv *env_;
    };


    template<typename T>
    class JavaRef;

    template<>
    class JavaRef<jobject> {
    public:
        JavaRef() : obj_(nullptr) {}

        JavaRef(std::nullptr_t) : JavaRef() {}

        ~JavaRef() {}

        jobject obj() const { return obj_; }

        bool IsNull() const { return obj_ == nullptr; }

    protected:

        JavaRef(JNIEnv *env, jobject obj) : obj_(obj) {}

        void Swap(JavaRef &other) { std::swap(obj_, other.obj_); }

        JNIEnv *SetNewLocalRef(JNIEnv *env, jobject obj);

        void SetNewGlobalRef(JNIEnv *env, jobject obj);

        void ResetLocalRef(JNIEnv *env);

        void ResetGlobalRef();

        jobject ReleaseInternal();

    private:
        JavaRef(const JavaRef &) = delete;

        void operator=(const JavaRef &) = delete;

    private:
        jobject obj_;
    };


    template<typename T>
    class JavaRef : public JavaRef<jobject> {
    public:
        JavaRef() {}

        JavaRef(std::nullptr_t) : JavaRef<jobject>(nullptr) {}

        ~JavaRef() {}

        T obj() const { return static_cast<T>(JavaRef<jobject>::obj()); }

    protected:
        JavaRef(JNIEnv *env, T obj) : JavaRef<jobject>(env, obj) {}

    private:
        JavaRef(const JavaRef &) = delete;

        void operator=(const JavaRef &) = delete;
    };

    template<typename T>
    class JavaParamRef : public JavaRef<T> {
    public:
        JavaParamRef(JNIEnv *env, T obj) : JavaRef<T>(env, obj) {}

        JavaParamRef(std::nullptr_t) : JavaRef<T>(nullptr) {}

        ~JavaParamRef() {}

        operator T() const { return JavaRef<T>::obj(); }

    private:
        JavaParamRef(const JavaParamRef &) = delete;

        void operator=(const JavaParamRef &) = delete;
    };


    template<typename T>
    class ScopedJavaLocalRef : public JavaRef<T> {
    public:
        ScopedJavaLocalRef() : env_(nullptr) {}

        ScopedJavaLocalRef(std::nullptr_t) : env_(nullptr) {}

        ScopedJavaLocalRef(const ScopedJavaLocalRef<T> &other)
                : env_(other.env_) {
            this->SetNewLocalRef(env_, other.obj());
        }

        ScopedJavaLocalRef(ScopedJavaLocalRef<T> &&other) : env_(other.env_) {
            this->Swap(other);
        }

        explicit ScopedJavaLocalRef(const JavaRef<T> &other) : env_(nullptr) {
            this->Reset(other);
        }

        ScopedJavaLocalRef(JNIEnv *env, T obj) : JavaRef<T>(env, obj), env_(env) {}

        ~ScopedJavaLocalRef() {
            this->Reset();
        }

        void operator=(const ScopedJavaLocalRef<T> &other) {
            this->Reset(other);
        }

        void operator=(ScopedJavaLocalRef<T> &&other) {
            env_ = other.env_;
            this->Swap(other);
        }

        void Reset() {
            this->ResetLocalRef(env_);
        }

        void Reset(const ScopedJavaLocalRef<T> &other) {

            this->Reset(other.env_, other.obj());
        }

        void Reset(const JavaRef<T> &other) {
            this->Reset(env_, other.obj());
        }

        void Reset(JNIEnv *env, T obj) { env_ = this->SetNewLocalRef(env, obj); }

        T Release() {
            return static_cast<T>(this->ReleaseInternal());
        }

    private:

        JNIEnv *env_;

        ScopedJavaLocalRef(JNIEnv *env, const JavaParamRef<T> &other);
    };


    template<typename T>
    class ScopedJavaGlobalRef : public JavaRef<T> {
    public:
        ScopedJavaGlobalRef() {}

        ScopedJavaGlobalRef(std::nullptr_t) {}

        ScopedJavaGlobalRef(const ScopedJavaGlobalRef<T> &other) {
            this->Reset(other);
        }

        ScopedJavaGlobalRef(ScopedJavaGlobalRef<T> &&other) { this->Swap(other); }

        ScopedJavaGlobalRef(JNIEnv *env, T obj) { this->Reset(env, obj); }

        explicit ScopedJavaGlobalRef(const JavaRef<T> &other) { this->Reset(other); }

        ~ScopedJavaGlobalRef() {
            this->Reset();
        }

        void operator=(_jclass *other) {
            this->Reset(other);
        }

        void operator=(ScopedJavaGlobalRef<T> &&other) { this->Swap(other); }

        void Reset() {
            this->ResetGlobalRef();
        }

        void Reset(const JavaRef<T> &other) { this->Reset(nullptr, other.obj()); }

        void Reset(JNIEnv *env, const JavaParamRef<T> &other) {
            this->Reset(env, other.obj());
        }

        void Reset(JNIEnv *env, T obj) { this->SetNewGlobalRef(env, obj); }

        T Release() {
            return static_cast<T>(this->ReleaseInternal());
        }
    };

} // namespace jni
} // namespace android
} // namespace orc


#endif //__ORC_ANDROID_JNI_SCOPED_JAVA_REF_H__
