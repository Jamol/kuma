#include "TestLoop.h"
#include "LoopPool.h"
#include "Connection.h"
#include "HttpTest.h"
#include "WsTest.h"
#include "AutoHelper.h"

#include <string.h>

TestLoop::TestLoop(LoopPool* server, PollType poll_type)
: loop_(new EventLoop(poll_type))
, server_(server)
, thread_()
{
    
}

void TestLoop::cleanup()
{
    std::lock_guard<std::mutex> lg(obj_mutex_);
    for (auto &kv : obj_map_) {
        kv.second->close();
        delete kv.second;
    }
    obj_map_.clear();
}

bool TestLoop::init()
{
    try {
        thread_ = std::thread([this] {
            run();
        });
    }
    catch(...)
    {
        return false;
    }
    return true;
}

void TestLoop::stop()
{
    //cleanup();
    if(loop_) {
        loop_->runInEventLoop([this] { cleanup(); });
        loop_->stop();
    }
    if(thread_.joinable()) {
        try {
            thread_.join();
        } catch (...) {
            printf("failed to join loop thread\n");
        }
    }
}

void TestLoop::run()
{
    if(!loop_->init()) {
        printf("TestLoop::run, failed to init EventLoop\n");
        return;
    }
    loop_->loop();
}

void TestLoop::addFd(SOCKET_FD fd, Proto proto)
{
    loop_->runInEventLoop([=] {
        switch (proto) {
            case PROTO_TCP:
            {
                long conn_id = server_->getConnId();
                Connection* conn = new Connection(loop_, conn_id, this);
                addObject(conn_id, conn);
                conn->attachFd(fd);
                break;
            }
            case PROTO_HTTP:
            case PROTO_HTTPS:
            {
                long conn_id = server_->getConnId();
                HttpTest* http = new HttpTest(loop_, conn_id, this);
                addObject(conn_id, http);
                http->attachFd(fd, proto==PROTO_HTTPS?FLAG_HAS_SSL:0);
                break;
            }
            case PROTO_WS:
            case PROTO_WSS:
            {
                long conn_id = server_->getConnId();
                WsTest* ws = new WsTest(loop_, conn_id, this);
                addObject(conn_id, ws);
                ws->attachFd(fd, proto==PROTO_WSS?FLAG_HAS_SSL:0);
                break;
            }
            case PROTO_AUTO:
            case PROTO_AUTOS:
            {
                long conn_id = server_->getConnId();
                AutoHelper* helper = new AutoHelper(loop_, conn_id, this);
                addObject(conn_id, helper);
                helper->attachFd(fd, proto==PROTO_AUTOS?FLAG_HAS_SSL:0);
                break;
            }
                
            default:
                break;
        }
    });
}

#ifdef KUMA_OS_WIN
# define strcasecmp _stricmp
#endif

void TestLoop::addTcp(TcpSocket &tcp, HttpParser&& parser, uint32_t flags)
{
    if (strcasecmp(parser.getHeaderValue("Upgrade"), "WebSocket") == 0 &&
        strcasecmp(parser.getHeaderValue("Connection"), "Upgrade") == 0) {
        long conn_id = server_->getConnId();
        WsTest* ws = new WsTest(loop_, conn_id, this);
        addObject(conn_id, ws);
        ws->attachTcp(tcp, std::move(parser), flags);
        return;
    } else {
        long conn_id = server_->getConnId();
        HttpTest* http = new HttpTest(loop_, conn_id, this);
        addObject(conn_id, http);
        http->attachTcp(tcp, std::move(parser), flags);
    }
}

void TestLoop::addObject(long conn_id, LoopObject* obj)
{
    std::lock_guard<std::mutex> lg(obj_mutex_);
    obj_map_.insert(std::make_pair(conn_id, obj));
}

void TestLoop::removeObject(long conn_id)
{
    printf("TestLoop::removeObject, conn_id=%ld\n", conn_id);
    std::lock_guard<std::mutex> lg(obj_mutex_);
    auto it = obj_map_.find(conn_id);
    if(it != obj_map_.end()) {
        delete it->second;
        obj_map_.erase(it);
    }
}

