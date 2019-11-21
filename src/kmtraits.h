// 
// Functor wrapper for move-only objects
// https://stackoverflow.com/questions/25330716/move-only-version-of-stdfunction
//
#pragma once

#include <type_traits>

KUMA_NS_BEGIN

template<typename Fn, typename En = void>
struct wrapper;

template<typename Fn>
struct wrapper<Fn, std::enable_if_t< std::is_copy_constructible<Fn>{} >>
{
    Fn fn;

    template<typename... Args>
    auto operator()(Args&&... args) { return fn(std::forward<Args>(args)...); }
};

template<typename Fn>
struct wrapper<Fn, std::enable_if_t< !std::is_copy_constructible<Fn>{}
    && std::is_move_constructible<Fn>{} >>
{
    Fn fn;

    wrapper(Fn &&fn) : fn(std::forward<Fn>(fn)) { }

    wrapper(wrapper&&) = default;
    wrapper& operator=(wrapper&&) = default;

    wrapper(const wrapper& rhs) : fn(const_cast<Fn&&>(rhs.fn)) { throw 0; }
    wrapper& operator=(wrapper&) { throw 0; }

    template<typename... Args>
    auto operator()(Args&&... args) { return fn(std::forward<Args>(args)...); }
};

KUMA_NS_END
