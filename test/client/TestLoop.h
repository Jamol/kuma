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
    PROTO_UDP
} Proto;

class TestObject
{
public:
    virtual ~TestObject() {}
    virtual int close() = 0;
};

class ObjectManager
{
public:
    virtual ~ObjectManager() {}
    virtual void addObject(long conn_id, TestObject* obj) = 0;
    virtual void removeObject(long conn_id) = 0;
    virtual EventLoop* getEventLoop() = 0;
};

class LoopPool;
class TestLoop : public ObjectManager
{
public:
    TestLoop(LoopPool* server, PollType poll_type = PollType::NONE);

    bool init();
    void stop();
    
    void startTest(std::string& addr_url, std::string& bind_addr);
    
    void addObject(long conn_id, TestObject* obj) override;
    void removeObject(long conn_id) override;
    EventLoop* getEventLoop() override { return loop_.get(); }
    
private:
    void cleanup();
    void run();
    
private:
    typedef std::map<long, TestObject*> ObjectMap;
    
    std::unique_ptr<EventLoop>  loop_;
    LoopPool*       server_;
    
    std::mutex      obj_mutex_;
    ObjectMap       obj_map_;
    
    std::thread     thread_;
};

#endif
