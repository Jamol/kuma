
#ifndef __H2ConnTest_H__
#define __H2ConnTest_H__

#include "kmapi.h"
#include "TestLoop.h"

#include <string>
#include <list>

using namespace kuma;

using std_time_point = std::chrono::steady_clock::time_point;

class H2ConnTest : public TestObject, public ObjectManager
{
public:
    H2ConnTest(TestLoop* loop, long conn_id);
    ~H2ConnTest();
    
    KMError attachSocket(TcpSocket&& tcp, HttpParser&& parser, const KMBuffer *init_buf);
    int close() override;
    
private:
    bool onAccept(uint32_t stream_id, const char *method, const char *path, const char *host, const char *protocol);
    void onError(int err);
    void cleanup();
    
    void addObject(long conn_id, TestObject* obj) override;
    void removeObject(long conn_id) override;
    EventLoop* eventLoop() override { return loop_->eventLoop(); }
    
private:
    TestLoop*   loop_;
    H2Connection conn_;
    uint32_t    total_bytes_read_;
    long        conn_id_;
    
    std::mutex      h2_mutex_;
    std::map<long, TestObject*> obj_map_;
};

#endif /* __H2ConnTest_H__ */
