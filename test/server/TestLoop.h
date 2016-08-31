#ifndef __TestLoop_H__
#define __TestLoop_H__

#include "kmapi.h"

#include <thread>
#include <mutex>
#include <map>
#include <memory>

using namespace kuma;

typedef enum {
    PROTO_TCP,
    PROTO_TCPS,
    PROTO_HTTP,
    PROTO_HTTPS,
    PROTO_WS,
    PROTO_WSS,
    PROTO_UDP,
    PROTO_AUTO,
    PROTO_AUTOS
} Proto;

class LoopObject
{
public:
    virtual ~LoopObject() {}
    virtual int close() = 0;
};

class LoopPool;
class TestLoop
{
public:
    TestLoop(LoopPool* loopPool, PollType poll_type = PollType::NONE);

    bool init();
    void stop();
    
    void addFd(SOCKET_FD fd, Proto proto);
    
    void addHttp(TcpSocket&& tcp, HttpParser&& parser);
    void addHttp2(TcpSocket&& tcp, HttpParser&& parser);
    void addWebSocket(TcpSocket&& tcp, HttpParser&& parser);
    
    void addObject(long conn_id, LoopObject* obj);
    void removeObject(long conn_id);
    
    EventLoop* getEventLoop() { return loop_.get(); }
    
private:
    void cleanup();
    void run();
    
private:
    typedef std::map<long, LoopObject*> ObjectMap;
    
    std::unique_ptr<EventLoop>  loop_;
    LoopPool*       loopPool_;
    
    std::mutex      obj_mutex_;
    ObjectMap       obj_map_;
    
    std::thread     thread_;
};

#endif
