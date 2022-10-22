#ifndef __TestLoop_H__
#define __TestLoop_H__

#include "kmapi.h"

#include <thread>
#include <mutex>
#include <map>
#include <memory>

using namespace kuma;

#ifdef KUMA_OS_WIN
# define getLastError()  ::WSAGetLastError()
#else
# define getLastError()  errno
#endif

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
    virtual EventLoop* eventLoop() = 0;
};

class LoopPool;
class TestLoop : public ObjectManager
{
public:
    TestLoop(LoopPool* server, PollType poll_type = PollType::DEFAULT);

    bool init();
    void stop();
    
    void startTest(const std::string& addr_url, const std::string& bind_addr);
    
    void addObject(long conn_id, TestObject* obj) override;
    void removeObject(long conn_id) override;
    EventLoop* eventLoop() override { return loop_.get(); }
    
private:
    void startTest_i(const std::string& addr_url, const std::string& bind_addr);
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
