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

KUMA_NS_BEGIN

IOPoll* createEventPoll();

EventLoop::EventLoop(uint32_t max_wait_time_ms)
{
    m_stopLoop = false;
    m_poll = createEventPoll();
}

EventLoop::~EventLoop()
{
    if(m_poll) {
        delete m_poll;
        m_poll = NULL;
    }
    IEvent *ev = NULL;
    while (m_eventQueue.dequeue(ev)) {
        delete ev;
    }
}

bool EventLoop::init()
{
    if(!m_poll->init()) {
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
                handler->acquireRef();
            }
            loop = l;
        }
        
        ~RegisterIOEvent()
        {
            if(handler) {
                handler->releaseRef();
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
    while (!m_stopLoop) {
        IEvent *ev = NULL;
        while (!m_stopLoop && m_eventQueue.dequeue(ev)) {
            ev->fire();
            delete ev;
        }
        m_poll->wait(-1);
    }
}

int EventLoop::registerHandler_i(int fd, uint32_t events, IOHandler* handler)
{
    if(m_handlerMap.find(fd) != m_handlerMap.end()) {
        return KUMA_ERROR_INVALID_STATE;
    }
    int ret = m_poll->registerFD(fd, events, handler);
    if(ret != KUMA_ERROR_NOERR) {
        return ret;
    }
    handler->acquireRef();
    m_handlerMap.insert(std::make_pair(fd, handler));
    return KUMA_ERROR_NOERR;
}

int EventLoop::unregisterHandler_i(int fd, bool close_fd)
{
    m_poll->unregisterFD(fd);
    IOHandlerMap::iterator it = m_handlerMap.find(fd);
    if(it != m_handlerMap.end()) {
        it->second->releaseRef();
        m_handlerMap.erase(it);
    }
    if(close_fd) {
        closesocket(fd);
    }
    return KUMA_ERROR_NOERR;
}

int EventLoop::postEvent(IEvent* ev)
{
    m_eventQueue.enqueue(ev);
    m_poll->notify();
    return KUMA_ERROR_NOERR;
}

#ifdef KUMA_OS_WIN
IOPoll* createSelectPoll();
#elif defined(KUMA_OS_LINUX)
IOPoll* createEPoll();
#elif defined(KUMA_OS_MACOS)
IOPoll* createVPoll();
#else
IOPoll* createSelectPoll();
#endif

IOPoll* createEventPoll()
{
#ifdef KUMA_OS_WIN
    return createSelectPoll();
#elif defined(KUMA_OS_LINUX)
    return createEPoll();
#elif defined(KUMA_OS_MACOS)
    return createVPoll();
#else
    return createSelectPoll();
#endif
}

KUMA_NS_END
