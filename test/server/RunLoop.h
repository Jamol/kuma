#pragma once

#include "kmapi.h"

#include <thread>
#include <memory>
#include <atomic>
#include <unordered_map>

namespace kmsvr {

using EventLoopPtr = std::shared_ptr<kuma::EventLoop>;

class LoopObject
{
public:
    using Ptr = std::shared_ptr<LoopObject>;
    LoopObject()
    {
        static std::atomic<uint64_t> s_objIdSeed{0};
        objId_ = ++s_objIdSeed;
    }
    virtual ~LoopObject() {}
    virtual size_t getLoad() const { return 1; }
    uint64_t getObjectId() const { return objId_; }

private:
    uint64_t objId_{0};
};


class RunLoop
{
public:
    using Ptr = std::shared_ptr<RunLoop>;
    using WPtr = std::weak_ptr<RunLoop>;
    
    RunLoop(kuma::PollType poll_type);
    ~RunLoop();
    
    bool start();
    void stop();
    
    EventLoopPtr getEventLoop() const { return loop_; }

    void addObject(uint64_t objId, LoopObject::Ptr obj);
    void removeObject(uint64_t objId);
    size_t getLoad() const { return load_; }
    
private:
    EventLoopPtr        loop_;
    std::thread         thread_;
    std::atomic<size_t> load_{0};

    using ObjectMap = std::unordered_map<uint64_t, LoopObject::Ptr>;
    ObjectMap           objMap_;
};

}
