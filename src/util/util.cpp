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

#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef KUMA_OS_WIN
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
# ifdef KUMA_OS_MAC
#  include "CoreFoundation/CoreFoundation.h"
#  include <mach-o/dyld.h>
# endif
#endif

#include <chrono>
#include <random>

KUMA_NS_BEGIN

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
    if(km_getaddrinfo(host_name, nullptr, &hints, &ai) != 0 || nullptr == ai) {
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
        } else {
            inet_pton(sa->sin_family, addr, &sa->sin_addr);
        }
        return 0;
    }
#endif
    char service[128] = {0};
    struct addrinfo* ai = nullptr;
    if(!addr && hints) {
        hints->ai_flags |= AI_PASSIVE;
    }
    SNPRINTF(service, sizeof(service)-1, "%d", port);
    if(km_getaddrinfo(addr, service, hints, &ai) != 0 || !ai || ai->ai_addrlen > sk_addr_len) {
        if(ai) km_freeaddrinfo(ai);
        return -1;
    }
    if(sk_addr)
        memcpy(sk_addr, ai->ai_addr, ai->ai_addrlen);
    km_freeaddrinfo(ai);
    return 0;
}

extern "C" int km_get_sock_addr(struct sockaddr * sk_addr, unsigned int sk_addr_len,
                                char* addr, unsigned int addr_len, unsigned short* port)
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
        return KUMA_ERROR_INVALID_PARAM;
    
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
    
    return KUMA_ERROR_NOERR;
}

int set_nonblocking(int fd) {
#ifdef KUMA_OS_WIN
    int mode = 1;
    ::ioctlsocket(fd, FIONBIO, (ULONG*)&mode);
#else
    int flag = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flag | O_NONBLOCK | O_ASYNC);
#endif
    return 0;
}

int find_first_set(unsigned int b)
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

TICK_COUNT_TYPE get_tick_count_ms()
{
    std::chrono::steady_clock::time_point _now = std::chrono::steady_clock::now();
    std::chrono::milliseconds _now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(_now.time_since_epoch());
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
    return strcasecmp(str1.c_str(), str2.c_str()) == 0;
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

std::string getExecutablePath()
{
    std::string str_path;
#ifdef KUMA_OS_WIN
    char c_path[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, c_path, sizeof(c_path));
    str_path = c_path;
#elif defined(KUMA_OS_MAC)
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
#else
    char buf[32];
    snprintf(buf, sizeof(buf), "/proc/%u/exe", getpid());
    char c_path[1024] = {0};
    readlink(buf, c_path, sizeof(c_path));
    str_path = c_path;
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
    str_path.append(1, PATH_SEPARATOR);
    return str_path;
}

KUMA_NS_END

#ifdef KUMA_OS_WIN
void kuma_init()
{
    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(1, 1);
    int nResult = WSAStartup(wVersionRequested, &wsaData);
    if (nResult != 0)
    {
        return;
    }
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
