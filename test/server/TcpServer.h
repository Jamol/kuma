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
    
    KMError startListen(const char* proto, const char* host, uint16_t port);
    KMError stopListen();
    
    bool onAccept(SOCKET_FD, const char* ip, uint16_t port);
    void onError(KMError err);
    
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
