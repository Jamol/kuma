#include "kmapi.h"
#include "util/util.h"
#include "LoopPool.h"
#include "util/AutoCleaner.h"

#include <stdio.h>
#include <stdlib.h>
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
"   client [option] tcp://www.kuma.com:8443\n"
"   client [option] http://www.kuma.com\n"
"   client [option] ws://www.kuma.com\n"
"   client [option] udp//www.kuma.com:5500\n"
"   client [option] mcast//224.0.0.1:5500\n\n"
"   -b host:port    local host and port to be binded to\n"
"   -c number       concurrent clients\n"
"   -t ms           send interval\n"
"   -v              print version\n"
;

std::vector<std::thread> event_threads;

void printUsage()
{
    printf("%s\n", g_usage);
}

static uint32_t _send_interval_ = 0;
uint32_t getSendInterval()
{
    return _send_interval_;
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
    
    std::string addr;
    std::string bind_addr;
    int concurrent = 1;
    
    for (int i=1; i<argc; ++i) {
        if(argv[i][0] == '-') {
            switch (argv[i][1]) {
                case 'v':
                    printf("kuma test client v1.0\n");
                    return 0;
                case 'b':
                    if(++i < argc) {
                        bind_addr = argv[i];
                    } else {
                        printUsage();
                        return -1;
                    }
                    break;
                case 'c':
                    if(++i < argc) {
                        concurrent = atoi(argv[i]);
                    } else {
                        printUsage();
                        return -1;
                    }
                    break;
                case 't':
                    if (++i < argc) {
                        _send_interval_ = atoi(argv[i]);
                    } else {
                        printUsage();
                        return -1;
                    }
                    break;
                default:
                    printUsage();
                    return -1;
            }
        } else {
            addr = argv[i];
        }
    }
    
    if(addr.empty()) {
        printUsage();
        return -1;
    }
    
    kuma::init();
    AUTO_CLEAN([]{ kuma::fini(); });
    
    char proto[16] = {0};
    char host[64] = {0};
    uint16_t port = 0;
    if(km_parse_address(addr.c_str(), proto, sizeof(proto), host, sizeof(host), &port) != KUMA_ERROR_NOERR) {
        printUsage();
        return -1;
    }
    
    if (!main_loop.init()) {
        printf("failed to init EventLoop\n");
        return -1;
    }
    if(concurrent <= 0) {
        concurrent = 1;
    }
    
    LoopPool loop_pool;
    loop_pool.init(THREAD_COUNT);
    loop_pool.startTest(addr, bind_addr, concurrent);
    
    main_loop.loop();
    printf("stop loops...\n");
    loop_pool.stop();
    
	printf("main exit...\n");
#ifdef KUMA_OS_WIN
	SetConsoleCtrlHandler(HandlerRoutine, FALSE);
#endif
	return 0;
}
