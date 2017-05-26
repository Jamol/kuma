#include "TcpServer.h"

#include <string.h>
#include <iostream>
#include <sstream>

TcpServer::TcpServer(EventLoop* loop, int count)
: loop_(loop)
, server_(loop_)
, proto_(PROTO_TCP)
, thr_count_(count)
, loop_pool_()
{
    
}

TcpServer::~TcpServer()
{
    
}

void TcpServer::cleanup()
{

}

KMError TcpServer::startListen(const std::string &proto, const std::string &host, uint16_t port)
{
    if(proto == "tcp") {
        proto_ = PROTO_TCP;
    } else if(proto == "http") {
        proto_ = PROTO_HTTP;
    } else if(proto == "https") {
        proto_ = PROTO_HTTPS;
    } else if(proto == "ws") {
        proto_ = PROTO_WS;
    } else if(proto == "wss") {
        proto_ = PROTO_WSS;
    } else if(proto == "auto") {
        proto_ = PROTO_AUTO;
    } else if(proto == "autos") {
        proto_ = PROTO_AUTOS;
    }
    loop_pool_.init(thr_count_, loop_->getPollType());
    server_.setAcceptCallback([this] (SOCKET_FD fd, const char* ip, uint16_t port) -> bool { return onAccept(fd, ip, port); });
    server_.setErrorCallback([this] (KMError err) { onError(err); });
    return server_.startListen(host.c_str(), port);
}

KMError TcpServer::stopListen()
{
    server_.stopListen(nullptr, 0);
    loop_pool_.stop();
    return KMError::NOERR;
}

bool TcpServer::onAccept(SOCKET_FD fd, const std::string &ip, uint16_t port)
{
    std::stringstream ss;
    ss << "TcpServer::onAccept, fd=" << fd << ", ip=" << ip << ", port=" << port << ", proto=" << proto_ << std::endl;
    std::cout << ss.str();
    TestLoop* test_loop = loop_pool_.getNextLoop();
    test_loop->addFd(fd, proto_);
    return true;
}

void TcpServer::onError(KMError err)
{
    printf("TcpServer::onError, err=%d\n", err);
}
