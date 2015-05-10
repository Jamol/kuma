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

#elif defined(KUMA_OS_MAC)
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
#include <stdint.h>

#include <map>
#include <list>

#ifndef KUMA_OS_WIN
#define closesocket close
#endif

KUMA_NS_BEGIN

#define INVALID_FD  -1

class IOPoll
{
public:
    virtual ~IOPoll() {}
    
    virtual bool init() = 0;
    virtual int register_fd(int fd, uint32_t events, IOCallback& cb) = 0;
    virtual int register_fd(int fd, uint32_t events, IOCallback&& cb) = 0;
    virtual int unregister_fd(int fd) = 0;
    virtual int modify_events(int fd, uint32_t events) = 0;
    virtual int wait(uint32_t wait_time_ms) = 0;
    virtual void notify() = 0;
};

KUMA_NS_END

#endif
