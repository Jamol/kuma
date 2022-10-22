#ifndef __LoopPool_H__
#define __LoopPool_H__

#include "kmapi.h"

#include <string>
#include <thread>
#include <vector>
#include <map>
#include <atomic>

using namespace kuma;

class TestLoop;
class LoopPool
{
public:
    LoopPool();

    bool init(int count, PollType poll_type = PollType::DEFAULT);
    void startTest(std::string& addr_url, std::string& bind_addr, int concurrent);
    void stop();
    
    long getConnId() { return ++id_seed_; }
    TestLoop* getNextLoop();
    
private:
    void cleanup();
    
private:
    std::vector<TestLoop*> loops_;
    int             next_loop_;
    std::atomic_long id_seed_;
};

#endif
