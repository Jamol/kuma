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

#include "util.h"

KUMA_NS_BEGIN

KMError toKMError(kev::Result result)
{
    switch (result) {
        case kev::Result::OK:
            return KMError::NOERR;
        case kev::Result::FAILED:
            return KMError::FAILED;
        case kev::Result::FATAL:
            return KMError::FATAL;
        case kev::Result::REJECTED:
            return KMError::REJECTED;
        case kev::Result::CLOSED:
            return KMError::CLOSED;
        case kev::Result::AGAIN:
            return KMError::AGAIN;
        case kev::Result::TIMEOUT:
            return KMError::TIMEOUT;
        case kev::Result::INVALID_STATE:
            return KMError::INVALID_STATE;
        case kev::Result::INVALID_PARAM:
            return KMError::INVALID_PARAM;
        case kev::Result::INVALID_PROTO:
            return KMError::INVALID_PROTO;
        case kev::Result::ALREADY_EXIST:
            return KMError::ALREADY_EXIST;
        case kev::Result::NOT_EXIST:
            return KMError::NOT_EXIST;
        case kev::Result::SOCK_ERROR:
            return KMError::SOCK_ERROR;
        case kev::Result::POLL_ERROR:
            return KMError::POLL_ERROR;
        case kev::Result::PROTO_ERROR:
            return KMError::PROTO_ERROR;
        case kev::Result::SSL_ERROR:
            return KMError::SSL_ERROR;
        case kev::Result::BUFFER_TOO_SMALL:
            return KMError::BUFFER_TOO_SMALL;
        case kev::Result::BUFFER_TOO_LONG:
            return KMError::BUFFER_TOO_LONG;
        case kev::Result::NOT_SUPPORTED:
            return KMError::NOT_SUPPORTED;
        case kev::Result::NOT_IMPLEMENTED:
            return KMError::NOT_IMPLEMENTED;
        case kev::Result::NOT_AUTHORIZED:
            return KMError::NOT_AUTHORIZED;
        case kev::Result::DESTROYED:
            return KMError::DESTROYED;
            
        default:
            return KMError::FAILED;
    }
}

KUMA_NS_END


extern "C" int km_resolve_2_ip(const char *host_name, char *ip_buf, size_t ip_buf_len, int ipv)
{
    return kev::km_resolve_2_ip(host_name, ip_buf, ip_buf_len, ipv);
}

extern "C" int km_set_sock_addr(const char *addr, 
                                unsigned short port,
                                addrinfo *hints, 
                                sockaddr *sk_addr,
                                size_t sk_addr_len)
{
    return kev::km_set_sock_addr(addr, port, hints, sk_addr, sk_addr_len);
}

extern "C" int km_get_sock_addr(const sockaddr *sk_addr, 
                                size_t sk_addr_len,
                                char *addr, 
                                size_t addr_len, 
                                unsigned short *port)
{
    return kev::km_get_sock_addr(sk_addr, sk_addr_len, addr, addr_len, port);
}

extern "C" bool km_is_ipv6_address(const char *addr)
{
    return kev::km_is_ipv6_address(addr);
}

extern "C" bool km_is_ip_address(const char *addr)
{
    return kev::km_is_ip_address(addr);
}

extern "C" bool km_is_mcast_address(const char *addr)
{
    return kev::km_is_mcast_address(addr);
}

extern "C" int km_parse_address(const char *addr,
                                char *proto, 
                                size_t proto_len,
                                char *host, 
                                size_t  host_len, 
                                unsigned short *port)
{
    return kev::km_parse_address(addr, proto, proto_len, host, host_len, port);
}
