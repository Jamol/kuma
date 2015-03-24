#ifndef __KMTHREAD_H__
#define __KMTHREAD_H__

#include "kmconf.h"
#include "kmdefs.h"
#ifdef KUMA_HAS_CXX0X
# include <thread>
# define THREAD_HANDLE          std::thread::native_handle_type
# define THREAD_ID              std::thread::id
#else
# ifdef KUMA_OS_WIN
#  if _MSC_VER > 1200
#   define _WINSOCKAPI_	// Prevent inclusion of winsock.h in windows.h
#  endif
#  include <Windows.h>
#  include <process.h>
#  define THREAD_HANDLE         HANDLE
#  define THREAD_ID             unsigned long
# else // KUMA_OS_WIN
#  include <pthread.h>
#  ifdef KUMA_OS_MAC
#   define GetCurrentThreadId() pthread_mach_thread_np(pthread_self())
#  else // KUMA_OS_MAC
#   ifndef GetCurrentThreadId
#    define GetCurrentThreadId  pthread_self
#   endif
#  endif // KUMA_OS_MAC
#  define THREAD_HANDLE         pthread_t
#  define THREAD_ID             unsigned long
# endif // KUMA_OS_WIN
#endif // KUMA_HAS_CXX0X

KUMA_NS_BEGIN

class KM_Thread
{
public:
	KM_Thread()
    {
#ifdef KUMA_HAS_CXX0X
#else
        thread_id_ = 0;
#ifdef KUMA_OS_WIN
        thread_handle_ = NULL;
#endif
#endif
    }

	virtual ~KM_Thread()
    {
#ifdef KUMA_HAS_CXX0X
#else
#ifdef KUMA_OS_WIN
        if(thread_handle_)
        {
            ::CloseHandle(thread_handle_);
            thread_handle_ = NULL;
        }
#endif
#endif
    }

#ifdef KUMA_HAS_CXX0X
    KM_Thread(KM_Thread&& other)
	: thread_(std::move(other.thread_))
	{
	}

	KM_Thread& operator=(KM_Thread&& other)
	{
        thread_ = std::move(other.thread_);
        return *this;
	}
#endif

	virtual int thread_run() = 0;

	bool thread_start()
    {
#ifdef KUMA_HAS_CXX0X
        try
        {
            std::thread thr(&KM_Thread::thread_run_i, this);
            thread_ = std::move(thr);
        }
        catch(...)
        {
            return false;
        }
        return true;
#else
#ifdef KUMA_OS_WIN
        unsigned int thread_id = 0;
        thread_handle_ = (HANDLE)_beginthreadex(NULL, 0, thread_proc, this, 0, &thread_id);
        thread_id_ = thread_id;
        return thread_handle_ != NULL;
#else
        //pthread_attr_t attr;
        //pthread_attr_init(&attr);
        //pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);
        //pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        int ret = pthread_create(&thread_handle_, NULL/*&attr*/,
            thread_proc, this);
        //pthread_attr_destroy(&attr);

#ifdef MACOS
        thread_id_ = pthread_mach_thread_np(thread_handle_);
#else
        thread_id_ = (THREAD_ID)thread_handle_;
#endif
        return thread_handle_ != (THREAD_HANDLE)NULL;
#endif // KUMA_OS_WIN
#endif // KUMA_HAS_CXX0X
    }

	void thread_join()
    {
#ifdef KUMA_HAS_CXX0X
        try
        {
            thread_.join();
        }
        catch(...)
        {
        }
#else
        if(is_self_thread())
        {
            return ;
        }
#ifdef KUMA_OS_WIN
        if(thread_handle_ != NULL) {
            ::WaitForSingleObject(thread_handle_, INFINITE);
            ::CloseHandle(thread_handle_);
            thread_handle_ = NULL;
        }
#else
        pthread_join(thread_handle_, NULL);
#endif // KUMA_OS_WIN
#endif // KUMA_HAS_CXX0X
    }

	THREAD_HANDLE get_thread_handle()
    {
#ifdef KUMA_HAS_CXX0X
        return thread_.native_handle();
#else
        return thread_handle_;
#endif
    }

	THREAD_ID get_thread_id()
    {
#ifdef KUMA_HAS_CXX0X
        return thread_.get_id();
#else
        return thread_id_;
#endif
    }

private:

#ifndef KUMA_HAS_CXX0X
#ifdef KUMA_OS_WIN
	static unsigned int WINAPI thread_proc(void* param)
#else
	static void* thread_proc(void* param)
#endif
    {
        KM_Thread* _this = (KM_Thread*)param;
        _this->thread_run_i();

#ifdef KUMA_OS_WIN
    //_endthreadex( 0 );
#endif
        return 0;
    }
#endif

    void thread_run_i()
    {
        thread_run();
    }

	bool is_self_thread()
    {
#ifdef KUMA_HAS_CXX0X
        return thread_.get_id() == std::this_thread::get_id();
#else
#ifdef KUMA_OS_WIN
        return thread_id_ == GetCurrentThreadId();
#else
        return thread_handle_ == pthread_self();
#endif
#endif // KUMA_HAS_CXX0X
    }

private:
#ifdef KUMA_HAS_CXX0X
    std::thread     thread_;
#else
	THREAD_ID		thread_id_;
	THREAD_HANDLE	thread_handle_;
#endif
};

KUMA_NS_END

#endif
