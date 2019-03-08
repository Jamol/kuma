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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef KUMA_OS_WIN
# include <MSWSock.h>
# include <Ws2tcpip.h>
# include <windows.h>
#else
# include <string.h>
# include <netdb.h>
# include <arpa/inet.h>
# include <fcntl.h>
# include <errno.h>
# include <sys/types.h>
# include <sys/time.h>
# include <dlfcn.h>
# include <unistd.h>
# include <netinet/tcp.h>
# ifdef KUMA_OS_MAC
#  include "CoreFoundation/CoreFoundation.h"
#  include <mach-o/dyld.h>
#  ifndef KUMA_OS_IOS
#   include <libproc.h>
#  endif
# endif
#endif

#include <chrono>
#include <random>
#include <string>
#include <sstream>
#include <algorithm>
#include <limits.h>

#include "kmobject.h"
#include "kmtrace.h"

KUMA_NS_BEGIN

std::atomic<long> KMObject::objIdSeed_{0};

#ifdef KUMA_OS_WIN
#define STRNCPY_S   strncpy_s
#define SNPRINTF(d, dl, fmt, ...)    _snprintf_s(d, dl, _TRUNCATE, fmt, ##__VA_ARGS__)
#else
#define STRNCPY_S(d, dl, s, sl) \
do{ \
    if(0 == dl) \
        break; \
    strncpy(d, s, dl-1); \
    d[dl-1]='\0'; \
}while(0);
#define SNPRINTF    snprintf
#endif

enum{
    KM_RESOLVE_IPV0    = 0,
    KM_RESOLVE_IPV4    = 1,
    KM_RESOLVE_IPV6    = 2
};

#ifdef KUMA_OS_WIN
typedef int (WSAAPI *pf_getaddrinfo)(
    _In_opt_  PCSTR pNodeName,
    _In_opt_  PCSTR pServiceName,
    _In_opt_  const addrinfo *pHints,
    _Out_     addrinfo **ppResult
);

typedef int (WSAAPI *pf_getnameinfo)(
    __in   const struct sockaddr FAR *sa,
    __in   socklen_t salen,
    __out  char FAR *host,
    __in   DWORD hostlen,
    __out  char FAR *serv,
    __in   DWORD servlen,
    __in   int flags
);

typedef void (WSAAPI *pf_freeaddrinfo)(
    __in  struct addrinfo *ai
);

static HMODULE s_hmod_ws2_32 = NULL;
static bool s_ipv6_support = false;
static bool s_ipv6_inilized = false;
pf_getaddrinfo km_getaddrinfo = nullptr;
pf_getnameinfo km_getnameinfo = nullptr;
pf_freeaddrinfo km_freeaddrinfo = nullptr;
#else
# define km_getaddrinfo getaddrinfo
# define km_getnameinfo getnameinfo
# define km_freeaddrinfo freeaddrinfo
#endif

bool ipv6_api_init()
{
#ifdef KUMA_OS_WIN
    if(!s_ipv6_inilized)
    {
        s_ipv6_inilized = true;
        
#define GET_WS2_FUNCTION(f) km_##f = (pf_##f)GetProcAddress(s_hmod_ws2_32, #f);\
if(NULL == km_##f)\
{\
FreeLibrary(s_hmod_ws2_32);\
s_hmod_ws2_32 = NULL;\
break;\
}
        
        s_hmod_ws2_32 = LoadLibrary("Ws2_32.dll");
        do
        {
            if(NULL == s_hmod_ws2_32)
                break;
            GET_WS2_FUNCTION(getaddrinfo);
            GET_WS2_FUNCTION(getnameinfo);
            GET_WS2_FUNCTION(freeaddrinfo);
            s_ipv6_support = true;
        } while (0);
    }
    
    return s_ipv6_support;
#else
    return true;
#endif
}

#if 0
int km_resolve_2_ip_v4(const char* host_name, char *ip_buf, int ip_buf_len)
{
    const char* ptr = host_name;
    bool is_digit = true;
    while(*ptr) {
        if(*ptr != ' ' &&  *ptr != '\t') {
            if(*ptr != '.' && !(*ptr >= '0' && *ptr <= '9')) {
                is_digit = false;
                break;
            }
        }
        ++ptr;
    }
    
    if (is_digit) {
        STRNCPY_S(ip_buf, ip_buf_len, host_name, strlen(host_name));
        return 0;
    }
    
    struct hostent* he = nullptr;
#ifdef KUMA_OS_LINUX
    int nError = 0;
    char szBuffer[1024] = {0};
    struct hostent *pheResultBuf = reinterpret_cast<struct hostent *>(szBuffer);
    
    if (::gethostbyname_r(
                          host_name,
                          pheResultBuf,
                          szBuffer + sizeof(struct hostent),
                          sizeof(szBuffer) - sizeof(struct hostent),
                          &pheResultBuf,
                          &nError) == 0)
    {
        he = pheResultBuf;
    }
#else
    he = gethostbyname(host_name);
#endif
    
    if(he && he->h_addr_list && he->h_addr_list[0])
    {
#ifndef KUMA_OS_WIN
        inet_ntop(AF_INET, he->h_addr_list[0], ip_buf, ip_buf_len);
        return 0;
#else
        char* tmp = (char*)inet_ntoa((in_addr&)(*he->h_addr_list[0]));
        if(tmp) {
            STRNCPY_S(ip_buf, ip_buf_len, tmp, strlen(tmp));
            return 0;
        }
#endif
    }
    
    ip_buf[0] = 0;
    return -1;
}
#endif

extern "C" int km_resolve_2_ip(const char* host_name, char *ip_buf, int ip_buf_len, int ipv)
{
    if(!host_name || !ip_buf) {
        return -1;
    }
    
    ip_buf[0] = '\0';
#ifdef KUMA_OS_WIN
    if(!ipv6_api_init()) {
        //if(KM_RESOLVE_IPV6 == ipv)
            return -1;
        //return km_resolve_2_ip_v4(host_name, ip_buf, ip_buf_len);
    }
#endif
    
    addrinfo* ai = nullptr;
    struct addrinfo hints = {0};
    if (KM_RESOLVE_IPV6 == ipv) {
        hints.ai_family = AF_INET6;
    } else if (KM_RESOLVE_IPV4 == ipv) {
        hints.ai_family = AF_INET;
    } else {
        hints.ai_family = AF_UNSPEC;
    }
    hints.ai_flags = AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    if(km_getaddrinfo(host_name, nullptr, &hints, &ai) != 0 || !ai) {
        return -1;
    }
    
	for (addrinfo *aii = ai; aii; aii = aii->ai_next)
    {
        if(AF_INET6 == aii->ai_family && (KM_RESOLVE_IPV6 == ipv || KM_RESOLVE_IPV0 == ipv))
        {
            sockaddr_in6 *sa6 = (sockaddr_in6*)aii->ai_addr;
            if(IN6_IS_ADDR_LINKLOCAL(&(sa6->sin6_addr)))
                continue;
            if(IN6_IS_ADDR_SITELOCAL(&(sa6->sin6_addr)))
                continue;
            if(km_getnameinfo(aii->ai_addr, aii->ai_addrlen, ip_buf, ip_buf_len, NULL, 0, NI_NUMERICHOST|NI_NUMERICSERV) != 0)
                continue;
            else
                break; // found a ipv6 address
        }
        else if(AF_INET == aii->ai_family && (KM_RESOLVE_IPV4 == ipv || KM_RESOLVE_IPV0 == ipv))
        {
            if(km_getnameinfo(aii->ai_addr, aii->ai_addrlen, ip_buf, ip_buf_len, NULL, 0, NI_NUMERICHOST|NI_NUMERICSERV) != 0)
                continue;
            else
                break; // found a ipv4 address
        }
    }
    if('\0' == ip_buf[0] && KM_RESOLVE_IPV0 == ipv &&
       km_getnameinfo(ai->ai_addr, ai->ai_addrlen, ip_buf, ip_buf_len, NULL, 0, NI_NUMERICHOST|NI_NUMERICSERV) != 0)
    {
        km_freeaddrinfo(ai);
        return -1;
    }
    km_freeaddrinfo(ai);
    return 0;
}

extern "C" int km_set_sock_addr(const char* addr, unsigned short port,
                                struct addrinfo* hints, struct sockaddr * sk_addr,
                                unsigned int sk_addr_len)
{
#ifdef KUMA_OS_WIN
    if(!ipv6_api_init()) {
        struct sockaddr_in *sa = (struct sockaddr_in*)sk_addr;
        sa->sin_family = AF_INET;
        sa->sin_port = htons(port);
        if(!addr || addr[0] == '\0') {
            sa->sin_addr.s_addr = INADDR_ANY;
            return 0;
        } else {
            auto ret = inet_pton(sa->sin_family, addr, &sa->sin_addr);
            if (ret == 1) {
                return 0;
            } else if (ret == 0) {
                return EAI_NONAME;
            } else {
                return -1;
            }
        }
    }
#endif
    char service[128] = {0};
    struct addrinfo* ai = nullptr;
    if(!addr && hints) {
        hints->ai_flags |= AI_PASSIVE;
    }
    SNPRINTF(service, sizeof(service)-1, "%d", port);
    auto ret = km_getaddrinfo(addr, service, hints, &ai);
    if(ret != 0 || !ai) {
        if(ai) km_freeaddrinfo(ai);
        return ret;
    }
    if (ai->ai_addrlen > sk_addr_len) {
        if(ai) km_freeaddrinfo(ai);
        return -1;
    }
    if(sk_addr) {
        memcpy(sk_addr, ai->ai_addr, ai->ai_addrlen);
    }
    km_freeaddrinfo(ai);
    return 0;
}

extern "C" int km_get_sock_addr(const sockaddr *sk_addr, unsigned int sk_addr_len,
                                char *addr, unsigned int addr_len, unsigned short *port)
{
#ifdef KUMA_OS_WIN
    if(!ipv6_api_init()) {
        struct sockaddr_in *sa = (struct sockaddr_in*)sk_addr;
        inet_ntop(sa->sin_family, &sa->sin_addr, addr, addr_len);
        if(port)
            *port = ntohs(sa->sin_port);
        return 0;
    }
#endif
    
    char service[16] = {0};
    if(km_getnameinfo(sk_addr, sk_addr_len, addr, addr_len, service, sizeof(service), NI_NUMERICHOST|NI_NUMERICSERV) != 0)
        return -1;
    if(port)
        *port = atoi(service);
    return 0;
}

int km_get_sock_addr(const sockaddr *addr, size_t addr_len, std::string &ip, uint16_t *port)
{
    char ip_buf[128] = {0};
    if (km_get_sock_addr(addr, (unsigned int)addr_len, ip_buf, sizeof(ip_buf), port) != 0) {
        return -1;
    }
    ip = ip_buf;
    return 0;
}

int km_get_sock_addr(const sockaddr_storage &addr, std::string &ip, uint16_t *port)
{
    char ip_buf[128] = { 0 };
    int addr_len = km_get_addr_length(addr);
    if (km_get_sock_addr((const sockaddr *)&addr, addr_len, ip_buf, sizeof(ip_buf), port) != 0) {
        return -1;
    }
    ip = ip_buf;
    return 0;
}

int km_set_addr_port(uint16_t port, sockaddr_storage &addr)
{
    if (AF_INET == addr.ss_family) {
        sockaddr_in *p = (sockaddr_in*)&addr;
        p->sin_port = htons(port);
    } else if (AF_INET6 == addr.ss_family) {
        sockaddr_in6 *p = (sockaddr_in6*)&addr;
        p->sin6_port = htons(port);
    } else {
        return -1;
    }
    return 0;
}

int km_get_addr_length(const sockaddr_storage &addr)
{
    int addr_len = sizeof(addr);
    if (AF_INET == addr.ss_family) {
        addr_len = sizeof(sockaddr_in);
    }
    else if (AF_INET6 == addr.ss_family) {
        addr_len = sizeof(sockaddr_in6);
    }
    return addr_len;
}

extern "C" bool km_is_ipv6_address(const char* addr)
{
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST;
    if(km_set_sock_addr(addr, 0, &hints, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) != 0) {
        return false;
    }
    return AF_INET6==ss_addr.ss_family;
}

extern "C" bool km_is_ip_address(const char* addr)
{
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST;
    return km_set_sock_addr(addr, 0, &hints, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) == 0;
}

extern "C" bool km_is_mcast_address(const char* addr)
{
    sockaddr_storage ss_addr = {0};
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST|AI_ADDRCONFIG; // will block 10 seconds in some case if not set AI_ADDRCONFIG
    km_set_sock_addr(addr, 0, &hints, (struct sockaddr*)&ss_addr, sizeof(ss_addr));
    switch(ss_addr.ss_family) {
        case AF_INET: {
            struct sockaddr_in *sa_in4=(struct sockaddr_in *)&ss_addr;
            return IN_MULTICAST(ntohl(sa_in4->sin_addr.s_addr));
        }
        case AF_INET6: {
            struct sockaddr_in6 *sa_in6=(struct sockaddr_in6 *)&ss_addr;
            return IN6_IS_ADDR_MULTICAST(&sa_in6->sin6_addr)?true:false;
        }
    }
    return false;
}

extern "C" int km_parse_address(const char* addr,
                                char* proto, int proto_len,
                                char* host, int  host_len, unsigned short* port)
{
    if(!addr || !host)
        return -1;
    
    const char* tmp1 = nullptr;
    int tmp_len = 0;
    const char* tmp = strstr(addr, "://");
    if(tmp) {
        tmp_len = int(proto_len > tmp-addr?
            tmp-addr:proto_len-1);
        
        if(proto) {
            memcpy(proto, addr, tmp_len);
            proto[tmp_len] = '\0';
        }
        tmp += 3;
    } else {
        if(proto) proto[0] = '\0';
        tmp = addr;
    }
    const char* end = strchr(tmp, '/');
    if(!end)
        end = addr + strlen(addr);
    
    tmp1 = strchr(tmp, '[');
    if(tmp1) {// ipv6 address
        tmp = tmp1 + 1;
        tmp1 = strchr(tmp, ']');
        if(!tmp1)
            return -1;
        tmp_len = int(host_len>tmp1-tmp?
            tmp1-tmp:host_len-1);
        memcpy(host, tmp, tmp_len);
        host[tmp_len] = '\0';
        tmp = tmp1 + 1;
        tmp1 = strchr(tmp, ':');
        if(tmp1 && tmp1 <= end)
            tmp = tmp1 + 1;
        else
            tmp = nullptr;
    } else {// ipv4 address
        tmp1 = strchr(tmp, ':');
        if(tmp1 && tmp1 <= end) {
            tmp_len = int(host_len>tmp1-tmp?
                tmp1-tmp:host_len-1);
            memcpy(host, tmp, tmp_len);
            host[tmp_len] = '\0';
            tmp = tmp1 + 1;
        } else {
            tmp_len = int(host_len>end-tmp?
                end-tmp:host_len-1);
            memcpy(host, tmp, tmp_len);
            host[tmp_len] = '\0';
            tmp = nullptr;
        }
    }
    
    if(port) {
        *port = tmp ? atoi(tmp) : 0;
    }
    
    return 0;
}

int set_nonblocking(SOCKET_FD fd) {
#ifdef KUMA_OS_WIN
    int mode = 1;
    ::ioctlsocket(fd, FIONBIO, (ULONG*)&mode);
#else
    int flag = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flag | O_NONBLOCK | O_ASYNC);
#endif
    return 0;
}

int set_tcpnodelay(SOCKET_FD fd) {
    int opt_val = 1;
    return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&opt_val, sizeof(int));
}

int find_first_set(uint32_t b)
{
    if(0 == b) {
        return -1;
    }
    int n = 0;
    if (!(0xffff & b))
        n += 16;
    if (!((0xff << n) & b))
        n += 8;
    if (!((0xf << n) & b))
        n += 4;
    if (!((0x3 << n) & b))
        n += 2;
    if (!((0x1 << n) & b))
        n += 1;
    return n;
}

int find_first_set(uint64_t b)
{
    if(0 == b) {
        return -1;
    }
    int n = 0;
    if (!(0xffffffff & b))
        n += 32;
    if (!((0xffffLL << n) & b))
        n += 16;
    if (!((0xffLL << n) & b))
        n += 8;
    if (!((0xfLL << n) & b))
        n += 4;
    if (!((0x3LL << n) & b))
        n += 2;
    if (!((0x1LL << n) & b))
        n += 1;
    return n;
}

TICK_COUNT_TYPE get_tick_count_ms()
{
    using namespace std::chrono;
    steady_clock::time_point _now = steady_clock::now();
    milliseconds _now_ms = duration_cast<milliseconds>(_now.time_since_epoch());
	return (TICK_COUNT_TYPE)_now_ms.count();
}

TICK_COUNT_TYPE calc_time_elapse_delta_ms(TICK_COUNT_TYPE now_tick, TICK_COUNT_TYPE& start_tick)
{
    if(now_tick - start_tick > (((TICK_COUNT_TYPE)-1)>>1)) {
        start_tick = now_tick;
        return 0;
    }
    return now_tick - start_tick;
}

#if 0
// need c++ 14
bool is_equal(const std::string& s1, const std::string& s2)
{
    return std::equal(s1.begin(), s1.end(), s2.begin(), s2.end(),
        [] (const char& ch1, const char& ch2) {
            return std::toupper(ch1) == std::toupper(ch2);
    });
}
#endif

bool is_equal(const char* str1, const char* str2)
{
    return strcasecmp(str1, str2) == 0;
}

bool is_equal(const std::string& str1, const std::string& str2)
{
    return str1.length() == str2.length() && strcasecmp(str1.c_str(), str2.c_str()) == 0;
}

bool is_equal(const char* str1, const std::string& str2)
{
    return strcasecmp(str1, str2.c_str()) == 0;
}

bool is_equal(const std::string& str1, const char* str2)
{
    return strcasecmp(str1.c_str(), str2) == 0;
}

bool is_equal(const char* str1, const char* str2, int n)
{
    return strncasecmp(str1, str2, n) == 0;
}

bool is_equal(const std::string& str1, const std::string& str2, int n)
{
    return strncasecmp(str1.c_str(), str2.c_str(), n) == 0;
}

bool is_equal(const char* str1, const std::string& str2, int n)
{
    return strncasecmp(str1, str2.c_str(), n) == 0;
}

bool is_equal(const std::string& str1, const char* str2, int n)
{
    return strncasecmp(str1.c_str(), str2, n) == 0;
}

char* trim_left(char* str)
{
    while (*str && isspace(*str++)) {
        ;
    }
    
    return str;
}

char* trim_right(char* str)
{
    return trim_right(str, str + strlen(str));
}

char* trim_right(char* str, char* str_end)
{
    while (--str_end >= str && isspace(*str_end)) {
        ;
    }
    *(++str_end) = 0;
    
    return str;
}

std::string& trim_left(std::string& str)
{
    str.erase(0, str.find_first_not_of(' '));
    return str;
}

std::string& trim_right(std::string& str)
{
    auto pos = str.find_last_not_of(' ');
    if(pos != std::string::npos) {
        str.erase(pos + 1);
    }
    return str;
}

bool contains_token(const std::string& str, const std::string& token, char delim)
{
    bool found = false;
    for_each_token(str, delim, [&found, &token](std::string &t){
        if (is_equal(t, token)) {
            found = true;
            return false;
        }
        return true;
    });
    
    return found;
}

bool remove_token(std::string& tokens, const std::string& token, char delim)
{
    bool removed = false;
    std::string str;
    for_each_token(tokens, delim, [&removed, &token, &str](std::string &t){
        if (is_equal(t, token)) {
            removed = true;
        } else {
            if (!str.empty()) {
                str += ", ";
            }
            str += t;
        }
        return true;
    });
    tokens = std::move(str);
    
    return removed;
}

int generateRandomBytes(uint8_t *buf, int len)
{
    using bytes_randomizer = std::independent_bits_engine<std::default_random_engine, CHAR_BIT, unsigned long>;
    
    bytes_randomizer br(std::random_device{}());
    std::generate(buf, buf + len, std::ref(br));
    return len;
}

std::string getExecutablePath()
{
    std::string str_path;
#ifdef KUMA_OS_WIN
    char c_path[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, c_path, sizeof(c_path));
    str_path = c_path;
#elif defined(KUMA_OS_MAC)
# ifndef KUMA_OS_IOS
    char c_path[PROC_PIDPATHINFO_MAXSIZE];
    if(proc_pidpath(getpid(), c_path, sizeof(c_path)) <= 0) {
        return "./";
    }
    str_path = c_path;
# else
    char c_path[PATH_MAX] = {0};
    uint32_t size = sizeof(c_path);
    CFBundleRef cf_bundle = CFBundleGetMainBundle();
    if(cf_bundle) {
        CFURLRef cf_url = CFBundleCopyBundleURL(cf_bundle);
        if(CFURLGetFileSystemRepresentation(cf_url, TRUE, (UInt8 *)c_path, PATH_MAX)) {
            CFStringRef cf_str = CFURLCopyFileSystemPath(cf_url, kCFURLPOSIXPathStyle);
            CFStringGetCString(cf_str, c_path, PATH_MAX, kCFStringEncodingASCII);
            CFRelease(cf_str);
        }
        CFRelease(cf_url);
        str_path = c_path;
        if(str_path.at(str_path.length()-1) != PATH_SEPARATOR) {
            str_path += PATH_SEPARATOR;
        }
        return str_path;
    } else {
        _NSGetExecutablePath(c_path, &size);
    }
    str_path = c_path;
# endif
#elif defined(KUMA_OS_LINUX)
    char c_path[1024] = {0};
    if (readlink("/proc/self/exe", c_path, sizeof(c_path)) < 0) {
        return "./";
    }
    str_path = c_path;
#else
    return "./";
#endif
    if(str_path.empty()) {
        return "./";
    }
    auto pos = str_path.rfind(PATH_SEPARATOR, str_path.size());
    if(pos != std::string::npos) {
        str_path.resize(pos);
    }
    str_path.append(1, PATH_SEPARATOR);
    return str_path;
}

std::string getCurrentModulePath()
{
    std::string str_path;
#ifdef KUMA_OS_WIN
    char c_path[MAX_PATH] = { 0 };
    HMODULE hModule = NULL;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCTSTR>(getCurrentModulePath), &hModule);
    GetModuleFileName(hModule, c_path, sizeof(c_path));
    str_path = c_path;
#else
    Dl_info dlInfo;
    dladdr((void*)getCurrentModulePath, &dlInfo);
    str_path = dlInfo.dli_fname;
#endif
    auto pos = str_path.rfind(PATH_SEPARATOR, str_path.size());
    str_path.resize(pos);
    return str_path;
}

#ifndef KUMA_OS_MAC
/**
 * strlcpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer
 *
 * Compatible with *BSD: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero). It does not pad
 * out the result like strncpy() does.
 */
extern "C" size_t strlcpy(char *dest, const char *src, size_t size)
{
    size_t ret = strlen(src);
    
    if (size) {
        size_t len = (ret >= size) ? size - 1 : ret;
        memcpy(dest, src, len);
        dest[len] = '\0';
    }
    return ret;
}

/**
 * strlcat - Append a length-limited, C-string to another
 * @dest: The string to be appended to
 * @src: The string to append to it
 * @count: The size of the destination buffer.
 */
extern "C" size_t strlcat(char *dest, const char *src, size_t count)
{
    size_t dsize = strlen(dest);
    size_t len = strlen(src);
    size_t res = dsize + len;
    
    dest += dsize;
    count -= dsize;
    if (len >= count)
        len = count-1;
    memcpy(dest, src, len);
    dest[len] = 0;
    return res;
}
#endif

KUMA_NS_END

#ifdef KUMA_OS_WIN
KUMA_NS_BEGIN
LPFN_CONNECTEX connect_ex = nullptr;
LPFN_ACCEPTEX accept_ex = nullptr;
LPFN_CANCELIOEX cancel_io_ex = nullptr;
KUMA_NS_END
using namespace kuma;
void kuma_init()
{
    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(1, 1);
    int nResult = WSAStartup(wVersionRequested, &wsaData);
    if (nResult != 0)
    {
        return;
    }

    auto sock = socket(AF_INET, SOCK_STREAM, 0);
    GUID guid = WSAID_CONNECTEX;
    DWORD bytes = 0;
    if (::WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &connect_ex, sizeof(connect_ex), &bytes, 0, 0) != 0)
    {
        connect_ex = nullptr;
    }

    guid = WSAID_ACCEPTEX;
    bytes = 0;
    if (::WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &accept_ex, sizeof(accept_ex), &bytes, 0, 0) != 0)
    {
        accept_ex = nullptr;
    }
    closeFd(sock);
    cancel_io_ex = (LPFN_CANCELIOEX)GetProcAddress(GetModuleHandle("KERNEL32"), "CancelIoEx");
}

void kuma_fini()
{
    WSACleanup();
}

BOOL WINAPI DllMain(HINSTANCE module_handle, DWORD reason_for_call, LPVOID reserved)
{
    switch (reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            //DisableThreadLibraryCalls(module_handle);
            kuma_init();
            break;
        case DLL_PROCESS_DETACH:
            kuma_fini();
            break;
    }
    return TRUE;
}
#endif
