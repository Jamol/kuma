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

int find_first_set(unsigned int b);
TICK_COUNT_TYPE get_tick_count_ms();
TICK_COUNT_TYPE calc_time_elapse_delta_ms(TICK_COUNT_TYPE now_tick, TICK_COUNT_TYPE& start_tick);

//////////////////////////////////////////////////////////////////////////
// KM_Timer
using namespace komm;

KM_Timer::KM_Timer(KM_Timer_Manager* mgr, TimerHandler* hdr)
{
    m_timer_mgr = mgr;
    m_handler = hdr;
    m_timer_node.timer = this;
}

KM_Timer::~KM_Timer()
{
    unschedule();
}

bool KM_Timer::schedule(unsigned int time_elapse, bool repeat)
{
    if(m_timer_mgr) {
        return m_timer_mgr->schedule(&m_timer_node, time_elapse, repeat);
    }
    return false;
}

void KM_Timer::unschedule()
{
    if(m_timer_mgr) {
        m_timer_mgr->unschedule(&m_timer_node);
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
    m_reschedule_node = NULL;
    m_timer_count = 0;
    m_last_tick = 0;
    memset(&m_tv0_bitmap, 0, sizeof(m_tv0_bitmap));
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
    KM_Timer_Node* head = NULL;
    KM_Timer_Node* node = NULL;
    m_mutex.lock();
    for(int i=0; i<TV_COUNT; ++i)
    {
        for (int j=0; j<TIMER_VECTOR_SIZE; ++j)
        {
            head = &m_tv[i][j];
            node = head->next;
            while(node != head)
            {
                if(node->timer) {
                    node->timer->on_detach();
                }
                node = node->next;
            }
        }
    }
    m_mutex.unlock();
}

bool KM_Timer_Manager::schedule(KM_Timer_Node* timer_node, unsigned int time_elapse, bool repeat)
{
    if(timer_pending(timer_node) && time_elapse == timer_node->elapse) {
        return true;
    }
    uint64_t nowTick = get_tick_count_ms();
    timer_node->unscheduled = false;
    m_mutex.lock();
    if(timer_pending(timer_node)) {
        remove_timer(timer_node);
    }
    timer_node->start_tick = nowTick;
    timer_node->elapse = time_elapse;
    timer_node->repeat = repeat;

    bool ret = add_timer(timer_node, true);
    m_mutex.unlock();
    return ret;
}

void KM_Timer_Manager::unschedule(KM_Timer_Node* timer_node)
{
    if(NULL == timer_node || timer_node->unscheduled) {
        return ;
    }
    timer_node->unscheduled = true;
    bool cancelled = false;
    if(m_reschedule_node == timer_node || timer_pending(timer_node)) {
        m_mutex.lock();
        if(timer_pending(timer_node)) {
            remove_timer(timer_node);
            cancelled = true;
        } else {
            if(m_running_node != timer_node) {
                cancelled = true;
            }
            if(m_reschedule_node == timer_node) {
                m_reschedule_node = NULL;
            }
        }
        m_mutex.unlock();
    }
    // check timer running
    if(!cancelled && m_running_node == timer_node) { // may be running, wait it
        m_running_mutex.lock();
        if(m_running_node == timer_node) {
            m_running_node = NULL;
            cancelled = true;
        }
        m_running_mutex.unlock();
    }
    // check again for reschedule timer
    if(!cancelled && (m_reschedule_node == timer_node || timer_pending(timer_node))) {
        m_mutex.lock();
        if(timer_pending(timer_node)) {
            remove_timer(timer_node);
        }
        if(m_reschedule_node == timer_node) {
            m_reschedule_node = NULL;
        }
        m_mutex.unlock();
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

void KM_Timer_Manager::set_tv0_bitmap(unsigned char idx)
{
    unsigned char a = idx/(sizeof(m_tv0_bitmap[0])*8);
    unsigned char b = idx%(sizeof(m_tv0_bitmap[0])*8);
    m_tv0_bitmap[a] |= 1 << b;
}

void KM_Timer_Manager::clear_tv0_bitmap(unsigned char idx)
{
    unsigned char a = idx/(sizeof(m_tv0_bitmap[0])*8);
    unsigned char b = idx%(sizeof(m_tv0_bitmap[0])*8);
    m_tv0_bitmap[a] &= ~(1 << b);
}

int KM_Timer_Manager::find_first_set_in_bitmap(unsigned char idx)
{
    unsigned char a = idx/(sizeof(m_tv0_bitmap[0])*8);
    unsigned char b = idx%(sizeof(m_tv0_bitmap[0])*8);
    int pos = -1;
    pos = find_first_set(m_tv0_bitmap[a] >> b);
    if(-1 == pos) {
        int i = a + 1;
        for (i &= 7; i != a; ++i, i &= 7) {
            pos = find_first_set(m_tv0_bitmap[i]);
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
        unsigned int bits = (m_tv0_bitmap[a] << (32 - b)) >> (32 - b);
        pos = find_first_set(bits);
        if(pos >= 0) {
            pos += 256 - b;
        }
    }
    return pos;
}

bool KM_Timer_Manager::add_timer(KM_Timer_Node* timer_node, bool from_schedule)
{
    if(0 == m_timer_count) {
        m_last_tick = timer_node->start_tick;
    }
    TICK_COUNT_TYPE fire_tick = timer_node->elapse + timer_node->start_tick;
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
        set_tv0_bitmap(i);
        timer_node->tv_index = 0;
        timer_node->tl_index = i;
    }
    else if(elapse_jiffies < 1 << (2*TIMER_VECTOR_BITS)) {
        int i = (fire_jiffies>>TIMER_VECTOR_BITS) & TIMER_VECTOR_MASK;
        head = &m_tv[1][i];
        timer_node->tv_index = 1;
        timer_node->tl_index = i;
    }
    else if(elapse_jiffies < 1 << (3*TIMER_VECTOR_BITS)) {
        int i = (fire_jiffies>>(2*TIMER_VECTOR_BITS)) & TIMER_VECTOR_MASK;
        head = &m_tv[2][i];
        timer_node->tv_index = 2;
        timer_node->tl_index = i;
    }
    else if(elapse_jiffies <= 0xFFFFFFFF) {
        int i = (fire_jiffies>>(3*TIMER_VECTOR_BITS)) & TIMER_VECTOR_MASK;
        head = &m_tv[3][i];
        timer_node->tv_index = 3;
        timer_node->tl_index = i;
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
    if(0 == timer_node->tv_index
       && timer_node->next != timer_node
       && timer_node->next == timer_node->prev
       && timer_node->next == &m_tv[0][timer_node->tl_index]) {
        clear_tv0_bitmap(timer_node->tl_index);
    }
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
int KM_Timer_Manager::check_expire(unsigned long* remain_time_ms)
{
    if(0 == m_timer_count) {
        return 0;
    }
    TICK_COUNT_TYPE now_tick = get_tick_count_ms();
    TICK_COUNT_TYPE delta_tick = calc_time_elapse_delta_ms(now_tick, m_last_tick);
    if(0 == delta_tick) {
        return 0;
    }
    TICK_COUNT_TYPE cur_jiffies = now_tick;
    TICK_COUNT_TYPE next_jiffies = m_last_tick+1;
    m_last_tick = now_tick;
    KM_Timer_Node tmp_head;
    list_init_head(&tmp_head);
    
    m_mutex.lock();
    while(cur_jiffies >= next_jiffies)
    {
        int idx = next_jiffies & TIMER_VECTOR_MASK;
        if (!idx &&
            (!cascade_timer(1, INDEX(0))) &&
            (!cascade_timer(2, INDEX(1)))) {
            cascade_timer(3, INDEX(2));
        }
        ++next_jiffies;
        list_combine(&m_tv[0][idx], &tmp_head);
        clear_tv0_bitmap(idx);
    }
    int count = 0;
    while(!list_empty(&tmp_head))
    {
        m_running_node = tmp_head.next;
        if(!m_running_node->unscheduled && m_running_node->repeat) {
            m_reschedule_node = m_running_node;
        }
        list_remove_node(m_running_node);
        --m_timer_count;
        m_mutex.unlock(); // sync nodes in tmp_list with unschedule.
        
        m_running_mutex.lock();
        if(m_running_node) {
            if(m_running_node->timer) {
                m_running_node->timer->on_timer();
                ++count;
            }
        } else { // this timer is unscheduled
            m_reschedule_node = NULL;
        }
        m_running_node = NULL;
        m_running_mutex.unlock();
        
        m_mutex.lock();
        if(m_reschedule_node && !m_reschedule_node->unscheduled && !timer_pending(m_reschedule_node)) {
            m_reschedule_node->start_tick = get_tick_count_ms();
            add_timer(m_reschedule_node, true);
            m_reschedule_node = NULL;
        }
    }

    if(remain_time_ms) {
        // calc remain time in ms
        int pos = find_first_set_in_bitmap(next_jiffies & TIMER_VECTOR_MASK);
        *remain_time_ms = -1==pos?256:pos;
        
        now_tick = get_tick_count_ms();
        delta_tick = calc_time_elapse_delta_ms(now_tick, m_last_tick);
        if(*remain_time_ms <= delta_tick) {
            *remain_time_ms = 0;
        } else {
            *remain_time_ms -= delta_tick;
        }
    }

    m_mutex.unlock();

    return count;
}

