#ifndef __INTERNAL_H__
#define __INTERNAL_H__

#include "kmconf.h"
#include "kuma.h"

#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
# include <windows.h>
# include <time.h>

# pragma warning(disable: 4996)

#elif defined(KUMA_OS_LINUX)
# include <string.h>
# include <pthread.h>
# include <unistd.h>
# include <fcntl.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/time.h>
# include <sys/socket.h>
# include <netdb.h>
# include <signal.h>
# include <arpa/inet.h>
# include <netinet/tcp.h>
# include <netinet/in.h>

#elif defined(KUMA_OS_MACOS)
# include <string.h>
# include <pthread.h>
# include <unistd.h>

# include <sys/types.h>
# include <sys/socket.h>
# include <sys/ioctl.h>
# include <sys/fcntl.h>
# include <sys/time.h>
# include <sys/uio.h>
# include <netinet/tcp.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <ifaddrs.h>

#else
# error "UNSUPPORTED OS"
#endif

#include <stdarg.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef KUMA_HAS_CXX0X
# include <functional>
# define FUNCTION    std::function
# define BIND        std::bind
#else
# include <boost/function.hpp>
# include <boost/bind.hpp>
# define FUNCTION    boost::function
# define BIND        boost::bind
#endif

#include <map>
#include <list>

#ifndef KUMA_OS_WIN
#define closesocket close
#endif

KUMA_NS_BEGIN

#define INVALID_FD  -1

class IOHandler
{
public:
    virtual ~IOHandler() {}
    
    virtual int acquireRef() = 0;
    virtual int releaseRef() = 0;
    virtual int onEvent(uint32_t ev) = 0;
};

typedef std::map<int, IOHandler*>   IOHandlerMap;

class IOPoll
{
public:
    virtual ~IOPoll() {}
    
    virtual bool init() = 0;
    virtual int registerFD(int fd, uint32_t events, IOHandler* handler) = 0;
    virtual int unregisterFD(int fd) = 0;
    virtual int modifyEvents(int fd, uint32_t events, IOHandler* handler) = 0;
    virtual int wait(uint32_t wait_time_ms) = 0;
    virtual void notify() = 0;
};

class IEvent
{
public:
    virtual ~IEvent() {}

    virtual void fire() = 0;
};

KUMA_NS_END

#endif
