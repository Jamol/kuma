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

#include "EventLoop.h"
#include "poll/IOPoll.h"
#include "util/kmqueue.h"
#include "util/TimerManager.h"
#include <thread>
#include <condition_variable>

KUMA_NS_BEGIN

IOPoll* createIOPoll();

EventLoop::EventLoop(uint32_t max_wait_ms)
: poll_(createIOPoll())
, stop_loop_(false)
, thread_id_()
, max_wait_ms_(max_wait_ms)
, timer_mgr_(new TimerManager())
{
    
}

EventLoop::~EventLoop()
{
    if(poll_) {
        delete poll_;
        poll_ = nullptr;
    }
}

bool EventLoop::init()
{
    if(!poll_->init()) {
        return false;
    }
    thread_id_ = std::this_thread::get_id();
    return true;
}

PollType EventLoop::getPollType()
{
    if(poll_) {
        return poll_->getType();
    }
    return POLL_TYPE_NONE;
}

int EventLoop::registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb)
{
    if(isInEventLoopThread()) {
        return poll_->registerFd(fd, events, cb);
    }
    LoopCallback ev([=, &cb] {
        int ret = poll_->registerFd(fd, events, cb);
        if(ret != KUMA_ERROR_NOERR) {
            return ;
        }
    });
    return runInEventLoop(std::move(ev));
}

int EventLoop::registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb)
{
    return registerFd(fd, events, cb);
}

int EventLoop::updateFd(SOCKET_FD fd, uint32_t events)
{
    if(isInEventLoopThread()) {
        return poll_->updateFd(fd, events);
    }
    LoopCallback ev([=] {
        int ret = poll_->updateFd(fd, events);
        if(ret != KUMA_ERROR_NOERR) {
            return ;
        }
    });
    return runInEventLoop(std::move(ev));
}

int EventLoop::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    if(isInEventLoopThread()) {
        return poll_->unregisterFd(fd);
    } else {
        LoopCallback ev([=] {
            poll_->unregisterFd(fd);
            if(close_fd) {
                closeFd(fd);
            }
        });
        int ret = runInEventLoopSync(ev);
        if(KUMA_ERROR_NOERR != ret) {
            return ret;
        }
        return KUMA_ERROR_NOERR;
    }
}

void EventLoop::loop()
{
    while (!stop_loop_) {
        LoopCallback cb;
        while (!stop_loop_ && cb_queue_.dequeue(cb)) {
            if(cb) {
                cb();
            }
        }
        unsigned long wait_ms = max_wait_ms_;
        timer_mgr_->checkExpire(&wait_ms);
        if(wait_ms > max_wait_ms_) {
            wait_ms = max_wait_ms_;
        }
        poll_->wait((uint32_t)wait_ms);
    }
}

void EventLoop::stop()
{
    stop_loop_ = true;
}

int EventLoop::runInEventLoop(LoopCallback& cb)
{
    if(isInEventLoopThread()) {
        cb();
    } else {
        cb_queue_.enqueue(cb);
        poll_->notify();
    }
    return KUMA_ERROR_NOERR;
}

int EventLoop::runInEventLoop(LoopCallback&& cb)
{
    if(isInEventLoopThread()) {
        cb();
    } else {
        cb_queue_.enqueue(std::move(cb));
        poll_->notify();
    }
    return KUMA_ERROR_NOERR;
}

int EventLoop::runInEventLoopSync(LoopCallback& cb)
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

#if defined(KUMA_OS_LINUX)
IOPoll* createEPoll();
#elif defined(KUMA_OS_MAC)
IOPoll* createVPoll();
#else
IOPoll* createSelectPoll();
#endif

IOPoll* createIOPoll()
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

KUMA_NS_END
