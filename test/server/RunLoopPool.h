#pragma once

#include "RunLoop.h"

#include <vector>
#include <mutex>

namespace kmsvr {

class RunLoopPool
{
public:
    RunLoopPool();
    ~RunLoopPool();
    
    bool start(size_t count, kuma::PollType poll_type);
    void stop();
    
    RunLoop::Ptr getRunLoop();
    size_t getload();
    
private:
    std::vector<RunLoop::Ptr>   runLoopList_;
    std::mutex                  runLoopMutex_;
    size_t                      runLoopIndex_{0};
};

}
