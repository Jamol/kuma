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

#ifndef __KUMAEVDEFS_H__
#define __KUMAEVDEFS_H__

#include "kmdefs.h"

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

typedef enum {
    POLL_TYPE_NONE,
    POLL_TYPE_POLL,
    POLL_TYPE_EPOLL,
    POLL_TYPE_SELECT,
    POLL_TYPE_WIN
}PollType;

KUMA_NS_END

#endif
