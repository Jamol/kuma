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

#include "kmconf.h"
#include "kmtimer.h"

#ifdef KUMA_OS_WIN
# if _MSC_VER > 1200
#  define _WINSOCKAPI_	// Prevent inclusion of winsock.h in windows.h
# endif
#include <windows.h>
#endif

#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#endif

TICK_COUNT_TYPE get_tick_count_t()
{
#if defined(KUMA_OS_WIN)
    return GetTickCount();
#else
    unsigned long  ret;
    struct  timeval time_val;

    gettimeofday(&time_val, NULL);
    ret = time_val.tv_sec * 1000 + time_val.tv_usec / 1000;
    return ret;
#endif
}

TICK_COUNT_TYPE calc_time_elapse_delta_t(TICK_COUNT_TYPE cur_tick, TICK_COUNT_TYPE& start_tick)
{
    if(cur_tick - start_tick > (((TICK_COUNT_TYPE)-1)>>1)) {
        start_tick = cur_tick;
        return 0;
    }
    return cur_tick - start_tick;
}
//////////////////////////////////////////////////////////////////////////
// KM_Timer
using namespace komm;

KM_Timer::KM_Timer(KM_Timer_Manager* mgr, TimerHandler* hdr)
{
    m_timer_mgr = mgr;
    m_handler = hdr;
}

KM_Timer::~KM_Timer()
{
    schedule_cancel();
}

bool KM_Timer::schedule(unsigned int time_elapse)
{
    if(NULL == m_timer_mgr) {
        return false;
    }
    return m_timer_mgr->schedule(&m_timer_node, time_elapse);
}

void KM_Timer::schedule_cancel()
{
    if(m_timer_mgr) {
        m_timer_mgr->schedule_cancel(&m_timer_node);
    }
}

void KM_Timer::on_timer()
{
    if(m_handler) {
        m_handler->onTimer();
    }
}

//////////////////////////////////////////////////////////////////////////
// KM_Timer_Manager
KM_Timer_Manager::KM_Timer_Manager()
{
    m_running_node = NULL;
    m_timer_count = 0;
    m_last_tick = 0;
    for (int i=0; i<TV_COUNT; ++i)
    {
        for (int j=0; j<TIMER_VECTOR_SIZE; ++j)
        {
            list_init_head(&m_tv[i][j]);
        }
    }
}

KM_Timer_Manager::~KM_Timer_Manager()
{
    m_mutex.lock();
    KM_Timer_Node* head = NULL;
    KM_Timer_Node* node = NULL;
    for(int i=0; i<TV_COUNT; ++i)
    {
        for (int j=0; j<TIMER_VECTOR_SIZE; ++j)
        {
            head = &m_tv[i][j];
            node = head->next;
            while(node != head)
            {
                if(node->timer_) {
                    node->timer_->on_detach();
                }
                node = node->next;
            }
        }
    }
    m_mutex.unlock();
}

bool KM_Timer_Manager::schedule(KM_Timer_Node* timer_node, unsigned int time_elapse)
{
    if(timer_pending(timer_node) && time_elapse == timer_node->elapse_) {
        return true;
    }
    TICK_COUNT_TYPE nowTick = get_tick_count_t();
    timer_node->cancelled_ = false;
    m_mutex.lock();
    if(timer_pending(timer_node)) {
        remove_timer(timer_node);
    }
    timer_node->start_tick_ = nowTick;
    timer_node->elapse_ = time_elapse;

    bool ret = add_timer(timer_node, true);
    m_mutex.unlock();
    return ret;
}

void KM_Timer_Manager::schedule_cancel(KM_Timer_Node* timer_node)
{
    if(NULL == timer_node || timer_node->cancelled_) {
        return ;
    }
    timer_node->cancelled_ = true;
    bool cancelled = false;
    if(timer_pending(timer_node)) {
        m_mutex.lock();
        if(timer_pending(timer_node)) {
            remove_timer(timer_node);
            cancelled = true;
        }
        m_mutex.unlock();
    }
    if(!cancelled) { // may be running, wait it
        m_running_mutex.lock();
        if(m_running_node == timer_node) {
            m_running_node = NULL;
        }
        m_running_mutex.unlock();
    }
}

void KM_Timer_Manager::list_init_head(KM_Timer_Node* head)
{
    head->next = head;
    head->prev = head;
}

void KM_Timer_Manager::list_add_node(KM_Timer_Node* head, KM_Timer_Node* timer_node)
{
    head->prev->next = timer_node;
    timer_node->prev = head->prev;
    timer_node->next = head;
    head->prev = timer_node;
}

void KM_Timer_Manager::list_remove_node(KM_Timer_Node* timer_node)
{
    timer_node->prev->next = timer_node->next;
    timer_node->next->prev = timer_node->prev;
    timer_node->reset();
}

void KM_Timer_Manager::list_replace(KM_Timer_Node* old_head, KM_Timer_Node* new_head)
{
    new_head->next = old_head->next;
    new_head->next->prev = new_head;
    new_head->prev = old_head->prev;
    new_head->prev->next = new_head;
    list_init_head(old_head);
}

void KM_Timer_Manager::list_combine(KM_Timer_Node* from_head, KM_Timer_Node* to_head)
{
    if(from_head->next == from_head) {
        return ;
    }
    to_head->prev->next = from_head->next;
    from_head->next->prev = to_head->prev;
    from_head->prev->next = to_head;
    to_head->prev = from_head->prev;
    list_init_head(from_head);
}

bool KM_Timer_Manager::list_empty(KM_Timer_Node* head)
{
    return head->next == head;
}

bool KM_Timer_Manager::add_timer(KM_Timer_Node* timer_node, bool from_schedule)
{
    if(0 == m_timer_count) {
        m_last_tick = timer_node->start_tick_;
    }
    TICK_COUNT_TYPE fire_tick = timer_node->elapse_ + timer_node->start_tick_;
    if(fire_tick - m_last_tick > (((TICK_COUNT_TYPE)-1)>>1)) // time backward
    {// fire it right now
        fire_tick = m_last_tick;
    }
    if(fire_tick == m_last_tick) {
        ++fire_tick; // = next_jiffies
    }
    TICK_COUNT_TYPE elapse_jiffies = fire_tick - m_last_tick;
    TICK_COUNT_TYPE fire_jiffies = fire_tick;
    KM_Timer_Node* head;
    if(elapse_jiffies < TIMER_VECTOR_SIZE) {
        int i = fire_jiffies & TIMER_VECTOR_MASK;
        head = &m_tv[0][i];
    }
    else if(elapse_jiffies < 1 << (2*TIMER_VECTOR_BITS)) {
        int i = (fire_jiffies>>TIMER_VECTOR_BITS) & TIMER_VECTOR_MASK;
        head = &m_tv[1][i];
    }
    else if(elapse_jiffies < 1 << (3*TIMER_VECTOR_BITS)) {
        int i = (fire_jiffies>>(2*TIMER_VECTOR_BITS)) & TIMER_VECTOR_MASK;
        head = &m_tv[2][i];
    }
    else if(elapse_jiffies <= 0xFFFFFFFF) {
        int i = (fire_jiffies>>(3*TIMER_VECTOR_BITS)) & TIMER_VECTOR_MASK;
        head = &m_tv[3][i];
    }
    else
    {// don't support elapse larger than 0xffffffff
        //printf("add_timer, failed, elapse=%lu, start_tick=%lu, last_tick=%lu\n", timer_node->elapse, timer_node->start_tick, m_last_tick);
        return false;
    }
    list_add_node(head, timer_node);
    if(from_schedule) {
        ++m_timer_count;
    }
    return true;
}

void KM_Timer_Manager::remove_timer(KM_Timer_Node* timer_node)
{
    list_remove_node(timer_node);
    --m_timer_count;
}

int KM_Timer_Manager::cascade_timer(int tv_idx, int tl_idx)
{
    KM_Timer_Node tmp_head;
    list_init_head(&tmp_head);
    list_replace(&m_tv[tv_idx][tl_idx], &tmp_head);
    KM_Timer_Node* next_node = tmp_head.next;
    KM_Timer_Node* tmp_node = NULL;
    while(next_node != &tmp_head)
    {
        tmp_node = next_node;
        next_node = next_node->next;
        add_timer(tmp_node);
    }

    return tl_idx;
}

#define INDEX(N) ((next_jiffies >> ((N+1) * TIMER_VECTOR_BITS)) & TIMER_VECTOR_MASK)
int KM_Timer_Manager::check_expire(unsigned long& remain_time)
{
    if(0 == m_timer_count)
        return 0;
    TICK_COUNT_TYPE cur_tick = get_tick_count_t();
    TICK_COUNT_TYPE delta_tick = calc_time_elapse_delta_t(cur_tick, m_last_tick);
    if(0 == delta_tick)
        return 0;
    TICK_COUNT_TYPE cur_jiffies = cur_tick;
    TICK_COUNT_TYPE next_jiffies = m_last_tick+1;
    m_last_tick = cur_tick;
    KM_Timer_Node tmp_head;
    list_init_head(&tmp_head);
    
    m_mutex.lock();
    while(cur_jiffies >= next_jiffies)
    {
        int idx = next_jiffies & TIMER_VECTOR_MASK;
        if (!idx &&
            (!cascade_timer(1, INDEX(0))) &&
            (!cascade_timer(2, INDEX(1))))
        {
            cascade_timer(3, INDEX(2));
        }
        ++next_jiffies;
        list_combine(&m_tv[0][idx], &tmp_head);
    }
    int count = 0;
    while(!list_empty(&tmp_head))
    {
        m_running_node = tmp_head.next;
        list_remove_node(m_running_node);
        m_mutex.unlock(); // sync nodes in tmp_list with schedule_cancel.
        m_running_mutex.lock();
        if(m_running_node && m_running_node->timer_) {
            m_running_node->timer_->on_timer();
        }
        m_running_node = NULL;
        m_running_mutex.unlock();
        ++count;
        m_mutex.lock();
    }
    m_timer_count -= count;
    m_mutex.unlock();

    return count;
}

