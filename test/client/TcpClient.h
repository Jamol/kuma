#ifndef __TcpClient_H__
#define __TcpClient_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

#include <chrono>

using namespace kuma;

class TcpClient : public TestObject
{
public:
    TcpClient(TestLoop* loop, long conn_id);
    
    KMError bind(const char* bind_host, uint16_t bind_port);
    KMError connect(const char* host, uint16_t port);
    int close();
    
    void onConnect(KMError err);
    void onSend(KMError err);
    void onReceive(KMError err);
    void onClose(KMError err);
    void onTimer();
    
private:
    void sendData();
    
private:
    TestLoop*   loop_;
    TcpSocket   tcp_;
    
    Timer       timer_;
    
    long        conn_id_;
    
    uint32_t    index_;
    uint32_t    max_send_count_;
    std::chrono::steady_clock::time_point   start_point_;
};

#endif
