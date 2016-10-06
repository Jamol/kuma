#ifndef __HttpClient_H__
#define __HttpClient_H__

#include "kmapi.h"
#include "TestLoop.h"

#include <string>

using namespace kuma;

using std_time_point = std::chrono::steady_clock::time_point;

class HttpClient : public TestObject
{
public:
    HttpClient(TestLoop* loop, long conn_id);
    
    void startRequest(std::string& url);
    int close();

    void onData(void* data, size_t len);
    void onSend(KMError err);
    void onClose(KMError err);
    void onHeaderComplete();
    void onRequestComplete();
    
private:
    void sendData();
    
private:
    TestLoop*   loop_;
    HttpRequest http_request_;
    size_t      total_bytes_read_;
    long        conn_id_;
};

#endif
