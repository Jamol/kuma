/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "EventLoopImpl.h"
#include "poll/IOPoll.h"
#include "util/kmqueue.h"
#include "util/kmtrace.h"
#include <thread>
#include <condition_variable>

KUMA_NS_BEGIN

IOPoll* createIOPoll(PollType poll_type);

EventLoopImpl::EventLoopImpl(PollType poll_type)
: poll_(createIOPoll(poll_type))
, stop_loop_(false)
, thread_id_()
, timer_mgr_(new TimerManager(this))
{
    
}

EventLoopImpl::~EventLoopImpl()
{
    if(poll_) {
        delete poll_;
        poll_ = nullptr;
    }
}

bool EventLoopImpl::init()
{
    if(!poll_->init()) {
        return false;
    }
    stop_loop_ = false;
    thread_id_ = std::this_thread::get_id();
    return true;
}

PollType EventLoopImpl::getPollType() const
{
    if(poll_) {
        return poll_->getType();
    }
    return POLL_TYPE_NONE;
}

bool EventLoopImpl::isPollLT() const
{
    if(poll_) {
        return poll_->isLevelTriggered();
    }
    return false;
}

int EventLoopImpl::registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb)
{
    if(isInEventLoopThread()) {
        return poll_->registerFd(fd, events, std::move(cb));
    }
    return runInEventLoop([=] () mutable {
        int ret = poll_->registerFd(fd, events, cb);
        if(ret != KUMA_ERROR_NOERR) {
            return ;
        }
    });
}

int EventLoopImpl::updateFd(SOCKET_FD fd, uint32_t events)
{
    if(isInEventLoopThread()) {
        return poll_->updateFd(fd, events);
    }
    return runInEventLoop([=] {
        int ret = poll_->updateFd(fd, events);
        if(ret != KUMA_ERROR_NOERR) {
            return ;
        }
    });
}

int EventLoopImpl::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    if(isInEventLoopThread()) {
        int ret = poll_->unregisterFd(fd);
        if(close_fd) {
            closeFd(fd);
        }
        return ret;
    } else {
        int ret = runInEventLoopSync([=] {
            poll_->unregisterFd(fd);
            if(close_fd) {
                closeFd(fd);
            }
        });
        if(KUMA_ERROR_NOERR != ret) {
            return ret;
        }
        return KUMA_ERROR_NOERR;
    }
}

int EventLoopImpl::registerObject(ILoopObject *obj)
{
    objects_.push_back(obj);
    return KUMA_ERROR_NOERR;
}

int EventLoopImpl::unregisterObject(ILoopObject *obj)
{
    for (auto it = objects_.begin(); it != objects_.end(); ++it) {
        if (*it == obj) {
            objects_.erase(it);
            break;
        }
    }
    return KUMA_ERROR_NOERR;
}

void EventLoopImpl::loopOnce(uint32_t max_wait_ms)
{
    LoopCallback cb;
    while (cb_queue_.dequeue(cb)) {
        if(cb) {
            cb();
        }
    }
    unsigned long wait_ms = max_wait_ms;
    timer_mgr_->checkExpire(&wait_ms);
    if(wait_ms > max_wait_ms) {
        wait_ms = max_wait_ms;
    }
    poll_->wait((uint32_t)wait_ms);
}

void EventLoopImpl::loop(uint32_t max_wait_ms)
{
    while (!stop_loop_) {
        loopOnce(max_wait_ms);
    }
    LoopCallback cb;
    while (cb_queue_.dequeue(cb)) {
        if (cb) {
            cb();
        }
    }
    for (auto obj : objects_) {
        obj->notifyLoopStopped();
    }
    objects_.clear();
    KUMA_INFOTRACE("EventLoop::loop, stopped");
}

void EventLoopImpl::notify()
{
    poll_->notify();
}

void EventLoopImpl::stop()
{
    KUMA_INFOTRACE("EventLoop::stop");
    stop_loop_ = true;
    poll_->notify();
}

int EventLoopImpl::runInEventLoop(LoopCallback cb)
{
    if(isInEventLoopThread()) {
        cb();
    } else {
        cb_queue_.enqueue(std::move(cb));
        poll_->notify();
    }
    return KUMA_ERROR_NOERR;
}

int EventLoopImpl::runInEventLoopSync(LoopCallback cb)
{
    if(isInEventLoopThread()) {
        cb();
    } else {
        std::mutex m;
        std::condition_variable cv;
        bool ready = false;
        LoopCallback cb_sync([&] {
            cb();
            std::unique_lock<std::mutex> lk(m);
            ready = true;
            lk.unlock();
            cv.notify_one();
        });
        cb_queue_.enqueue(std::move(cb_sync));
        poll_->notify();
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&ready] { return ready; });
    }
    return KUMA_ERROR_NOERR;
}

int EventLoopImpl::queueInEventLoop(LoopCallback cb)
{
    cb_queue_.enqueue(std::move(cb));
    if(!isInEventLoopThread()) {
        poll_->notify();
    }
    return KUMA_ERROR_NOERR;
}

IOPoll* createEPoll();
IOPoll* createVPoll();
IOPoll* createSelectPoll();

IOPoll* createDefaultIOPoll()
{
#ifdef KUMA_OS_WIN
    return createSelectPoll();
#elif defined(KUMA_OS_LINUX)
    return createEPoll();
#elif defined(KUMA_OS_MAC)
    return createVPoll();
#else
    return createSelectPoll();
#endif
}

IOPoll* createIOPoll(PollType poll_type)
{
    switch (poll_type)
    {
        case POLL_TYPE_POLL:
            return createVPoll();
        case POLL_TYPE_SELECT:
            return createSelectPoll();
        case POLL_TYPE_EPOLL:
#ifdef KUMA_OS_LINUX
            return createEPoll();
#endif
        default:
            return createDefaultIOPoll();
    }
}

KUMA_NS_END
