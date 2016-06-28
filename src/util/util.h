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
#include <string>

struct addrinfo;
struct sockaddr;

KUMA_NS_BEGIN

#ifdef KUMA_OS_WIN
# define snprintf    _snprintf
# define vsnprintf   _vsnprintf
# define strcasecmp _stricmp
# define strncasecmp _strnicmp
# define getCurrentThreadId() GetCurrentThreadId()
#elif defined(KUMA_OS_MAC)
# define getCurrentThreadId() pthread_mach_thread_np(pthread_self())
#else
# define getCurrentThreadId() pthread_self()
#endif

#ifdef KUMA_OS_WIN
# define PATH_SEPARATOR '\\'
#else
# define PATH_SEPARATOR '/'
#endif

#ifndef TICK_COUNT_TYPE
# define TICK_COUNT_TYPE	unsigned long
#endif

int set_nonblocking(int fd);
int find_first_set(unsigned int b);
TICK_COUNT_TYPE get_tick_count_ms();
TICK_COUNT_TYPE calc_time_elapse_delta_ms(TICK_COUNT_TYPE now_tick, TICK_COUNT_TYPE& start_tick);

bool is_equal(const char* str1, const char* str2);
bool is_equal(const std::string& str1, const std::string& str2);
bool is_equal(const char* str1, const std::string& str2);
bool is_equal(const std::string& str1, const char* str2);
bool is_equal(const char* str1, const char* str2, int n);
bool is_equal(const std::string& str1, const std::string& str2, int n);
bool is_equal(const char* str1, const std::string& str2, int n);
bool is_equal(const std::string& str1, const char* str2, int n);
char* trim_left(char* str);
char* trim_right(char* str);
char* trim_right(char* str, char* str_end);
std::string& trim_left(std::string& str);
std::string& trim_right(std::string& str);
std::string getExecutablePath();
std::string getCurrentModulePath();

//#define decode_u32(buf) ((buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3])
//#define decode_u24(buf) ((buf[0] << 16) | (buf[1] << 8) | buf[2])
//#define decode_u16(buf) ((buf[0] << 8) | buf[1])
//#define encode_u32(buf, u) buf[0] = uint8_t(u >> 24), buf[1] = uint8_t(u >> 16), buf[2] = uint8_t(u >> 8), buf[3] = uint8_t(u)
//#define encode_u24(buf, u) buf[0] = uint8_t(u >> 16), buf[1] = uint8_t(u >> 8), buf[2] = uint8_t(u)
//#define encode_u16(buf, u) buf[0] = uint8_t(u >> 8), buf[1] = uint8_t(u)

inline uint32_t decode_u32(const uint8_t *buf) {
    return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

inline uint32_t decode_u24(const uint8_t *buf) {
    return (buf[0] << 16) | (buf[1] << 8) | buf[2];
}

inline uint16_t decode_u16(const uint8_t *buf) {
    return (buf[0] << 8) | buf[1];
}

inline void encode_u32(uint32_t u, uint8_t *buf) {
    buf[0] = u >> 24, buf[1] = u >> 16, buf[2] = u >> 8, buf[3] = u;
}

inline void encode_u24(uint32_t u, uint8_t *buf) {
    buf[0] = u >> 16, buf[1] = u >> 8, buf[2] = u;
}

inline void encode_u16(uint32_t u, uint8_t *buf) {
    buf[0] = u >> 8, buf[1] = u;
}

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
    KUMA_API bool km_is_mcast_address(const char* addr);
}

KUMA_NS_END

#endif
