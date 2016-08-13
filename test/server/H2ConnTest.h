
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
    H2ConnTest(TestLoop* loop, long conn_id);
    
    KMError attachSocket(TcpSocket&& tcp, HttpParser&& parser);
    int close();
    
private:
    void onConnect(int err);
    
private:
    TestLoop*   loop_;
    H2Connection conn_;
    uint32_t    total_bytes_read_;
    long        conn_id_;
};

#endif /* __H2ConnTest_H__ */
