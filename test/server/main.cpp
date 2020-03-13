#include "kmapi.h"
#include "TcpServer.h"
#include "UdpServer.h"
#include "../../third_party/libkev/src/util/defer.h"
#include "testutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <string>
#include <sstream>

#ifndef KUMA_OS_WIN
#include <signal.h>
#endif

using namespace kuma;

#define THREAD_COUNT    5

static bool g_exit = false;
EventLoop main_loop(PollType::NONE);
std::string www_path;

int km_parse_address(const char* addr,
                     char* proto, int proto_len,
                     char* host, int  host_len, unsigned short* port);

#ifdef KUMA_OS_WIN
BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
    if(CTRL_C_EVENT == dwCtrlType) {
        g_exit = TRUE;
        main_loop.stop();
        return TRUE;
    }
    return FALSE;
}
#else
void HandlerRoutine(int sig)
{
    if(sig == SIGTERM || sig == SIGINT || sig == SIGKILL) {
        g_exit = true;
        main_loop.stop();
    }
}
#endif

static const std::string g_usage =
"   server [option] tcp://0.0.0.0:52328\n"
"   server [option] http://0.0.0.0:8443\n"
"   server [option] ws://0.0.0.0:8443\n"
"   server [option] udp://0.0.0.0:52328\n"
"   server [option] auto://0.0.0.0:8443\n"
"   -v              print version\n"
;

std::vector<std::thread> event_threads;

void printUsage()
{
    printf("%s\n", g_usage.c_str());
}

int main(int argc, char *argv[])
{
#ifdef KUMA_OS_WIN
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#else
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, HandlerRoutine);
    signal(SIGTERM, HandlerRoutine);
#endif
    
    if(argc < 2) {
        printUsage();
        return -1;
    }
    
    www_path = ::getCurrentModulePath();
    www_path += PATH_SEPARATOR;
#if defined(KUMA_OS_WIN)
    www_path += "../../../";
#elif defined(KUMA_OS_MAC)
    www_path += "../../../";
#elif defined(KUMA_OS_LINUX)
    www_path += "../../";
#endif
    www_path += "test/www";
    
    std::string listen_addr;
    
    for (int i=1; i<argc; ++i) {
        if(argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'v':
                    printf("kuma test server v1.0\n");
                    return 0;
                default:
                    printUsage();
                    return -1;
            }
        } else {
            listen_addr = argv[i];
        }
    }
    
    if(listen_addr.empty()) {
        printUsage();
        return -1;
    }
    char proto[16] = {0};
    char host[128] = {0};
    uint16_t port = 0;
    if(km_parse_address(listen_addr.c_str(), proto, sizeof(proto), host, sizeof(host), &port) != 0) {
        printUsage();
        return -1;
    }
    
    kuma::init();
    DEFER(kuma::fini());
    if (!main_loop.init()) {
        printf("failed to init EventLoop\n");
        return -1;
    }
    
    if(strcmp(proto, "udp") == 0) {
        UdpServer udp_server(&main_loop);
        udp_server.bind(host, port);
        main_loop.loop();
        udp_server.close();
    } else {
        TcpServer tcp_server(&main_loop, THREAD_COUNT);
        tcp_server.startListen(proto, host, port);
        main_loop.loop();
        tcp_server.stopListen();
    }
    
    printf("main exit...\n");
#ifdef KUMA_OS_WIN
    SetConsoleCtrlHandler(HandlerRoutine, FALSE);
#endif
    
    return 0;
}

int km_parse_address(const char* addr,
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

