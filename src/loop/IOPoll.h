#ifndef __IOPoll_H__
#define __IOPoll_H__

#include "kmdefs.h"
#include "evdefs.h"

#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
# include <windows.h>
# include <time.h>
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
#include <vector>

KUMA_NS_BEGIN

struct PollItem
{
    PollItem() : fd(INVALID_FD), idx(-1) { }

    SOCKET_FD fd;
    int idx;
    IOCallback cb;
};
typedef std::vector<PollItem>   PollItemVector;

class IOPoll
{
public:
    virtual ~IOPoll() {}
    
    virtual bool init() = 0;
    virtual int registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb) = 0;
    virtual int registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb) = 0;
    virtual int unregisterFd(SOCKET_FD fd) = 0;
    virtual int updateFd(SOCKET_FD fd, uint32_t events) = 0;
    virtual int wait(uint32_t wait_time_ms) = 0;
    virtual void notify() = 0;
    virtual PollType getType() = 0;
};

KUMA_NS_END

#endif
