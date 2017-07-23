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
class EventLoopToken;

class TaskSlot
{
public:
    enum class State
    {
        ACTIVE,
        RUNNING,
        INACTIVE,
    };
    TaskSlot(EventLoop::Task &&t, EventLoopToken *token)
    : task(std::move(t)), token(token) {}
    void operator() ()
    {
        if (task) {
            task();
        }
    }
    EventLoop::Task task;
    State state = State::ACTIVE;
    EventLoopToken* token;
};
using TaskQueue = DL_Queue<TaskSlot>;
using TaskNodePtr = TaskQueue::NodePtr;

enum class LoopActivity {
    EXIT,
};
using ObserverCallback = std::function<void(LoopActivity)>;
using ObserverToken = std::weak_ptr<DL_Queue<ObserverCallback>::DLNode>;

class PendingObject
{
public:
    virtual ~PendingObject() {}
    virtual bool isPending() const = 0;

public:
    PendingObject* next_ = nullptr;
    PendingObject* prev_ = nullptr;
};

class EventLoop::Impl final : public KMObject
{
public:
    Impl(PollType poll_type = PollType::NONE);
    ~Impl();

public:
    bool init();
    KMError registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb);
    KMError updateFd(SOCKET_FD fd, uint32_t events);
    KMError unregisterFd(SOCKET_FD fd, bool close_fd);
    TimerManagerPtr getTimerMgr() { return timer_mgr_; }
    
    PollType getPollType() const;
    bool isPollLT() const; // level trigger
    
    KMError appendObserver(ObserverCallback cb, EventLoopToken *token);
    KMError removeObserver(EventLoopToken *token);
    
public:
    bool inSameThread() const { return std::this_thread::get_id() == thread_id_; }
    std::thread::id threadId() const { return thread_id_; }
    KMError appendTask(Task task, EventLoopToken *token);
    KMError removeTask(EventLoopToken *token);
    KMError sync(Task task);
    KMError async(Task task, EventLoopToken *token=nullptr);
    KMError post(Task task, EventLoopToken *token=nullptr);
    void loopOnce(uint32_t max_wait_ms);
    void loop(uint32_t max_wait_ms = -1);
    void notify();
    void stop();
    bool stopped() const { return stop_loop_; }

    void appendPendingObject(PendingObject *obj);
    void removePendingObject(PendingObject *obj);

protected:
    void processTasks();
    
protected:
    using ObserverQueue = DL_Queue<ObserverCallback>;
    using LockType = std::mutex;
    using LockGuard = std::lock_guard<LockType>;
    
    IOPoll*             poll_;
    bool                stop_loop_{ false };
    std::thread::id     thread_id_;
    
    TaskQueue           task_queue_;
    LockType            task_mutex_;
    LockType            task_run_mutex_;
    
    ObserverQueue       obs_queue_;
    LockType            obs_mutex_;
    
    TimerManagerPtr     timer_mgr_;

    PendingObject*      pending_objects_ = nullptr;
};
using EventLoopPtr = std::shared_ptr<EventLoop::Impl>;
using EventLoopWeakPtr = std::weak_ptr<EventLoop::Impl>;

class EventLoopToken
{
public:
    EventLoopToken();
    ~EventLoopToken();
    
    void eventLoop(const EventLoopPtr &loop);
    EventLoopPtr eventLoop();
    
    void appendTaskNode(TaskNodePtr &node);
    void removeTaskNode(TaskNodePtr &node);
    
    bool expired();
    void reset();
    
protected:
    friend class EventLoop::Impl;
    EventLoopWeakPtr loop_;
    
    std::list<TaskNodePtr> node_queue_;
    bool observed = false;
    ObserverToken obs_token_;
};

KUMA_NS_END

#endif
