#include "kmapi.h"
#include "util/util.h"
#include "TcpServer.h"
#include "UdpServer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <thread>
#include <string>
#include <sstream>

#ifndef KUMA_OS_WIN
#include <signal.h>
#endif

using namespace kuma;

#define THREAD_COUNT    10

static bool g_exit = false;
EventLoop main_loop(POLL_TYPE_NONE);

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

static const char* g_usage =
"   server [option] tcp://0.0.0.0:8443\n"
"   server [option] http://0.0.0.0:8443\n"
"   server [option] ws://0.0.0.0:8443\n"
"   server [option] udp://0.0.0.0:8443\n"
"   server [option] auto://0.0.0.0:8443\n"
"   -v              print version\n"
;

std::vector<std::thread> event_threads;

void printUsage()
{
    printf("%s\n", g_usage);
}

int main(int argc, char *argv[])
{
#ifdef KUMA_OS_WIN
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#else
    signal(SIGPIPE, HandlerRoutine);
    signal(SIGINT, HandlerRoutine);
    signal(SIGTERM, HandlerRoutine);
#endif
    
    if(argc < 2) {
        printUsage();
        return -1;
    }
    
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
    if(km_parse_address(listen_addr.c_str(), proto, sizeof(proto), host, sizeof(host), &port) != KUMA_ERROR_NOERR) {
        printUsage();
        return -1;
    }
    
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
