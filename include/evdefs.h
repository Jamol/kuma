/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __KUMAEVDEFS_H__
#define __KUMAEVDEFS_H__

#include "kmdefs.h"
#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <sys/socket.h>
#endif
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

using KMEvent = uint32_t;
using IOCallback = std::function<void(KMEvent, void*, size_t)>;

enum class PollType {
    NONE,
    POLL,
    EPOLL,
    KQUEUE,
    SELECT,
    IOCP,
    WIN
};

KUMA_NS_END

#endif
