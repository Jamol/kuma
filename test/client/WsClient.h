#ifndef __WsClient_H__
#define __WsClient_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

#include <chrono>

using namespace kuma;

class WsClient : public LoopObject
{
public:
    WsClient(EventLoop* loop, long conn_id, TestLoop* server);
    
    void startRequest(std::string& url);
    int close();
    
    void onData(uint8_t*, uint32_t);
    void onConnect(int err);
    void onSend(int err);
    void onClose(int err);
    void onTimer();
    
private:
    void sendData();
    
private:
    EventLoop*  loop_;
    WebSocket   ws_;
    
    Timer       timer_;
    TestLoop*   server_;
    long        conn_id_;
    uint32_t    index_;
    uint32_t    max_send_count_;
    std::chrono::steady_clock::time_point   start_point_;
};

#endif
