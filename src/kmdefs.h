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

typedef enum{
    KUMA_ERROR_NOERR    = 0,
    KUMA_ERROR_FAILED,
    KUMA_ERROR_INVALID_STATE,
    KUMA_ERROR_INVALID_PARAM,
    KUMA_ERROR_INVALID_PROTO,
    KUMA_ERROR_ALREADY_EXIST,
    KUMA_ERROR_AGAIN,
    KUMA_ERROR_SOCKERR,
    KUMA_ERROR_POLLERR,
    KUMA_ERROR_SSL_FAILED,
    KUMA_ERROR_BUFFER_TOO_SMALL,
    KUMA_ERROR_UNSUPPORT,
    KUMA_ERROR_DESTROYED
}KMError;

typedef enum {
    HTTP_HEADER_COMPLETE,
    HTTP_COMPLETE,
    HTTP_ERROR
}HttpEvent;

typedef enum {
    SSL_NONE                    = 0,
    SSL_ENABLE                  = 1,
    SSL_ALLOW_EXPIRED_CERT      = 0X02,
    SSL_ALLOW_INVALID_CERT_CN   = 0X04,
    SSL_ALLOW_EXPIRED_ROOT      = 0X08,
    SSL_ALLOW_ANY_ROOT          = 0x10,
    SSL_ALLOW_REVOKED_CERT      = 0X20
}SslFlag;

typedef enum {
    AS_CLIENT,
    AS_SERVER
}SslRole;

typedef enum {
    ONE_SHOT,
    REPEATING
}TimerMode;

#define UDP_FLAG_MULTICAST  1

#ifdef KUMA_OS_WIN
struct iovec {
    unsigned long   iov_len;
    char*           iov_base;
};
#endif

KUMA_NS_END

#endif
