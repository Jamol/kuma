/* Copyright (c) 2014-2022, Fengping Bao <jamol@live.com>
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

#ifndef __KEVDEFS_H__
#define __KEVDEFS_H__

#include "kmconf.h"

#include <functional>

#define KEV_NS_BEGIN   namespace kev {
#define KEV_NS_END     }
#define KEV_NS_USING   using namespace kev;

KEV_NS_BEGIN

#ifdef KUMA_OS_WIN
using SOCKET_FD = uintptr_t;
const SOCKET_FD INVALID_FD = (SOCKET_FD)~0;
#else
using SOCKET_FD = int;
const SOCKET_FD INVALID_FD = ((SOCKET_FD)-1);
#endif

using KMEvent = uint32_t;
using IOCallback = std::function<void(SOCKET_FD, KMEvent, void*, size_t)>;

const uint32_t kEventRead       = 1;
const uint32_t kEventWrite      = (1 << 1);
const uint32_t kEventError      = (1 << 2);
const uint32_t kEventNetwork    = (kEventRead | kEventWrite | kEventError);


enum class Result : int {
    OK                  = 0,
    FAILED              = -1,
    FATAL               = -2,
    REJECTED            = -3,
    CLOSED              = -4,
    AGAIN               = -5,
    ABORTED             = -6,
    TIMEOUT             = -7,
    INVALID_STATE       = -8,
    INVALID_PARAM       = -9,
    INVALID_PROTO       = -10,
    ALREADY_EXIST       = -11,
    NOT_EXIST           = -12,
    SOCK_ERROR          = -13,
    POLL_ERROR          = -14,
    PROTO_ERROR         = -15,
    SSL_ERROR           = -16,
    BUFFER_TOO_SMALL    = -17,
    BUFFER_TOO_LONG     = -18,
    NOT_SUPPORTED       = -19,
    NOT_IMPLEMENTED     = -20,
    NOT_AUTHORIZED      = -21,
    
    DESTROYED           = -699
};

enum class PollType {
    DEFAULT,
    SELECT,
    POLL,
    EPOLL,
    KQUEUE,
    IOCP,
    STLCV, // none IO event loop
};


#ifdef KUMA_OS_WIN
struct iovec {
    unsigned long   iov_len;
    char*           iov_base;
};
#endif

KEV_NS_END

#endif
