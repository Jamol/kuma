#include "LoopPool.h"
#include "TestLoop.h"

#include <string>

LoopPool::LoopPool()
: next_loop_(0)
//, id_seed_(0)
{
    id_seed_ = 0;
}

void LoopPool::cleanup()
{
    for (auto loop : loops_) {
        loop->stop();
        delete loop;
    }
    loops_.clear();
}

TestLoop* LoopPool::getNextLoop()
{
    TestLoop* loop = loops_[next_loop_];
    if(++next_loop_ >= (int)loops_.size()) {
        next_loop_ = 0;
    }
    return loop;
}

bool LoopPool::init(int count, PollType poll_type)
{
    for (int i=0; i < count; ++i) {
        TestLoop* l = new TestLoop(this, poll_type);
        loops_.push_back(l);
        l->init();
    }
    return true;
}

void LoopPool::startTest(std::string& addr_url, std::string& bind_addr, int concurrent)
{
    auto loop_size = loops_.size();
    for (int j = 0; j < concurrent; ++j) {
        TestLoop* l = loops_[j%loop_size];
        l->startTest(addr_url, bind_addr);
    }
}

void LoopPool::stop()
{
    cleanup();
}
