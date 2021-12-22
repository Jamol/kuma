#include "ProtoServer.h"
#include "ProtoDemuxer.h"
#include "TcpHandler.h"
#include "HttpHandler.h"
#include "Http2Handler.h"
#include "WsHandler.h"


using namespace kmsvr;
using namespace kuma;


extern "C" int km_parse_address(const char *addr,
                                char *proto,
                                int proto_len,
                                char *host,
                                int  host_len,
                                unsigned short *port);
ProtoType toProtoType(const std::string &proto);


ProtoServer::ProtoServer(EventLoop *loop, RunLoopPool *pool)
: loop_(loop)
, pool_(pool)
, listener_(loop)
{
    
}

ProtoServer::~ProtoServer()
{
    stop();
}

bool ProtoServer::start(const std::string &listen_addr)
{
    char proto[16] = {0};
    char host[128] = {0};
    uint16_t port = 0;
    if(km_parse_address(listen_addr.c_str(), proto, sizeof(proto), host, sizeof(host), &port) != 0) {
        return false;
    }
    proto_ = toProtoType(proto);
    if (proto_ == ProtoType::UNKNOWN) {
        return false;
    }
    listener_.setAcceptCallback([this] (SOCKET_FD fd, const char* ip, uint16_t port) {
        return onAccept(fd, ip, port);
    });
    listener_.setErrorCallback([this] (KMError err) { onError(err); });
    return listener_.startListen(host, port) == KMError::NOERR;
}

void ProtoServer::stop()
{
    listener_.stopListen(nullptr, 0);
}

bool ProtoServer::onAccept(SOCKET_FD fd, const std::string &ip, uint16_t port)
{
    printf("ProtoServer::onAccept, fd=%d, addr=%s:%d, proto=%d\n", (int)fd, ip.c_str(), port, (int)proto_);
    switch (proto_) {
        case ProtoType::TCP: {
            auto loop = pool_->getRunLoop();
            loop->getEventLoop()->async([=] {
                auto tcp = std::make_shared<TcpHandler>(loop);
                loop->addObject(tcp->getObjectId(), tcp);
                tcp->attachFd(fd);
            });
            break;
        }
        case ProtoType::HTTP:
        case ProtoType::HTTPS:
        {
            auto loop = pool_->getRunLoop();
            loop->getEventLoop()->async([=] {
                auto http = std::make_shared<HttpHandler>(loop, "HTTP/1.1");
                loop->addObject(http->getObjectId(), http);
                http->attachFd(fd, proto_==ProtoType::HTTPS ? SSL_ENABLE : 0, nullptr);
            });
            break;
        }
        case ProtoType::WS:
        case ProtoType::WSS:
        {
            auto loop = pool_->getRunLoop();
            loop->getEventLoop()->async([=] {
                auto ws = std::make_shared<WsHandler>(loop, "HTTP/1.1");
                loop->addObject(ws->getObjectId(), ws);
                ws->attachFd(fd, proto_==ProtoType::WSS ? SSL_ENABLE : 0, nullptr);
            });
            break;
        }
        case ProtoType::AUTO:
        case ProtoType::AUTOS:
        {
            auto loop = pool_->getRunLoop();
            loop->getEventLoop()->async([=] {
                auto demuxer = std::make_shared<ProtoDemuxer>(loop);
                loop->addObject(demuxer->getObjectId(), demuxer);
                demuxer->attachFd(fd, proto_==ProtoType::AUTOS ? SSL_ENABLE : 0,
                [this,loop=std::move(loop)] (ProtoDemuxer::Proto p, TcpSocket &&tcp, HttpParser &&parser, const KMBuffer *init_buf) {
                    switch (p)
                    {
                        case ProtoDemuxer::Proto::HTTP: {
                            auto http = std::make_shared<HttpHandler>(loop, "HTTP/1.1");
                            loop->addObject(http->getObjectId(), http);
                            http->attachSocket(std::move(tcp), std::move(parser), init_buf);
                            break;
                        }
                        case ProtoDemuxer::Proto::HTTP2: {
                            auto http2 = std::make_shared<Http2Handler>(loop, pool_);
                            loop->addObject(http2->getObjectId(), http2);
                            http2->attachSocket(std::move(tcp), std::move(parser), init_buf);
                            break;
                        }
                        case ProtoDemuxer::Proto::WebSocket: {
                            auto ws = std::make_shared<WsHandler>(loop, "HTTP/1.1");
                            loop->addObject(ws->getObjectId(), ws);
                            ws->attachSocket(std::move(tcp), std::move(parser), init_buf);
                            break;
                        }
                        
                        default:
                            break;
                    }
                },
                [] (int errcode, const std::string &errmsg) {
                    
                });
            });
            break;
        }
            
        default:
            break;
    }
    return true;
}

void ProtoServer::onError(KMError err)
{
    printf("ProtoServer::onError, err=%d\n", (int)err);
}


ProtoType toProtoType(const std::string &proto)
{
    if(proto == "tcp") {
        return ProtoType::TCP;
    } else if(proto == "http") {
        return ProtoType::HTTP;
    } else if(proto == "https") {
        return ProtoType::HTTPS;
    } else if(proto == "ws") {
        return ProtoType::WS;
    } else if(proto == "wss") {
        return ProtoType::WSS;
    } else if(proto == "auto") {
        return ProtoType::AUTO;
    } else if(proto == "autos") {
        return ProtoType::AUTOS;
    } else if(proto == "udp") {
        return ProtoType::UDP;
    } else {
        return ProtoType::UNKNOWN;
    }
}

