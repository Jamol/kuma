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

#include "util/kmqueue.h"
#include <map>
#include <stdint.h>

KUMA_NS_BEGIN

class TimerHandler;
class KM_Timer_Manager;

class IOPoll;

class EventLoop
{
public:
    EventLoop(uint32_t max_wait_time_ms = -1);
    ~EventLoop();

public:
    bool init();
    int registerIOCallback(int fd, uint32_t events, IOCallback& cb);
    int unregisterIOCallback(int fd, bool close_fd);
    KM_Timer_Manager* getTimerMgr() { return timer_mgr_; }
    
public:
    int runInEventLoop(EventCallback &cb);
    int runInEventLoop(EventCallback &&cb);
    void loop();
    void stop();
    
private:
    typedef KM_QueueT<EventCallback, KM_Mutex> EventQueue;
    
    IOPoll*         poll_;
    bool            stopLoop_;
    
    KM_Mutex        mutex_;
    EventQueue      eventQueue_;
    
    uint32_t        max_wait_time_ms_;
    KM_Timer_Manager* timer_mgr_;
};

KUMA_NS_END

#endif
