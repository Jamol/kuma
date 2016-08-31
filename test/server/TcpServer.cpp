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

KMError TcpServer::startListen(const char* proto, const char* host, uint16_t port)
{
    if(strcmp(proto, "tcp") == 0) {
        proto_ = PROTO_TCP;
    } else if(strcmp(proto, "http") == 0) {
        proto_ = PROTO_HTTP;
    } else if(strcmp(proto, "https") == 0) {
        proto_ = PROTO_HTTPS;
    } else if(strcmp(proto, "ws") == 0) {
        proto_ = PROTO_WS;
    } else if(strcmp(proto, "wss") == 0) {
        proto_ = PROTO_WSS;
    } else if(strcmp(proto, "auto") == 0) {
        proto_ = PROTO_AUTO;
    } else if(strcmp(proto, "autos") == 0) {
        proto_ = PROTO_AUTOS;
    }
    loop_pool_.init(thr_count_);
    server_.setListenCallback([this] (SOCKET_FD fd, const char* ip, uint16_t port) { onAccept(fd, ip, port); });
    server_.setErrorCallback([this] (KMError err) { onError(err); });
    return server_.startListen(host, port);
}

KMError TcpServer::stopListen()
{
    server_.stopListen(nullptr, 0);
    loop_pool_.stop();
    return KMError::NOERR;
}

void TcpServer::onAccept(SOCKET_FD fd, const char* ip, uint16_t port)
{
    std::stringstream ss;
    ss << "TcpServer::onAccept, fd=" << fd << ", ip=" << ip << ", port=" << port << ", proto=" << proto_ << std::endl;
    std::cout << ss.str();
    TestLoop* test_loop = loop_pool_.getNextLoop();
    test_loop->addFd(fd, proto_);
}

void TcpServer::onError(KMError err)
{
    printf("TcpServer::onError, err=%d\n", err);
}
