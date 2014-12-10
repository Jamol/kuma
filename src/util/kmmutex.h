#ifndef __KM_MUTEX_H__
#define __KM_MUTEX_H__

#include "kmconf.h"
#include "kmdefs.h"
#ifdef KUMA_HAS_CXX0X
# include <mutex>
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

KUMA_NS_BEGIN

#ifdef KUMA_HAS_CXX0X
typedef std::recursive_mutex KM_Mutex;
typedef std::lock_guard<KM_Mutex> KM_Mutex_Guard;
#else

class KM_Mutex
{
public:
    KM_Mutex()
    {
#ifdef KUMA_OS_WIN
        InitializeCriticalSection(&cs_);
#else
        pthread_mutexattr_t mutexattr;
        pthread_mutexattr_init(&mutexattr);
        pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&mutex_, &mutexattr);
        pthread_mutexattr_destroy(&mutexattr);
#endif
    }
    virtual ~KM_Mutex()
    {
#ifdef KUMA_OS_WIN
        DeleteCriticalSection(&cs_);
#else
        pthread_mutex_destroy(&mutex_);
#endif
    }

    virtual void lock()
    {
#ifdef KUMA_OS_WIN
        EnterCriticalSection(&cs_);
#else
        pthread_mutex_lock(&mutex_);
#endif
    }
    virtual void unlock()
    {
#ifdef KUMA_OS_WIN
        LeaveCriticalSection(&cs_);
#else
        pthread_mutex_unlock(&mutex_);
#endif
    }
    virtual bool try_lock()
    {
#ifdef KUMA_OS_WIN
        return TryEnterCriticalSection(&cs_)?true:false;
#else
        int err = pthread_mutex_trylock(&mutex_);
        if (err == 0) return true;
        return false;
#endif
    }
protected :
#ifdef KUMA_OS_WIN
    CRITICAL_SECTION cs_;
#else
    pthread_mutex_t mutex_;
#endif
};
#endif // KUMA_HAS_CXX0X

KUMA_NS_END

#endif
