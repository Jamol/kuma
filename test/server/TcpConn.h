#ifndef __TcpConn_H__
#define __TcpConn_H__

#include "kmapi.h"
#include "util/util.h"
#include "TestLoop.h"

using namespace kuma;

class TcpConn : public LoopObject
{
public:
    TcpConn(TestLoop* loop, long conn_id);
    
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
