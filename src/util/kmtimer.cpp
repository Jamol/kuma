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

#include "kmtimer.h"

KUMA_NS_BEGIN

int find_first_set(unsigned int b);
TICK_COUNT_TYPE get_tick_count_ms();
TICK_COUNT_TYPE calc_time_elapse_delta_ms(TICK_COUNT_TYPE now_tick, TICK_COUNT_TYPE& start_tick);

//////////////////////////////////////////////////////////////////////////
// KM_Timer

KM_Timer::KM_Timer(KM_Timer_Manager* mgr)
: timer_mgr_(mgr)
{
    timer_node_.timer_ = this;
}

KM_Timer::~KM_Timer()
{
    cancel();
}

bool KM_Timer::schedule(unsigned int time_elapse, bool repeat)
{
    if(timer_mgr_) {
        return timer_mgr_->schedule_timer(this, time_elapse, repeat);
    }
    return false;
}

void KM_Timer::cancel()
{
    if(timer_mgr_) {
        timer_mgr_->cancel_timer(this);
    }
}

void KM_Timer::on_detach()
{
    timer_mgr_ = NULL; // may thread conflict
}

//////////////////////////////////////////////////////////////////////////
// KM_Timer_Manager
KM_Timer_Manager::KM_Timer_Manager()
{
    running_node_ = NULL;
    reschedule_node_ = NULL;
    timer_count_ = 0;
    last_tick_ = 0;
    memset(&tv0_bitmap_, 0, sizeof(tv0_bitmap_));
    for (int i=0; i<TV_COUNT; ++i)
    {
        for (int j=0; j<TIMER_VECTOR_SIZE; ++j)
        {
            list_init_head(&tv_[i][j]);
        }
    }
}

KM_Timer_Manager::~KM_Timer_Manager()
{
    KM_Timer_Node* head = NULL;
    KM_Timer_Node* node = NULL;
    KM_Lock_Guard g(mutex_);
    for(int i=0; i<TV_COUNT; ++i)
    {
        for (int j=0; j<TIMER_VECTOR_SIZE; ++j)
        {
            head = &tv_[i][j];
            node = head->next_;
            while(node != head)
            {
                if(node->timer_) {
                    node->timer_->on_detach();
                }
                node = node->next_;
            }
        }
    }
}

bool KM_Timer_Manager::schedule_timer(KM_Timer* timer, unsigned int time_elapse, bool repeat)
{
    KM_Timer_Node* timer_node = &timer->timer_node_;
    if(timer_pending(timer_node) && time_elapse == timer_node->elapse_) {
        return true;
    }
    TICK_COUNT_TYPE now_tick = get_tick_count_ms();
    timer_node->cancelled_ = false;
    KM_Lock_Guard g(mutex_);
    if(timer_pending(timer_node)) {
        remove_timer(timer_node);
    }
    timer_node->start_tick_ = now_tick;
    timer_node->elapse_ = time_elapse;
    timer_node->repeat_ = repeat;

    bool ret = add_timer(timer_node, true);
    if(reschedule_node_ == timer_node) {
        reschedule_node_ = NULL;
    }
    return ret;
}

void KM_Timer_Manager::cancel_timer(KM_Timer* timer)
{
    KM_Timer_Node* timer_node = &timer->timer_node_;
    if(timer_node->cancelled_) {
        return ;
    }
    timer_node->cancelled_ = true;
    bool cancelled = false;
    if(reschedule_node_ == timer_node || timer_pending(timer_node)) {
        mutex_.lock();
        if(timer_pending(timer_node)) {
            remove_timer(timer_node);
            cancelled = true;
        } else {
            if(running_node_ != timer_node) {
                cancelled = true;
            }
            if(reschedule_node_ == timer_node) {
                reschedule_node_ = NULL;
            }
        }
        mutex_.unlock();
    }
    // check timer running
    if(!cancelled && running_node_ == timer_node) { // may be running, wait it
        running_mutex_.lock();
        if(running_node_ == timer_node) {
            running_node_ = NULL;
            cancelled = true;
        }
        running_mutex_.unlock();
    }
    // check again for reschedule timer
    if(!cancelled && (reschedule_node_ == timer_node || timer_pending(timer_node))) {
        mutex_.lock();
        if(timer_pending(timer_node)) {
            remove_timer(timer_node);
        }
        if(reschedule_node_ == timer_node) {
            reschedule_node_ = NULL;
        }
        mutex_.unlock();
    }
}

void KM_Timer_Manager::list_init_head(KM_Timer_Node* head)
{
    head->next_ = head;
    head->prev_ = head;
}

void KM_Timer_Manager::list_add_node(KM_Timer_Node* head, KM_Timer_Node* timer_node)
{
    head->prev_->next_ = timer_node;
    timer_node->prev_ = head->prev_;
    timer_node->next_ = head;
    head->prev_ = timer_node;
}

void KM_Timer_Manager::list_remove_node(KM_Timer_Node* timer_node)
{
    timer_node->prev_->next_ = timer_node->next_;
    timer_node->next_->prev_ = timer_node->prev_;
    timer_node->reset();
}

void KM_Timer_Manager::list_replace(KM_Timer_Node* old_head, KM_Timer_Node* new_head)
{
    new_head->next_ = old_head->next_;
    new_head->next_->prev_ = new_head;
    new_head->prev_ = old_head->prev_;
    new_head->prev_->next_ = new_head;
    list_init_head(old_head);
}

void KM_Timer_Manager::list_combine(KM_Timer_Node* from_head, KM_Timer_Node* to_head)
{
    if(from_head->next_ == from_head) {
        return ;
    }
    to_head->prev_->next_ = from_head->next_;
    from_head->next_->prev_ = to_head->prev_;
    from_head->prev_->next_ = to_head;
    to_head->prev_ = from_head->prev_;
    list_init_head(from_head);
}

bool KM_Timer_Manager::list_empty(KM_Timer_Node* head)
{
    return head->next_ == head;
}

void KM_Timer_Manager::set_tv0_bitmap(int idx)
{
    unsigned char a = idx/(sizeof(tv0_bitmap_[0])*8);
    unsigned char b = idx%(sizeof(tv0_bitmap_[0])*8);
    tv0_bitmap_[a] |= 1 << b;
}

void KM_Timer_Manager::clear_tv0_bitmap(int idx)
{
    unsigned char a = idx/(sizeof(tv0_bitmap_[0])*8);
    unsigned char b = idx%(sizeof(tv0_bitmap_[0])*8);
    tv0_bitmap_[a] &= ~(1 << b);
}

int KM_Timer_Manager::find_first_set_in_bitmap(int idx)
{
    unsigned char a = idx/(sizeof(tv0_bitmap_[0])*8);
    unsigned char b = idx%(sizeof(tv0_bitmap_[0])*8);
    int pos = -1;
    pos = find_first_set(tv0_bitmap_[a] >> b);
    if(-1 == pos) {
        int i = a + 1;
        for (i &= 7; i != a; ++i, i &= 7) {
            pos = find_first_set(tv0_bitmap_[i]);
            if(-1 == pos) {
                continue;
            }
            if(i < a) {
                i += 8;
            }
            pos += (i - a - 1) * 32;
            pos += 32 - b;
            break;
        }
    }
    if(-1 == pos && b > 0) {
        unsigned int bits = (tv0_bitmap_[a] << (32 - b)) >> (32 - b);
        pos = find_first_set(bits);
        if(pos >= 0) {
            pos += 256 - b;
        }
    }
    return pos;
}

bool KM_Timer_Manager::add_timer(KM_Timer_Node* timer_node, bool from_schedule)
{
    if(0 == timer_count_) {
        last_tick_ = timer_node->start_tick_;
    }
    TICK_COUNT_TYPE fire_tick = timer_node->elapse_ + timer_node->start_tick_;
    if(fire_tick - last_tick_ > (((TICK_COUNT_TYPE)-1)>>1)) // time backward
    {// fire it right now
        fire_tick = last_tick_;
    }
    if(fire_tick == last_tick_) {
        ++fire_tick; // = next_jiffies
    }
    TICK_COUNT_TYPE elapse_jiffies = fire_tick - last_tick_;
    TICK_COUNT_TYPE fire_jiffies = fire_tick;
    KM_Timer_Node* head;
    if(elapse_jiffies < TIMER_VECTOR_SIZE) {
        int i = fire_jiffies & TIMER_VECTOR_MASK;
        head = &tv_[0][i];
        set_tv0_bitmap(i);
        timer_node->tv_index_ = 0;
        timer_node->tl_index_ = i;
    }
    else if(elapse_jiffies < 1 << (2*TIMER_VECTOR_BITS)) {
        int i = (fire_jiffies>>TIMER_VECTOR_BITS) & TIMER_VECTOR_MASK;
        head = &tv_[1][i];
        timer_node->tv_index_ = 1;
        timer_node->tl_index_ = i;
    }
    else if(elapse_jiffies < 1 << (3*TIMER_VECTOR_BITS)) {
        int i = (fire_jiffies>>(2*TIMER_VECTOR_BITS)) & TIMER_VECTOR_MASK;
        head = &tv_[2][i];
        timer_node->tv_index_ = 2;
        timer_node->tl_index_ = i;
    }
    else if(elapse_jiffies <= 0xFFFFFFFF) {
        int i = (fire_jiffies>>(3*TIMER_VECTOR_BITS)) & TIMER_VECTOR_MASK;
        head = &tv_[3][i];
        timer_node->tv_index_ = 3;
        timer_node->tl_index_ = i;
    }
    else
    {// don't support elapse larger than 0xffffffff
        //printf("add_timer, failed, elapse=%lu, start_tick=%lu, last_tick=%lu\n", timer_node->elapse, timer_node->start_tick, last_tick_);
        return false;
    }
    list_add_node(head, timer_node);
    if(from_schedule) {
        ++timer_count_;
    }
    return true;
}

void KM_Timer_Manager::remove_timer(KM_Timer_Node* timer_node)
{
    if(0 == timer_node->tv_index_
       && timer_node->next_ != timer_node
       && timer_node->next_ == timer_node->prev_
       && timer_node->next_ == &tv_[0][timer_node->tl_index_]) {
        clear_tv0_bitmap(timer_node->tl_index_);
    }
    list_remove_node(timer_node);
    --timer_count_;
}

int KM_Timer_Manager::cascade_timer(int tv_idx, int tl_idx)
{
    KM_Timer_Node tmp_head;
    list_init_head(&tmp_head);
    list_replace(&tv_[tv_idx][tl_idx], &tmp_head);
    KM_Timer_Node* next_node = tmp_head.next_;
    KM_Timer_Node* tmp_node = NULL;
    while(next_node != &tmp_head)
    {
        tmp_node = next_node;
        next_node = next_node->next_;
        add_timer(tmp_node);
    }

    return tl_idx;
}

#define INDEX(N) ((next_jiffies >> ((N+1) * TIMER_VECTOR_BITS)) & TIMER_VECTOR_MASK)
int KM_Timer_Manager::check_expire(unsigned long* remain_time_ms)
{
    if(0 == timer_count_) {
        return 0;
    }
    TICK_COUNT_TYPE now_tick = get_tick_count_ms();
    TICK_COUNT_TYPE delta_tick = calc_time_elapse_delta_ms(now_tick, last_tick_);
    if(0 == delta_tick) {
        return 0;
    }
    TICK_COUNT_TYPE cur_jiffies = now_tick;
    TICK_COUNT_TYPE next_jiffies = last_tick_+1;
    last_tick_ = now_tick;
    KM_Timer_Node tmp_head;
    list_init_head(&tmp_head);
    
    mutex_.lock();
    while(cur_jiffies >= next_jiffies)
    {
        int idx = next_jiffies & TIMER_VECTOR_MASK;
#if 1
        int delta = 0;
        if(0 != idx && (delta = find_first_set_in_bitmap(idx)) != 0) {
            if(-1 == delta || ((idx + delta)&TIMER_VECTOR_MASK) < idx) { // run over 0
                delta = TIMER_VECTOR_SIZE-idx; // need cascade timer when index 0
            }
            idx += delta;
            idx &= TIMER_VECTOR_MASK;
            next_jiffies += delta;
            if(next_jiffies > cur_jiffies) {
                break;
            }
        }
#endif
        ++next_jiffies;
        if (!idx &&
            (!cascade_timer(1, INDEX(0))) &&
            (!cascade_timer(2, INDEX(1)))) {
            cascade_timer(3, INDEX(2));
        }
        list_combine(&tv_[0][idx], &tmp_head);
        clear_tv0_bitmap(idx);
    }
    int count = 0;
    while(!list_empty(&tmp_head))
    {
        running_node_ = tmp_head.next_;
        if(!running_node_->cancelled_ && running_node_->repeat_) {
            reschedule_node_ = running_node_;
        }
        list_remove_node(running_node_);
        --timer_count_;
        mutex_.unlock(); // sync nodes in tmp_list with cancel_timer.
        
        running_mutex_.lock();
        if(running_node_) {
            if(running_node_->timer_) {
                running_node_->timer_->on_timer();
                ++count;
            }
        } else { // this timer is cancelled
            reschedule_node_ = NULL;
        }
        running_node_ = NULL;
        running_mutex_.unlock();
        
        mutex_.lock();
        if(reschedule_node_ && !reschedule_node_->cancelled_ && !timer_pending(reschedule_node_)) {
            reschedule_node_->start_tick_ = get_tick_count_ms();
            add_timer(reschedule_node_, true);
            reschedule_node_ = NULL;
        }
    }

    if(remain_time_ms) {
        // calc remain time in ms
        int pos = find_first_set_in_bitmap(next_jiffies & TIMER_VECTOR_MASK);
        *remain_time_ms = -1==pos?256:pos;
    }

    mutex_.unlock();
    if(remain_time_ms) { // revise the remain time
        now_tick = get_tick_count_ms();
        delta_tick = calc_time_elapse_delta_ms(now_tick, last_tick_);
        if(*remain_time_ms <= delta_tick) {
            *remain_time_ms = 0;
        } else {
            *remain_time_ms -= delta_tick;
        }
    }
    return count;
}

KUMA_NS_END

