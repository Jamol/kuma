#include "TcpServer.h"

#include <string.h>

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

int TcpServer::startListen(const char* proto, const char* host, uint16_t port)
{
    if(strcmp(proto, "tcp") == 0) {
        proto_ = PROTO_TCP;
    } else if(strcmp(proto, "http") == 0) {
        proto_ = PROTO_HTTP;
    } else if(strcmp(proto, "ws") == 0) {
        proto_ = PROTO_WS;
    } else if(strcmp(proto, "auto") == 0) {
        proto_ = PROTO_AUTO;
    }
    loop_pool_.init(thr_count_);
    server_.setAcceptCallback([this] (SOCKET_FD fd) { onAccept(fd); });
    server_.setErrorCallback([this] (int err) { onError(err); });
    return server_.startListen(host, port);
}

int TcpServer::stopListen()
{
    server_.stopListen(nullptr, 0);
    loop_pool_.stop();
    return 0;
}

void TcpServer::onAccept(SOCKET_FD fd)
{
    printf("TcpServer::onAccept, fd=%d, proto=%d\n", fd, proto_);
    TestLoop* test_loop = loop_pool_.getNextLoop();
    test_loop->addFd(fd, proto_);
}

void TcpServer::onError(int err)
{
    printf("TcpServer::onError, err=%d\n", err);
}
