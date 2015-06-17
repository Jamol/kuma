#include "LoopPool.h"
#include "TestLoop.h"

LoopPool::LoopPool()
: next_loop_(0)
//, id_seed_(0) fuuuck MSVC
{
    id_seed_ = 0;
}

void LoopPool::cleanup()
{
    
}

TestLoop* LoopPool::getNextLoop()
{
    TestLoop* loop = loops_[next_loop_];
    if(++next_loop_ >= loops_.size()) {
        next_loop_ = 0;
    }
    return loop;
}

bool LoopPool::init(int count, PollType poll_type)
{
    for (int i=0; i < count; ++i) {
        TestLoop* l = new TestLoop(this);
        loops_.push_back(l);
        l->init();
    }
    return true;
}

void LoopPool::stop()
{
    for (auto loop : loops_) {
        loop->stop();
        delete loop;
    }
    loops_.clear();
}
