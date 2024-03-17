#include "kmapi.h"
#include "ProtoServer.h"
#include "RunLoopPool.h"
#include "UdpServer.h"
#include "libkev/src/utils/defer.h"
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
using namespace kmsvr;

#define THREAD_COUNT    5

static bool g_exit = false;
EventLoop main_loop(PollType::DEFAULT);
std::string www_path;

extern "C" int km_parse_address(const char *addr,
    char *proto,
    size_t proto_len,
    char *host,
    size_t  host_len,
    unsigned short *port);


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
    
    RunLoopPool loop_pool;
    if (!loop_pool.start(0, main_loop.getPollType())) {
        return -1;
    }
    if(strcmp(proto, "udp") == 0) {
        UdpServer udp_server(&main_loop);
        udp_server.bind(host, port);
        main_loop.loop();
        udp_server.close();
    } else {
        ProtoServer tcp_server(&main_loop, &loop_pool);
        tcp_server.start(listen_addr);
        main_loop.loop();
        tcp_server.stop();
    }
    loop_pool.stop();
    
    printf("main exit...\n");
#ifdef KUMA_OS_WIN
    SetConsoleCtrlHandler(HandlerRoutine, FALSE);
#endif
    
    return 0;
}

