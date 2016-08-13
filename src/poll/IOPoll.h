/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

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
    SOCKET_FD fd = INVALID_FD;
    int idx = -1;
    IOCallback cb;
};
typedef std::vector<PollItem>   PollItemVector;

class IOPoll
{
public:
    virtual ~IOPoll() {}
    
    virtual bool init() = 0;
    virtual KMError registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb) = 0;
    virtual KMError unregisterFd(SOCKET_FD fd) = 0;
    virtual KMError updateFd(SOCKET_FD fd, uint32_t events) = 0;
    virtual KMError wait(uint32_t wait_time_ms) = 0;
    virtual void notify() = 0;
    virtual PollType getType() const = 0;
    virtual bool isLevelTriggered() const = 0;
    
protected:
    void resizePollItems(SOCKET_FD fd) {
        auto count = poll_items_.size();
        if (fd >= count) {
            if(fd > count + 1024) {
                poll_items_.resize(fd+1);
            } else {
                poll_items_.resize(count + 1024);
            }
        }
    }
    PollItemVector  poll_items_;
};

KUMA_NS_END

#endif
