#pragma once

#include "kmapi.h"
#include "RunLoop.h"

#include <string>

namespace kmsvr {

class WsHandler : public LoopObject
{
public:
    WsHandler(const RunLoop::Ptr &loop, const std::string &ver);
    ~WsHandler();

    kuma::KMError attachFd(kuma::SOCKET_FD fd, uint32_t ssl_flags, const kuma::KMBuffer *init_buf);
    kuma::KMError attachSocket(kuma::TcpSocket&& tcp, kuma::HttpParser&& parser, const kuma::KMBuffer *init_buf);
    kuma::KMError attachStream(uint32_t stream_id, kuma::H2Connection* conn);
    void close();
    
private:
    bool onHandshake(kuma::KMError err);
    void onOpen(kuma::KMError err);
    void onSend(kuma::KMError err);
    void onClose(kuma::KMError err);
    
    void onData(kuma::KMBuffer &buf, bool is_text, bool is_fin);

    void sendTestData();
    
private:
    RunLoop*        loop_;
    kuma::WebSocket ws_;
};

}
