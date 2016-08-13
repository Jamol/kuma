#ifndef __TcpTest_H__
#define __TcpTest_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

using namespace kuma;

class TcpTest : public LoopObject
{
public:
    TcpTest(TestLoop* loop, long conn_id);
    
    KMError attachFd(SOCKET_FD fd);
    int close();
    
    void onSend(KMError err);
    void onReceive(KMError err);
    void onClose(KMError err);
    
private:
    TestLoop*  loop_;
    TcpSocket   tcp_;
    long        conn_id_;
};

#endif
