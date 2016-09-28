#ifndef __UdpClient_H__
#define __UdpClient_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

#include <chrono>
#include <string>

using namespace kuma;

class UdpClient : public TestObject
{
public:
    UdpClient(TestLoop* loop, long conn_id);
    
    KMError bind(const char* bind_host, uint16_t bind_port);
    int close();
    
    void startSend(const char* host, uint16_t port);

    void onReceive(KMError err);
    void onClose(KMError err);
    
private:
    void sendData();
    
private:
    TestLoop*   loop_;
    UdpSocket   udp_;
    
    std::string host_;
    uint16_t    port_;
    
    long        conn_id_;
    
    uint32_t    index_;
    uint32_t    max_send_count_;
    std::chrono::steady_clock::time_point   start_point_;
};

#endif
