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
    WsClient(TestLoop* loop, long conn_id);
    
    void startRequest(std::string& url);
    int close();
    
    void onData(uint8_t*, size_t);
    void onConnect(int err);
    void onSend(int err);
    void onClose(int err);
    
private:
    void sendData();
    
private:
    TestLoop*   loop_;
    WebSocket   ws_;
    
    bool        timed_sending_;
    Timer       timer_;
    long        conn_id_;
    uint32_t    index_;
    uint32_t    max_send_count_;
    std::chrono::steady_clock::time_point   start_point_;
};

#endif
