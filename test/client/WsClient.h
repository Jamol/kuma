#ifndef __WsClient_H__
#define __WsClient_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

#include <chrono>

using namespace kuma;

class WsClient : public TestObject
{
public:
    WsClient(TestLoop* loop, long conn_id);
    
    void startRequest(const std::string& url);
    int close();
    
    void onData(void*, size_t);
    void onConnect(KMError err);
    void onSend(KMError err);
    void onClose(KMError err);
    
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
