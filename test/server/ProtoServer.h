#pragma once

#include "kmapi.h"
#include "RunLoopPool.h"

namespace kmsvr {

enum class ProtoType {
    UNKNOWN,
    TCP,
    TCPS,
    HTTP,
    HTTPS,
    WS,
    WSS,
    UDP,
    AUTO,
    AUTOS
};

class ProtoServer
{
public:
    ProtoServer(kuma::EventLoop *loop, RunLoopPool *pool);
    ~ProtoServer();
    
    bool start(const std::string &listen_addr);
    void stop();

    bool onAccept(kuma::SOCKET_FD, const std::string &ip, uint16_t port);
    void onError(kuma::KMError err);
    
private:
    kuma::EventLoop*    loop_;
    RunLoopPool*        pool_;
    kuma::TcpListener   listener_;
    ProtoType           proto_ = ProtoType::UNKNOWN;
};

} // namespace kmsvr
