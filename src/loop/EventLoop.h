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

#ifndef __KUMA_EVENTLOOP_H__
#define __KUMA_EVENTLOOP_H__
#include "kuma.h"
#include "util/kmmutex.h"
#include "util/kmqueue.h"
#include <map>
#include <stdint.h>

KUMA_NS_BEGIN

class TimerHandler;
class KM_Timer;
class KM_Timer_Manager;

class IOPoll;
class IEvent;
typedef KM_QueueT<IEvent*, KM_Mutex> EventQueue;
typedef std::map<int, IOHandler*> IOHandlerMap;

class EventLoop
{
public:
    EventLoop(uint32_t max_wait_time_ms = -1);
    ~EventLoop();

public:
    bool init();
    int registerHandler(int fd, uint32_t events, IOHandler* handler);
    int unregisterHandler(int fd, bool close_fd);
    KM_Timer* createTimer(TimerHandler* handler);
    void deleteTimer(KM_Timer* timer);
    
public:
    int postEvent(IEvent* ev);
    void loop();
    void stop();
    
protected:
    int registerHandler_i(int fd, uint32_t events, IOHandler* handler);
    int unregisterHandler_i(int fd, bool close_fd);
    
private:
    IOPoll*         m_poll;
    IOHandlerMap    m_handlerMap;
    bool            m_stopLoop;
    
    KM_Mutex        m_mutex;
    EventQueue      m_eventQueue;
    
    uint32_t        m_max_wait_time_ms;
    KM_Timer_Manager* m_timer_mgr;
};

KUMA_NS_END

#endif
