#ifndef __HttpClient_H__
#define __HttpClient_H__

#include "kmapi.h"
#include "TestLoop.h"

#include <string>

using namespace kuma;

using std_time_point = std::chrono::steady_clock::time_point;

class HttpClient : public LoopObject
{
public:
    HttpClient(EventLoop* loop, long conn_id, TestLoop* server);
    
    void startRequest(std::string& url);
    int close();

    void onData(uint8_t* data, uint32_t len);
    void onSend(int err);
    void onClose(int err);
    void onHeaderComplete();
    void onRequestComplete();
    
private:
    void sendData();
    
private:
    EventLoop*  loop_;
    HttpRequest http_request_;
    uint32_t    total_bytes_read_;
    TestLoop*   server_;
    long        conn_id_;
};

#endif
