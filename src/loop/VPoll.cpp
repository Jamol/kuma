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

#include "internal.h"
#include "Notifier.h"
#include "util/kmtrace.h"

#include <sys/poll.h>

KUMA_NS_BEGIN

class VPoll : public IOPoll
{
public:
    VPoll();
    ~VPoll();
    
    virtual bool init();
    virtual int register_fd(int fd, uint32_t events, IOHandler* handler);
    virtual int unregister_fd(int fd);
    virtual int modify_events(int fd, uint32_t events, IOHandler* handler);
    virtual int wait(uint32_t wait_time_ms);
    virtual void notify();
    
private:
    uint32_t get_events(uint32_t kuma_events);
    uint32_t get_kuma_events(uint32_t events);
    
private:
    class PollItem
    {
    public:
        PollItem() { fd = -1; handler = NULL; index = -1; }
        
        friend class VPoll;
    protected:
        int fd;
        IOHandler* handler;
        int index;
    };
    
private:
    Notifier        m_notifier;
    PollItem*       m_poll_items;
    struct pollfd*  m_poll_fds;
    int             m_fds_alloc;
    int             m_fds_used;
};

VPoll::VPoll()
{
    m_poll_fds = NULL;
    m_fds_alloc = 0;
    m_fds_used = 0;
}

VPoll::~VPoll()
{
    if(m_poll_items) {
        delete[] m_poll_items;
        m_poll_items = NULL;
    }
    if(m_poll_fds) {
        delete[] m_poll_fds;
        m_poll_fds = NULL;
    }
}

bool VPoll::init()
{
    m_notifier.init();
    register_fd(m_notifier.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, &m_notifier);
    return true;
}

uint32_t VPoll::get_events(uint32_t kuma_events)
{
    uint32_t ev = 0;
    if(kuma_events | KUMA_EV_READ) {
        ev |= POLLIN | POLLPRI;
    }
    if(kuma_events | KUMA_EV_WRITE) {
        ev |= POLLOUT | POLLWRBAND;
    }
    if(kuma_events | KUMA_EV_ERROR) {
        ev |= POLLERR | POLLHUP | POLLNVAL;
    }
    return ev;
}

uint32_t VPoll::get_kuma_events(uint32_t events)
{
    uint32_t ev = 0;
    if(events & (POLLIN | POLLPRI)) {
        ev |= KUMA_EV_READ;
    }
    if(events & (POLLOUT | POLLWRBAND)) {
        ev |= KUMA_EV_WRITE;
    }
    if(events & (POLLERR | POLLHUP | POLLNVAL)) {
        ev |= KUMA_EV_ERROR;
    }
    return ev;
}

int VPoll::register_fd(int fd, uint32_t events, IOHandler* handler)
{
    if (fd >= m_fds_alloc) {
        int tmp_num = m_fds_alloc + 1024;
        if (tmp_num < fd + 1)
            tmp_num = fd + 1;
        
        PollItem *newItems = new PollItem[tmp_num];
        if(m_fds_alloc) {
            memcpy(newItems, m_poll_items, m_fds_alloc*sizeof(PollItem));
        }
        
        delete[] m_poll_items;
        m_poll_items = newItems;
        
        pollfd *newpfds = new pollfd[tmp_num];
        memset((uint8_t*)newpfds, 0, tmp_num*sizeof(pollfd));
        if(m_fds_alloc) {
            memcpy(newpfds, m_poll_fds, m_fds_alloc*sizeof(pollfd));
        }
        
        delete[] m_poll_fds;
        m_poll_fds = newpfds;
        m_fds_alloc = tmp_num;
    }
    
    m_poll_items[fd].fd = fd;
    m_poll_items[fd].handler = handler;
    m_poll_items[fd].index = m_fds_used;
    m_poll_fds[m_fds_used].fd = fd;
    m_poll_fds[m_fds_used].events = get_events(events);
    KUMA_INFOTRACE("VPoll::register_fd, fd="<<fd<<", index="<<m_fds_used);
    ++m_fds_used;
    
    return KUMA_ERROR_NOERR;
}

int VPoll::unregister_fd(int fd)
{
    KUMA_INFOTRACE("VPoll::unregister_fd, fd="<<fd);
    if ((fd < 0) || (fd >= m_fds_alloc) || 0 == m_fds_used) {
        KUMA_WARNTRACE("VPoll::unregister_fd, failed, alloced="<<m_fds_alloc<<", used="<<m_fds_used);
        return KUMA_ERROR_INVALID_PARAM;
    }
    int pfds_index = m_poll_items[fd].index;
    
    m_poll_items[fd].handler = NULL;
    m_poll_items[fd].fd = INVALID_FD;
    m_poll_items[fd].index = -1;
    
    if (pfds_index != m_fds_used - 1) {
        m_poll_fds[pfds_index] = m_poll_fds[m_fds_used - 1];
        m_poll_items[m_poll_fds[pfds_index].fd].index = pfds_index;
    }
    m_poll_fds[m_fds_used - 1].fd = -1;
    m_poll_fds[m_fds_used - 1].events = 0;
    --m_fds_used;
    return KUMA_ERROR_NOERR;
}

int VPoll::modify_events(int fd, uint32_t events, IOHandler* handler)
{
    if ((fd < 0) || (fd >= m_fds_alloc) || 0 == m_fds_used) {
        KUMA_WARNTRACE("modify_events, failed, alloced: "<<m_fds_alloc<<", used: "<<m_fds_used);
        return KUMA_ERROR_INVALID_PARAM;
    }
    if(m_poll_items[fd].fd != fd) {
        KUMA_WARNTRACE("modify_events, failed, fd: "<<fd<<", m_fd: "<<m_poll_items[fd].fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    int pfds_index = m_poll_items[fd].index;
    if(pfds_index < 0 || pfds_index >= m_fds_used) {
        KUMA_WARNTRACE("modify_events, failed, pfd_index: "<<pfds_index);
        return KUMA_ERROR_INVALID_STATE;
    }
    if(m_poll_fds[pfds_index].fd != fd) {
        KUMA_WARNTRACE("modify_events, failed, fd: "<<fd<<", pfds_fd: "<<m_poll_fds[pfds_index].fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    m_poll_fds[pfds_index].events = get_events(events);
    return KUMA_ERROR_NOERR;
}

int VPoll::wait(uint32_t wait_time_ms)
{
    int num_revts = poll(m_poll_fds, m_fds_used, wait_time_ms);
    if (-1 == num_revts) {
        if(EINTR == errno) {
            errno = 0;
        } else {
            KUMA_ERRTRACE("VPoll::wait, errno: "<<errno);
        }
        return KUMA_ERROR_INVALID_STATE;
    }

    int cur_pfds_index = 0;
    IOHandler* handler = NULL;
    while(num_revts > 0 && cur_pfds_index < m_fds_used) {
        if(m_poll_fds[cur_pfds_index].revents) {
            --num_revts;
            handler = m_poll_items[m_poll_fds[cur_pfds_index].fd].handler;
            if(handler) handler->onEvent(get_kuma_events(m_poll_fds[cur_pfds_index].revents));
        }
        ++cur_pfds_index;
    }
    return KUMA_ERROR_NOERR;
}

void VPoll::notify()
{
    m_notifier.notify();
}

IOPoll* createVPoll() {
    return new VPoll();
}

KUMA_NS_END

