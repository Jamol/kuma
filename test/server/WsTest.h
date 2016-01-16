#ifndef __WsTest_H__
#define __WsTest_H__

#include "kmapi.h"
#include "TestLoop.h"

#include <map>

using namespace kuma;

class TestLoop;
class WsTest : public LoopObject
{
public:
    WsTest(EventLoop* loop, long conn_id, TestLoop* server);

    int attachFd(SOCKET_FD fd, uint32_t ssl_flags);
    int attachSocket(TcpSocket&& tcp, HttpParser&& parser);
    int close();
    
    void onSend(int err);
    void onClose(int err);
    
    void onData(uint8_t*, uint32_t);
    
private:
    void cleanup();
    void sendTestData();
    
private:
    EventLoop*      loop_;
    WebSocket       ws_;
    TestLoop*       server_;
    long            conn_id_;
};

#endif
