/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __EventLoopImpl_H__
#define __EventLoopImpl_H__

#include "kmapi.h"
#include "evdefs.h"
#include "util/kmqueue.h"
#include "TimerManager.h"
#include "util/kmobject.h"

#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#endif
#include <stdint.h>
#include <thread>
#include <list>

KUMA_NS_BEGIN

class IOPoll;

class EventLoop::Impl : public KMObject
{
public:
    class Listener
    {
    public:
        virtual ~Listener() {}
        virtual void loopStopped() = 0;
    };
    
public:
    Impl(PollType poll_type = PollType::NONE);
    ~Impl();

public:
    bool init();
    KMError registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb);
    KMError updateFd(SOCKET_FD fd, uint32_t events);
    KMError unregisterFd(SOCKET_FD fd, bool close_fd);
    TimerManagerPtr getTimerMgr() { return timer_mgr_; }
    
    void addListener(Listener *l);
    void removeListener(Listener *l);
    
    PollType getPollType() const;
    bool isPollLT() const; // level trigger
    
public:
    bool isInEventLoopThread() const { return std::this_thread::get_id() == thread_id_; }
    KMError runInEventLoop(LoopCallback cb);
    KMError runInEventLoopSync(LoopCallback cb);
    KMError queueInEventLoop(LoopCallback cb);
    void loopOnce(uint32_t max_wait_ms);
    void loop(uint32_t max_wait_ms = -1);
    void notify();
    void stop();
    bool stopped() const { return stop_loop_; }
    
private:
    using CallbackQueue = KM_QueueMT<LoopCallback, std::mutex>;
    
    IOPoll*         poll_;
    bool            stop_loop_{ false };
    std::thread::id thread_id_;
    
    CallbackQueue   cb_queue_;
    
    TimerManagerPtr timer_mgr_;
    
    std::list<Listener*> listeners_;
};

KUMA_NS_END

#endif
