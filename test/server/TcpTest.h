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
    
    int attachFd(SOCKET_FD fd);
    int close();
    
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
private:
    TestLoop*  loop_;
    TcpSocket   tcp_;
    long        conn_id_;
};

#endif
