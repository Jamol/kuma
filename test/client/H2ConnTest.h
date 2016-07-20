
#ifndef __H2ConnTest_H__
#define __H2ConnTest_H__

#include "kmapi.h"
#include "TestLoop.h"

#include <string>

using namespace kuma;

using std_time_point = std::chrono::steady_clock::time_point;

class H2ConnTest : public LoopObject
{
public:
    H2ConnTest(EventLoop* loop, long conn_id, TestLoop* server);
    
    void connect(const std::string& host, uint16_t port);
    int close();
    
private:
    void onConnect(int err);
    
private:
    EventLoop*  loop_;
    H2Connection conn_;
    uint32_t    total_bytes_read_;
    TestLoop*   server_;
    long        conn_id_;
};

#endif /* __H2ConnTest_H__ */
