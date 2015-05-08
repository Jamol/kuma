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

#ifndef __KMTIMER_H__
#define __KMTIMER_H__

#include "kmdefs.h"
#include <map>

#ifndef TICK_COUNT_TYPE
# define TICK_COUNT_TYPE    unsigned long
#endif

KUMA_NS_BEGIN

#define TIMER_VECTOR_BITS   8
#define TIMER_VECTOR_SIZE   (1 << TIMER_VECTOR_BITS)
#define TIMER_VECTOR_MASK   (TIMER_VECTOR_SIZE - 1)
#define TV_COUNT            4

class KM_Timer;

class KM_Timer_Manager
{
public:
    KM_Timer_Manager();
    ~KM_Timer_Manager();

    bool schedule_timer(KM_Timer* timer, unsigned int time_elapse, bool repeat);
    void cancel_timer(KM_Timer* timer);

    int check_expire(unsigned long* remain_time_ms = NULL);

public:
    class KM_Timer_Node
    {
    public:
        KM_Timer_Node()
        : cancelled_(true)
        , repeat_(false)
        , elapse_(0)
        , start_tick_(0)
        , timer_(NULL)
        , tv_index_(-1)
        , tl_index_(-1)
        { }
        void reset()
        {
            tv_index_ = -1;
            tl_index_ = -1;
            prev_ = NULL;
            next_ = NULL;
        }
        
        bool            cancelled_;
        bool            repeat_;
        unsigned int	elapse_;
        TICK_COUNT_TYPE start_tick_;
        KM_Timer*	    timer_;
        
    protected:
        friend class KM_Timer_Manager;
        int tv_index_;
        int tl_index_;
        KM_Timer_Node* prev_;
        KM_Timer_Node* next_;
    };
    
private:
    bool add_timer(KM_Timer_Node* timer_node, bool from_schedule=false);
    void remove_timer(KM_Timer_Node* timer_node);
    int cascade_timer(int tv_idx, int tl_idx);
    bool timer_pending(KM_Timer_Node* timer_node)
    {
        return timer_node->next_ != NULL;
    }

    void list_init_head(KM_Timer_Node* head);
    void list_add_node(KM_Timer_Node* head, KM_Timer_Node* timer_node);
    void list_remove_node(KM_Timer_Node* timer_node);
    void list_replace(KM_Timer_Node* old_head, KM_Timer_Node* new_head);
    void list_combine(KM_Timer_Node* from_head, KM_Timer_Node* to_head);
    bool list_empty(KM_Timer_Node* head);
    
    void set_tv0_bitmap(int idx);
    void clear_tv0_bitmap(int idx);
    int find_first_set_in_bitmap(int idx);

private:
    KM_Mutex mutex_;
    KM_Mutex running_mutex_;
    KM_Timer_Node*  running_node_;
    KM_Timer_Node*  reschedule_node_;
    TICK_COUNT_TYPE last_tick_;
    unsigned int timer_count_;
    unsigned int tv0_bitmap_[8]; // 1 -- have timer in this slot
    KM_Timer_Node tv_[TV_COUNT][TIMER_VECTOR_SIZE]; // timer vectors
};

class KM_Timer
{
public:
    KM_Timer(KM_Timer_Manager* mgr, TimerCallback& cb);
    KM_Timer(KM_Timer_Manager* mgr, TimerCallback&& cb);
    ~KM_Timer();
    
    bool schedule(unsigned int time_elapse, bool repeat = false);
    void cancel();
    void on_detach();
    
private:
    friend class KM_Timer_Manager;
    TimerCallback cb_;
    KM_Timer_Manager* timer_mgr_;
    KM_Timer_Manager::KM_Timer_Node timer_node_; // intrusive list node
};

KUMA_NS_END

#endif
