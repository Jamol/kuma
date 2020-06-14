
#pragma once

#include "kmapi.h"
#include "RunLoop.h"

#include <string>

namespace kmsvr {

class RunLoopPool;

class Http2Handler : public LoopObject
{
public:
    Http2Handler(const RunLoop::Ptr &loop, RunLoopPool *pool);
    ~Http2Handler();
    
    kuma::KMError attachSocket(kuma::TcpSocket&& tcp, kuma::HttpParser&& parser, const kuma::KMBuffer *init_buf);
    void close();
    
private:
    bool onAccept(uint32_t stream_id, const char *method, const char *path, const char *host, const char *protocol);
    void onError(int err);
    
    kuma::EventLoop* eventLoop() { return loop_->getEventLoop().get(); }
    
private:
    RunLoop*            loop_;
    RunLoopPool*        pool_;
    kuma::H2Connection  conn_;
};

}
