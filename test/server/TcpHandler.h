#pragma once

#include "kmapi.h"
#include "RunLoop.h"
#include "RateReporter.h"

namespace kmsvr {

class TcpHandler : public LoopObject
{
public:
    TcpHandler(const RunLoop::Ptr &loop);
    
    kuma::KMError attachFd(kuma::SOCKET_FD fd);
    void close();
    
private:
    void onSend(kuma::KMError err);
    void onReceive(kuma::KMError err);
    void onClose(kuma::KMError err);
    
private:
    RunLoop*        loop_;
    kuma::TcpSocket tcp_;

    RateReporter    recv_reporter_;
};

}
