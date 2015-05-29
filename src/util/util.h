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

#ifndef __kuma_util_h__
#define __kuma_util_h__

#include "kmdefs.h"

struct addrinfo;
struct sockaddr;

KUMA_NS_BEGIN

#ifdef KUMA_OS_WIN
#define snprintf    _snprintf
#define vsnprintf   _vsnprintf
#endif

#ifndef TICK_COUNT_TYPE
# define TICK_COUNT_TYPE	unsigned long
#endif

int set_nonblocking(int fd);
int find_first_set(unsigned int b);
TICK_COUNT_TYPE get_tick_count_ms();
TICK_COUNT_TYPE calc_time_elapse_delta_ms(TICK_COUNT_TYPE now_tick, TICK_COUNT_TYPE& start_tick);

extern "C" {
    KUMA_API int km_resolve_2_ip(const char* host_name, char *ip_buf, int ip_buf_len, int ipv = 0);
    KUMA_API int km_parse_address(const char* addr,
                         char* proto, int proto_len,
                         char* host, int  host_len, unsigned short* port);
    KUMA_API int km_set_sock_addr(const char* addr, unsigned short port,
                         addrinfo* hints, sockaddr * sk_addr,
                         unsigned int sk_addr_len);
    KUMA_API int km_get_sock_addr(sockaddr * sk_addr, unsigned int sk_addr_len,
                         char* addr, unsigned int addr_len, unsigned short* port);
    KUMA_API bool km_is_ipv6_address(const char* addr);
    KUMA_API bool km_is_ip_address(const char* addr);
}

KUMA_NS_END

#endif
