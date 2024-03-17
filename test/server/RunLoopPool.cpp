#include "RunLoopPool.h"

using namespace kmsvr;

const size_t kMaxRunLoopCount = 6;

RunLoopPool::RunLoopPool()
{
    
}

RunLoopPool::~RunLoopPool()
{
    
}

bool RunLoopPool::start(size_t count, kuma::PollType poll_type)
{
    if (count == 0) {
        count = std::thread::hardware_concurrency();
    }
    if (count > kMaxRunLoopCount) {
        count = kMaxRunLoopCount;
    }
    
    std::lock_guard<std::mutex> g(runLoopMutex_);
    for (size_t i = 0; i < count; ++i) {
        auto runLoop = std::make_shared<RunLoop>(poll_type);
        runLoop->start();
        runLoopList_.emplace_back(std::move(runLoop));
    }
    runLoopIndex_ = 0;
    
    return true;
}

void RunLoopPool::stop()
{
    std::lock_guard<std::mutex> g(runLoopMutex_);
    runLoopList_.clear();
}

RunLoop::Ptr RunLoopPool::getRunLoop()
{
    std::lock_guard<std::mutex> g(runLoopMutex_);
    if (!runLoopList_.empty()) {
        if (runLoopIndex_ >= runLoopList_.size()) {
            runLoopIndex_ = 0;
        }
        return runLoopList_[runLoopIndex_++];
    }
    
    return nullptr;
}

size_t RunLoopPool::getload()
{
    size_t load = 0;
    std::lock_guard<std::mutex> g(runLoopMutex_);
    for (auto const &loop : runLoopList_) {
        load += loop->getLoad();
    }
    
    return load;
}
