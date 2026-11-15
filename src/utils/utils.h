#ifndef __kuma_utils_h__
#define __kuma_utils_h__

#include "kmdefs.h"
#include "libkev/src/utils/utils.h"

#include <string>
#include <limits>

KUMA_NS_BEGIN

inline bool to_int64(const char *str, size_t len, int64_t &out)
{
    if (!str || len == 0) {
        return false;
    }
    bool negative = false;
    size_t pos = 0;
    if (str[0] == '+' || str[0] == '-') {
        negative = (str[0] == '-');
        pos = 1;
        if (len == 1) {
            return false;
        }
    }
    const int64_t min_val = std::numeric_limits<int64_t>::min();
    int64_t val = 0;
    for (; pos < len; ++pos) {
        unsigned char c = static_cast<unsigned char>(str[pos]);
        if (c < '0' || c > '9') {
            return false;
        }
        int64_t digit = c - '0';
        if (val < (min_val + digit) / 10) {
            return false;
        }
        val = val * 10 - digit;
    }
    if (!negative) {
        if (val == min_val) {
            return false;
        }
        val = -val;
    }
    out = val;
    return true;
}

inline bool to_uint64(const char *str, size_t len, uint64_t &out)
{
    if (!str || len == 0) {
        return false;
    }
    const uint64_t max_val = std::numeric_limits<uint64_t>::max();
    uint64_t val = 0;
    for (size_t pos = 0; pos < len; ++pos) {
        unsigned char c = static_cast<unsigned char>(str[pos]);
        if (c < '0' || c > '9') {
            return false;
        }
        uint64_t digit = c - '0';
        if (val > (max_val - digit) / 10) {
            return false;
        }
        val = val * 10 + digit;
    }
    out = val;
    return true;
}

inline bool to_int64(const std::string &str, int64_t &out)
{
    return to_int64(str.data(), str.size(), out);
}

inline bool to_uint64(const std::string &str, uint64_t &out)
{
    return to_uint64(str.data(), str.size(), out);
}

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
    dst[0] = u >> 24;
    dst[1] = u >> 16;
    dst[2] = u >> 8;
    dst[3] = u;
}

inline void encode_u24(uint8_t *dst, uint32_t u)
{
    dst[0] = u >> 16;
    dst[1] = u >> 8;
    dst[2] = u;
}

inline void encode_u16(uint8_t *dst, uint32_t u)
{
    dst[0] = u >> 8;
    dst[1] = u;
}

inline bool is_fatal_error(KMError err)
{
    return err != KMError::NOERR && err != KMError::AGAIN;
}

KMError toKMError(kev::Result result);

KUMA_NS_END


extern "C" {
    KUMA_API int km_resolve_2_ip(const char *host_name, char *ip_buf, size_t ip_buf_len, int ipv = 0);
    KUMA_API int km_parse_address(const char *addr,
                                  char *proto, 
                                  size_t proto_len,
                                  char *host, 
                                  size_t host_len, 
                                  unsigned short *port);
    KUMA_API int km_set_sock_addr(const char *addr, 
                                  unsigned short port,
                                  addrinfo *hints, 
                                  sockaddr *sk_addr,
                                  size_t sk_addr_len);
    KUMA_API int km_get_sock_addr(const sockaddr *sk_addr, 
                                  size_t sk_addr_len,
                                  char *addr, 
                                  size_t addr_len, 
                                  unsigned short *port);
    KUMA_API bool km_is_ipv6_address(const char *addr);
    KUMA_API bool km_is_ip_address(const char *addr);
    KUMA_API bool km_is_mcast_address(const char *addr);
}


#endif
