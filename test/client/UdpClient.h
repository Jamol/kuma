#ifndef __UdpClient_H__
#define __UdpClient_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

#include <chrono>
#include <string>

using namespace kuma;

class UdpClient : public LoopObject
{
public:
    UdpClient(EventLoop* loop, long conn_id, TestLoop* server);
    
    int bind(const char* bind_host, uint16_t bind_port);
    int close();
    
    void startSend(const char* host, uint16_t port);

    void onReceive(int err);
    void onClose(int err);
    
private:
    void sendData();
    
private:
    EventLoop*  loop_;
    UdpSocket   udp_;
    
    std::string host_;
    uint16_t    port_;
    
    TestLoop*   server_;
    long        conn_id_;
    
    uint32_t    index_;
    uint32_t    max_send_count_;
    std::chrono::steady_clock::time_point   start_point_;
};

#endif
