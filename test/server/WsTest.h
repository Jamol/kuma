#ifndef __WsTest_H__
#define __WsTest_H__

#include "kmapi.h"
#include "TestLoop.h"

#include <map>

using namespace kuma;

class WsTest : public TestObject
{
public:
    WsTest(TestLoop* loop, long conn_id);

    KMError attachFd(SOCKET_FD fd, uint32_t ssl_flags, void *init, size_t len);
    KMError attachSocket(TcpSocket&& tcp, HttpParser&& parser, void *init, size_t len);
    int close();
    
    void onSend(KMError err);
    void onClose(KMError err);
    
    void onData(void*, size_t);
    
private:
    void cleanup();
    void sendTestData();
    
private:
    TestLoop*       loop_;
    WebSocket       ws_;
    long            conn_id_;
};

#endif
