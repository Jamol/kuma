#include "RunLoop.h"

using namespace kmsvr;

RunLoop::RunLoop() : loop_(std::make_shared<kuma::EventLoop>())
{
    
}

RunLoop::~RunLoop()
{
    stop();
}

bool RunLoop::start()
{
    thread_ = std::thread([this] {
        if (loop_->init()) {
            loop_->loop();
        }
    });
    
    return true;
}

void RunLoop::stop()
{
    loop_->async([this] {
        objMap_.clear();
        loop_->stop();
    });
    try {
        if (thread_.joinable()) {
            thread_.join();
        }
    } catch (std::exception &) {
        
    }
}

void RunLoop::addObject(uint64_t objId, LoopObject::Ptr obj)
{
    load_ += obj->getLoad();
    objMap_.emplace(objId, obj);
}

void RunLoop::removeObject(uint64_t objId)
{
    auto it = objMap_.find(objId);
    if (it != objMap_.end()) {
        load_ -= it->second->getLoad();
        objMap_.erase(it);
    }
}
