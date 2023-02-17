/* Copyright (c) 2014-2023, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __ImplHelper_h__
#define __ImplHelper_h__

#include <type_traits>

KUMA_NS_BEGIN

template <typename Impl>
struct ImplHelper {
    using ImplPtr = std::shared_ptr<Impl>;
    Impl impl;
    ImplPtr ptr;

    template <typename... Args>
    ImplHelper(Args&... args)
        : impl(args...)
    {
        ptr.reset(&impl, [](Impl* p) {
            auto *ih = reinterpret_cast<ImplHelper*>(p);
            delete ih;
        });
    }

    template <typename... Args>
    ImplHelper(Args&&... args)
        : impl(std::forward<Args>(args)...)
    {
        ptr.reset(&impl, [](Impl* p) {
            auto *ih = reinterpret_cast<ImplHelper*>(p);
            delete ih;
        });
    }
    
    template <typename... Args>
    static Impl* create(Args&... args)
    {
        auto *ih = new ImplHelper(args...);
        return &ih->impl;
    }
    
    template <typename... Args>
    static Impl* create(Args&&... args)
    {
        auto *ih = new ImplHelper(std::forward<Args>(args)...);
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
            auto *ih = reinterpret_cast<ImplHelper*>(pimpl);
            return ih->ptr;
        }
        return ImplPtr();
    }

private:
    ~ImplHelper() = default;
};

KUMA_NS_END

#endif // __ImplHelper_h__
