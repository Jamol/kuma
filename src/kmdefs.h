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

#ifndef __KUMADEFS_H__
#define __KUMADEFS_H__

#include "kmconf.h"

#ifdef KUMA_OS_MAC
# define KUMA_NS_BEGIN   namespace kuma {;
#else
# define KUMA_NS_BEGIN   namespace kuma {
#endif
#define KUMA_NS_END     }

KUMA_NS_BEGIN

#ifdef KUMA_OS_WIN
# ifdef KUMA_EXPORTS
#  define KUMA_API __declspec(dllexport)
# else
#  define KUMA_API __declspec(dllimport)
# endif
#else
# define KUMA_API
#endif

enum class KMError : int {
    NOERR               = 0,
    FAILED              = -1,
    INVALID_STATE       = -2,
    INVALID_PARAM       = -3,
    INVALID_PROTO       = -4,
    ALREADY_EXIST       = -5,
    NOT_EXIST           = -6,
    AGAIN               = -7,
    TIMEOUT             = -8,
    SOCK_ERROR          = -9,
    POLL_ERROR          = -10,
    PROTO_ERROR         = -11,
    SSL_FAILED          = -12,
    BUFFER_TOO_SMALL    = -13,
    UNSUPPORT           = -14,
    DESTROYED           = -15
};

enum class HttpEvent : int {
    HEADER_COMPLETE,
    COMPLETE,
    HTTP_ERROR
};

typedef enum {
    SSL_NONE                    = 0,
    SSL_ENABLE                  = 0x01,
    SSL_ALLOW_EXPIRED_CERT      = 0x02,
    SSL_ALLOW_INVALID_CERT_CN   = 0x04,
    SSL_ALLOW_UNTRUSTED_CERT    = 0x08,
    SSL_ALLOW_EXPIRED_ROOT      = 0x10,
    SSL_ALLOW_ANY_ROOT          = 0x20,
    SSL_ALLOW_REVOKED_CERT      = 0x40,
    SSL_ALLOW_SELF_SIGNED_CERT  = 0x80,
    SSL_VERIFY_HOST_NAME        = 0x1000
}SslFlag;

enum class SslRole {
    CLIENT,
    SERVER
};

enum class TimerMode {
    ONE_SHOT,
    REPEATING
};

#define UDP_FLAG_MULTICAST  1

#ifdef KUMA_OS_WIN
struct iovec {
    unsigned long   iov_len;
    char*           iov_base;
};
#endif

KUMA_NS_END

#endif
