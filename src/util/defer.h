//
//  defer.h
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 9/9/15.
//  Copyright © 2015 kuma. All rights reserved.
//

#ifndef __defer_h__
#define __defer_h__

#include <functional>

namespace kuma {

class DeferExec final
{
public:
    DeferExec(std::function<void(void)> f) : f_(std::move(f)) {};
    ~DeferExec() { if(f_) f_(); }
    void reset() { f_ = nullptr; }
    
private:
    std::function<void(void)> f_;
};

} // namespace kuma

#define CONCAT_XY(x, y) x##y
#define MAKE_DEFER(r, l) kuma::DeferExec CONCAT_XY(defer_exec_, l)(r)
#define DEFER(r) MAKE_DEFER(r, __LINE__)

#endif // __defer_h__
