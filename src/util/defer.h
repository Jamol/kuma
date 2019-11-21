//
//  defer.h
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 9/9/15.
//  Copyright © 2015 kuma. All rights reserved.
//

#pragma once


namespace kuma {

template<class Callable>
class DeferExec final
{
public:
    DeferExec(Callable c) : c_(std::move(c)) {}
    DeferExec(DeferExec &&other) : c_(std::move(other.c_)) {}
    ~DeferExec() { c_(); }
    
    DeferExec(const DeferExec &) = delete;
    void operator=(const DeferExec &) = delete;
private:
    Callable c_;
};

template <typename Callable>
DeferExec<Callable> make_defer(Callable c) {
    return {std::move(c)};
}

} // namespace kuma

#define CONCAT_XY(x, y) x##y
#define MAKE_DEFER(r, l) auto CONCAT_XY(defer_exec_, l) = kuma::make_defer([&] () { r; })
#define DEFER(r) MAKE_DEFER(r, __LINE__)

