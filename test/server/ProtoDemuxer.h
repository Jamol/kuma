#pragma once

#include "kmapi.h"
#include "RunLoop.h"
#include "libkev/src/util/DestroyDetector.h"

#include <functional>


namespace kmsvr {

class ProtoDemuxer : public kev::DestroyDetector, public LoopObject
{
public:
    enum class Proto {
        HTTP,
        HTTP2,
        WebSocket
    };
    using ProtoCallback = std::function<void(Proto, kuma::TcpSocket&&, kuma::HttpParser&&, const kuma::KMBuffer*)>;
    using ErrorCallback = std::function<void(int errcode, const std::string &errmsg)>;

    ProtoDemuxer(const RunLoop::Ptr &loop);
    ~ProtoDemuxer();

    kuma::KMError attachFd(kuma::SOCKET_FD fd, uint32_t ssl_flags, ProtoCallback scb, ErrorCallback ecb);
    void close();
    size_t getLoad() const override { return 2; }
    
    void onSend(kuma::KMError err);
    void onReceive(kuma::KMError err);
    void onClose(kuma::KMError err);
    
    void onHttpData(kuma::KMBuffer &buf);
    void onHttpEvent(kuma::HttpEvent ev);
    
private:
    void cleanup();
    bool checkHttp2();
    void demuxHttp(const kuma::KMBuffer *init_buf);

    void onProto(Proto proto, const kuma::KMBuffer *init_buf);
    void onError(int errcode, const std::string &errmsg);
    
private:
    RunLoop*            loop_;
    kuma::TcpSocket     tcp_;
    kuma::HttpParser    http_parser_;
    kuma::Timer         timer_;
    ProtoCallback       scb_;
    ErrorCallback       ecb_;
};

} //namespace kmsvr
