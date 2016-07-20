#ifndef __TcpClient_H__
#define __TcpClient_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

#include <chrono>

using namespace kuma;

class TcpClient : public LoopObject
{
public:
    TcpClient(EventLoop* loop, long conn_id, TestLoop* server);
    
    int bind(const char* bind_host, uint16_t bind_port);
    int connect(const char* host, uint16_t port);
    int close();
    
    void onConnect(int err);
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    void onTimer();
    
private:
    void sendData();
    
private:
    EventLoop*  loop_;
    TcpSocket   tcp_;
    
    Timer       timer_;
    
    TestLoop*   server_;
    long        conn_id_;
    
    uint32_t    index_;
    uint32_t    max_send_count_;
    std::chrono::steady_clock::time_point   start_point_;
};

#endif
