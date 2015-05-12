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
#include "util/kmtimer.h"

#include <stdint.h>
#include <thread>

KUMA_NS_BEGIN

class IOPoll;

class EventLoop
{
public:
    EventLoop(uint32_t max_wait_time_ms = -1);
    ~EventLoop();

public:
    bool init();
    int registerFd(int fd, uint32_t events, IOCallback& cb);
    int unregisterFd(int fd, bool close_fd);
    TimerManagerPtr getTimerMgr() { return timer_mgr_; }
    
public:
    bool isInEventLoopThread() { return std::this_thread::get_id() == thread_id_; }
    int runInEventLoop(LoopCallback &cb);
    int runInEventLoop(LoopCallback &&cb);
    int runInEventLoopSync(LoopCallback &cb);
    void loop();
    void stop();
    
private:
    typedef KM_QueueT<LoopCallback, KM_Mutex> CallbackQueue;
    
    IOPoll*         poll_;
    bool            stop_loop_;
    std::thread::id thread_id_;
    
    CallbackQueue   cb_queue_;
    
    uint32_t        max_wait_ms_;
    TimerManagerPtr timer_mgr_;
};

KUMA_NS_END

#endif
