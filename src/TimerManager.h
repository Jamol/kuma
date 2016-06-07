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

#ifndef __TimerManager_H__
#define __TimerManager_H__

#include "kmdefs.h"

#include <memory>
#include <mutex>

#ifndef TICK_COUNT_TYPE
# define TICK_COUNT_TYPE    unsigned long
#endif

KUMA_NS_BEGIN

#define TIMER_VECTOR_BITS   8
#define TIMER_VECTOR_SIZE   (1 << TIMER_VECTOR_BITS)
#define TIMER_VECTOR_MASK   (TIMER_VECTOR_SIZE - 1)
#define TV_COUNT            4

class TimerImpl;
class EventLoopImpl;

class TimerManager
{
public:
    TimerManager(EventLoopImpl* loop);
    ~TimerManager();

    bool scheduleTimer(TimerImpl* timer, uint32_t delay_ms, TimerMode mode);
    void cancelTimer(TimerImpl* timer);

    int checkExpire(unsigned long* remain_ms = nullptr);

public:
    class TimerNode
    {
    public:
        TimerNode()
        : cancelled_(true)
        , repeating_(false)
        , delay_ms_(0)
        , start_tick_(0)
        , timer_(nullptr)
        , tv_index_(-1)
        , tl_index_(-1)
        , prev_(nullptr)
        , next_(nullptr)
        { }
        void reset()
        {
            tv_index_ = -1;
            tl_index_ = -1;
            prev_ = nullptr;
            next_ = nullptr;
        }
        
        bool            cancelled_;
        bool            repeating_;
        uint32_t        delay_ms_;
        TICK_COUNT_TYPE start_tick_;
        TimerImpl*      timer_;
        
    protected:
        friend class TimerManager;
        int tv_index_;
        int tl_index_;
        TimerNode* prev_;
        TimerNode* next_;
    };
    
private:
    typedef enum {
        FROM_SCHEDULE,
        FROM_CASCADE,
        FROM_RESCHEDULE
    } FROM;
    bool addTimer(TimerNode* timer_node, FROM from);
    void removeTimer(TimerNode* timer_node);
    int cascadeTimer(int tv_idx, int tl_idx);
    bool isTimerPending(TimerNode* timer_node)
    {
        return timer_node->next_ != nullptr;
    }

    void list_init_head(TimerNode* head);
    void list_add_node(TimerNode* head, TimerNode* timer_node);
    void list_remove_node(TimerNode* timer_node);
    void list_replace(TimerNode* old_head, TimerNode* new_head);
    void list_combine(TimerNode* from_head, TimerNode* to_head);
    bool list_empty(TimerNode* head);
    
    void set_tv0_bitmap(int idx);
    void clear_tv0_bitmap(int idx);
    int find_first_set_in_bitmap(int idx);

private:
    typedef std::recursive_mutex KM_Mutex;
    typedef std::lock_guard<KM_Mutex> KM_Lock_Guard;
    
    EventLoopImpl* loop_;
    KM_Mutex mutex_;
    KM_Mutex running_mutex_;
    TimerNode*  running_node_;
    TimerNode*  reschedule_node_;
    unsigned long last_remain_ms_;
    TICK_COUNT_TYPE last_tick_;
    uint32_t timer_count_;
    uint32_t tv0_bitmap_[8]; // 1 -- have timer in this slot
    TimerNode tv_[TV_COUNT][TIMER_VECTOR_SIZE]; // timer vectors
};
typedef std::shared_ptr<TimerManager> TimerManagerPtr;

class TimerImpl
{
public:
    typedef std::function<void(void)> TimerCallback;
    
    TimerImpl(TimerManagerPtr mgr);
    ~TimerImpl();
    
    bool schedule(uint32_t delay_ms, const TimerCallback& cb, TimerMode mode);
    bool schedule(uint32_t delay_ms, TimerCallback&& cb, TimerMode mode);
    void cancel();
    
private:
    friend class TimerManager;
    TimerCallback cb_;
    std::weak_ptr<TimerManager> timer_mgr_;
    TimerManager::TimerNode timer_node_; // intrusive list node
};

KUMA_NS_END

#endif
