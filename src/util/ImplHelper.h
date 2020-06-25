#ifndef __KEV_ImplHelper_h__
#define __KEV_ImplHelper_h__

#include <string>
#include <type_traits>

namespace kev {

template <typename Impl>
struct ImplHelper {
    using ImplPtr = std::shared_ptr<Impl>;
    Impl impl;
    ImplPtr ptr;

    // accept void arg
    template <typename... Args>
    ImplHelper(Args&... args)
        : impl(args...)
    {
        ptr.reset(&impl, [](Impl* p) {
            auto h = reinterpret_cast<ImplHelper*>(p);
            delete h;
        });
    }

    template <typename T1, typename... Args>
    ImplHelper(T1 &t1, Args&... args)
        : impl(t1, args...)
    {
        ptr.reset(&impl, [](Impl* p) {
            auto h = reinterpret_cast<ImplHelper*>(p);
            delete h;
        });
    }

    template <typename T1, typename... Args>
    ImplHelper(T1 &&t1, Args&&... args)
        : impl(std::forward<T1>(t1), std::forward<Args...>(args)...)
    {
        ptr.reset(&impl, [](Impl* p) {
            auto h = reinterpret_cast<ImplHelper*>(p);
            delete h;
        });
    }
    
    template <typename... Args>
    static Impl* create(Args&... args)
    {
        auto *ih = new ImplHelper(args...);
        return &ih->impl;
    }

    template <typename T1, typename... Args>
    static Impl* create(T1 &t1, Args&... args)
    {
        auto *ih = new ImplHelper(t1, args...);
        return &ih->impl;
    }
    
    template <typename T1, typename... Args>
    static Impl* create(T1 &&t1, Args&&... args)
    {
        auto *ih = new ImplHelper(std::forward<T1>(t1), std::forward<Args...>(args)...);
        return &ih->impl;
    }
    
    static void destroy(Impl *pimpl)
    {
        if (pimpl) {
            auto *ih = reinterpret_cast<ImplHelper*>(pimpl);
            ih->ptr.reset();
        }
    }
    
    static ImplPtr implPtr(Impl *pimpl)
    {
        if (pimpl) {
            auto h = reinterpret_cast<ImplHelper*>(pimpl);
            return h->ptr;
        }
        return ImplPtr();
    }

private:
    ~ImplHelper() = default;
};

} // namespace kev

#endif // __KEV_ImplHelper_h__
