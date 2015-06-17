#ifndef __HttpTest_H__
#define __HttpTest_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

#include <map>

using namespace kuma;

class TestLoop;
class HttpTest : public LoopObject
{
public:
    HttpTest(EventLoop* loop, long conn_id, TestLoop* server);

    int attachFd(SOCKET_FD fd);
    int close();
    
    void onSend(int err);
    void onClose(int err);
    
    void onHttpData(uint8_t*, uint32_t);
    void onHeaderComplete();
    void onRequestComplete();
    void onResponseComplete();
    
private:
    void cleanup();
    
private:
    EventLoop*      loop_;
    HttpResponse    http_;
    TestLoop*       server_;
    long            conn_id_;
    
};

#endif
