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
#include "internal.h"
#include "util/refcount.h"
#include "util/kmdp.h"
#include "util/kmthread.h"
#include "util/kmmutex.h"
#include "util/kmqueue.h"
#include "util/kmtimer.h"

KUMA_NS_BEGIN

IOPoll* createIOPoll();

EventLoop::EventLoop(uint32_t max_wait_time_ms)
{
    stopLoop_ = false;
    poll_ = createIOPoll();
    max_wait_time_ms_ = max_wait_time_ms;
    timer_mgr_ = new KM_Timer_Manager();
}

EventLoop::~EventLoop()
{
    if(poll_) {
        delete poll_;
        poll_ = NULL;
    }
    IEvent *ev = NULL;
    while (eventQueue_.dequeue(ev)) {
        delete ev;
    }
    delete timer_mgr_;
    timer_mgr_ = NULL;
}

bool EventLoop::init()
{
    if(!poll_->init()) {
        return false;
    }
    return true;
}

int EventLoop::registerHandler(int fd, uint32_t events, IOHandler* handler)
{
    class RegisterIOEvent : public IEvent
    {
    public:
        RegisterIOEvent(int f, uint32_t e, IOHandler* h, EventLoop* l)
        {
            fd = f;
            events = e;
            handler = h;
            if(handler) {
                handler->acquireReference();
            }
            loop = l;
        }
        
        ~RegisterIOEvent()
        {
            if(handler) {
                handler->releaseReference();
                handler = NULL;
            }
        }
        
        virtual void fire()
        {
            loop->registerHandler_i(fd, events, handler);
        }
        
        int fd;
        uint32_t events;
        IOHandler* handler;
        EventLoop* loop;
    };
    RegisterIOEvent* e = new RegisterIOEvent(fd, events, handler, this);
    postEvent(e);
    return KUMA_ERROR_NOERR;
}

int EventLoop::unregisterHandler(int fd, bool close_fd)
{
    class UnregisterIOEvent : public IEvent
    {
    public:
        UnregisterIOEvent(int f, bool c, EventLoop* l)
        {
            fd = f;
            close_fd = c;
            loop = l;
        }
        
        virtual void fire()
        {
            loop->unregisterHandler_i(fd, close_fd);
        }
        
        int fd;
        bool close_fd;
        EventLoop* loop;
    };
    UnregisterIOEvent* e = new UnregisterIOEvent(fd, close_fd, this);
    postEvent(e);
    return KUMA_ERROR_NOERR;
}

void EventLoop::loop()
{
    while (!stopLoop_) {
        IEvent *ev = NULL;
        while (!stopLoop_ && eventQueue_.dequeue(ev)) {
            ev->fire();
            delete ev;
        }
        unsigned long remain_time_ms = max_wait_time_ms_;
        timer_mgr_->check_expire(&remain_time_ms);
        if(remain_time_ms > max_wait_time_ms_) {
            remain_time_ms = max_wait_time_ms_;
        }
        poll_->wait((uint32_t)remain_time_ms);
    }
}

int EventLoop::registerHandler_i(int fd, uint32_t events, IOHandler* handler)
{
    if(handlerMap_.find(fd) != handlerMap_.end()) {
        return KUMA_ERROR_INVALID_STATE;
    }
    int ret = poll_->register_fd(fd, events, handler);
    if(ret != KUMA_ERROR_NOERR) {
        return ret;
    }
    handler->acquireReference();
    handlerMap_.insert(std::make_pair(fd, handler));
    return KUMA_ERROR_NOERR;
}

int EventLoop::unregisterHandler_i(int fd, bool close_fd)
{
    poll_->unregister_fd(fd);
    IOHandlerMap::iterator it = handlerMap_.find(fd);
    if(it != handlerMap_.end()) {
        it->second->releaseReference();
        handlerMap_.erase(it);
    }
    if(close_fd) {
        closesocket(fd);
    }
    return KUMA_ERROR_NOERR;
}

int EventLoop::postEvent(IEvent* ev)
{
    eventQueue_.enqueue(ev);
    poll_->notify();
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
