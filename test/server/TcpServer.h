#ifndef __TcpServer_H__
#define __TcpServer_H__

#include "kmapi.h"
#include "TestLoop.h"
#include "LoopPool.h"

#include <map>
#include <vector>
#include <atomic>

using namespace kuma;

class TcpServer
{
public:
    TcpServer(EventLoop* loop, int count);
    ~TcpServer();
    
    int startListen(const char* proto, const char* host, uint16_t port);
    int stopListen();
    
    void onAccept(SOCKET_FD);
    void onError(int err);
    
private:
    void cleanup();
    
private:
    EventLoop*      loop_;
    TcpListener     server_;
    Proto           proto_;
    int             thr_count_;
    LoopPool        loop_pool_;
};

#endif
