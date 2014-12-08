#ifndef __KM_ATOMIC_H__
#define __KM_ATOMIC_H__

#include "kmconf.h"
#ifdef KUMA_HAS_CXX0X
# include <atomic>
#else // KUMA_HAS_CXX0X
# ifdef KUMA_OS_WIN
#  if _MSC_VER > 1200
#   define _WINSOCKAPI_	// Prevent inclusion of winsock.h in windows.h
#  endif
#  include <windows.h>
# else // KUMA_OS_WIN
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/time.h>
#  include <string.h>
#  include <pthread.h>
# endif // KUMA_OS_WIN
#endif // KUMA_HAS_CXX0X

namespace komm {;

#ifdef KUMA_HAS_CXX0X
typedef std::atomic_long KM_Atomic;
#else

class KM_Atomic
{
public :
    KM_Atomic() : value_(0) {}
    ~KM_Atomic() {}

    long operator++ ()
    {
#ifdef KUMA_OS_WIN
        return ::InterlockedIncrement(const_cast<long*>(&value_));
#else
        return (*increment_fn_)(&value_);
#endif // KUMA_OS_WIN
    }

    long operator++ (int)
    {
        return ++*this - 1;
    }

    long operator-- (void)
    {
#ifdef KUMA_OS_WIN
        return ::InterlockedDecrement(const_cast<long*>(&value_));
#else
        return (*decrement_fn_)(&value_);
#endif // KUMA_OS_WIN
    }

    long operator-- (int)
    {
        return --*this + 1;
    }

    long operator+= (long v)
    {
#ifdef KUMA_OS_WIN
        return ::InterlockedExchangeAdd(const_cast<long*>(&value_), v) + v;
#else
        return (*exchange_add_fn_)(&value_, v) + v;
#endif // KUMA_OS_WIN
    }

    long operator-= (long v)
    {
#ifdef KUMA_OS_WIN
        return ::InterlockedExchangeAdd(const_cast<long*>(&value_), -v) - v;
#else
        return (*exchange_add_fn_)(&value_, -v) - v;
#endif // KUMA_OS_WIN
    }

    bool operator== (long v) const
    {
        return (value_ == v);
    }

    bool operator!= (long v) const
    {
        return (value_ != v);
    }

    bool operator>= (long v) const
    {
        return (value_ >= v);
    }

    bool operator> (long v) const
    {
        return (value_ > v);
    }

    bool operator<= (long v) const
    {
        return (value_ <= v);
    }

    bool operator< (long v) const
    {
        return (value_ < v);
    }

    long operator= (long v)
    {
#ifdef KUMA_OS_WIN
        ::InterlockedExchange(const_cast<long*>(&value_), v);
#else
        (*exchange_fn_)(&value_, v);
#endif // KUMA_OS_WIN
        return v;
    }

    operator long() const
    {
        return value_;
    }

private:
    KM_Atomic(const KM_Atomic&);	// not defined
    KM_Atomic& operator=(const KM_Atomic&);	// not defined
    //KM_Atomic& operator=(const KM_Atomic&) volatile;	// not defined

protected :
    volatile long value_;
};
#endif // KUMA_HAS_CXX0X

}
#endif
