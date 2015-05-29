#ifndef __KUMAEVDEFS_H__
#define __KUMAEVDEFS_H__

#include "kmdefs.h"

#include <mutex>
#include <functional>

KUMA_NS_BEGIN

#define KUMA_EV_READ    1
#define KUMA_EV_WRITE   (1 << 1)
#define KUMA_EV_ERROR   (1 << 2)
#define KUMA_EV_NETWORK (KUMA_EV_READ|KUMA_EV_WRITE|KUMA_EV_ERROR)

#ifdef KUMA_OS_WIN
# define SOCKET_FD   SOCKET
# define closeFd   ::closesocket
# define getLastError() WSAGetLastError()
#else
# define SOCKET_FD   int
# define closeFd   ::close
# define getLastError() errno
#endif

#define INVALID_FD  ((SOCKET_FD)-1)

typedef std::function<void(uint32_t)> IOCallback;
typedef std::function<void(void)> LoopCallback;
typedef std::function<void(void)> TimerCallback;

typedef std::recursive_mutex KM_Mutex;
typedef std::lock_guard<KM_Mutex> KM_Lock_Guard;

typedef enum {
    POLL_TYPE_NONE,
    POLL_TYPE_POLL,
    POLL_TYPE_EPOLL,
    POLL_TYPE_SELECT,
    POLL_TYPE_WIN
}PollType;

KUMA_NS_END

#endif
