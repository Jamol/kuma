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

#ifndef __kuma_util_h__
#define __kuma_util_h__

#include "kmdefs.h"
#include "evdefs.h" // for SOCKET_FD
#include <string>

struct addrinfo;
struct sockaddr;

KUMA_NS_BEGIN

#ifdef KUMA_OS_WIN
# define snprintf       _snprintf
# define vsnprintf      _vsnprintf
# define strcasecmp     _stricmp
# define strncasecmp    _strnicmp
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
# define strncpy_s(d, dl, s, c) strlcpy(d, s, dl)
#endif

#ifndef TICK_COUNT_TYPE
# define TICK_COUNT_TYPE	uint64_t
#endif

#define UNUSED(x) (void)(x)

template <typename T, size_t N>
char(&ArraySizeHelper(const T(&array)[N]))[N];
#define ARRAY_SIZE(array) (sizeof(ArraySizeHelper(array)))

int set_nonblocking(SOCKET_FD fd);
int set_tcpnodelay(SOCKET_FD fd);
int find_first_set(uint32_t b);
int find_first_set(uint64_t b);
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
bool contains_token(const std::string& str, const std::string& token, char delim);
std::string getExecutablePath();
std::string getCurrentModulePath();

inline uint32_t decode_u32(const uint8_t *src)
{
    return (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
}

inline uint32_t decode_u24(const uint8_t *src)
{
    return (src[0] << 16) | (src[1] << 8) | src[2];
}

inline uint16_t decode_u16(const uint8_t *src)
{
    return (src[0] << 8) | src[1];
}

inline void encode_u32(uint8_t *dst, uint32_t u)
{
    dst[0] = u >> 24, dst[1] = u >> 16, dst[2] = u >> 8, dst[3] = u;
}

inline void encode_u24(uint8_t *dst, uint32_t u)
{
    dst[0] = u >> 16, dst[1] = u >> 8, dst[2] = u;
}

inline void encode_u16(uint8_t *dst, uint32_t u)
{
    dst[0] = u >> 8, dst[1] = u;
}

int generateRandomBytes(uint8_t *buf, int len);

extern "C" {
    KUMA_API int km_resolve_2_ip(const char* host_name, char *ip_buf, int ip_buf_len, int ipv = 0);
    KUMA_API int km_parse_address(const char* addr,
                         char* proto, int proto_len,
                         char* host, int  host_len, unsigned short* port);
    KUMA_API int km_set_sock_addr(const char* addr, unsigned short port,
                         addrinfo* hints, sockaddr * sk_addr,
                         unsigned int sk_addr_len);
    KUMA_API int km_get_sock_addr(const sockaddr * sk_addr, unsigned int sk_addr_len,
                         char* addr, unsigned int addr_len, unsigned short* port);
    KUMA_API bool km_is_ipv6_address(const char* addr);
    KUMA_API bool km_is_ip_address(const char* addr);
    KUMA_API bool km_is_mcast_address(const char* addr);
    
#ifndef KUMA_OS_MAC
    size_t strlcpy(char *, const char *, size_t);
    size_t strlcat(char *, const char *, size_t);
#endif
}

int km_get_sock_addr(const sockaddr *addr, size_t addr_len, std::string &ip, uint16_t *port);
int km_get_sock_addr(const sockaddr_storage &addr, std::string &ip, uint16_t *port);
int km_set_addr_port(uint16_t port, sockaddr_storage &addr);
int km_get_addr_length(const sockaddr_storage &addr);

inline bool km_is_fatal_error(KMError err)
{
    return err != KMError::NOERR && err != KMError::AGAIN;
}

KUMA_NS_END

#endif
