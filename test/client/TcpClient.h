#ifndef __TcpClient_H__
#define __TcpClient_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"
#include "DataSender.h"

#include <chrono>

using namespace kuma;

class TcpClient : public TestObject
{
public:
    TcpClient(TestLoop* loop, long conn_id);
    
    void setSslFlags(uint32_t ssl_flags) { ssl_flags_ = ssl_flags; }
    KMError bind(const std::string &bind_host, uint16_t bind_port);
    KMError connect(const std::string &host, uint16_t port);
    int close();
    
    void onConnect(KMError err);
    void onSend(KMError err);
    void onReceive(KMError err);
    void onClose(KMError err);
    void onTimer();
    
    KMError onData(uint8_t *data, size_t size);
    
private:
    void sendDataEcho();
    void sendDataMax();
    int sendData(void *data, size_t size);
    
private:
    TestLoop*   loop_;
    TcpSocket   tcp_;
    ProxyConnection proxy_conn_;
    
    Timer       timer_;
    DataSender  data_sender_;
    
    uint32_t    ssl_flags_ = 0;
    long        conn_id_;
    
    uint32_t    index_;
    uint32_t    max_send_count_;
    std::chrono::steady_clock::time_point   start_point_;
};

#endif
