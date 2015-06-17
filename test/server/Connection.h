#ifndef __Connection_H__
#define __Connection_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

using namespace kuma;

class TestLoop;
class Connection : public LoopObject
{
public:
    Connection(EventLoop* loop, long conn_id, TestLoop* server);
    
    int attachFd(SOCKET_FD fd);
    int close();
    
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    
private:
    EventLoop*  loop_;
    TcpSocket   tcp_;
    TestLoop*   server_;
    long        conn_id_;
};

#endif
