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

KUMA_NS_BEGIN

class SelectPoll : public IOPoll
{
public:
    SelectPoll();
    ~SelectPoll();
    
    virtual bool init();
    virtual int registerFD(int fd, uint32_t events, IOHandler* handler);
    virtual int unregisterFD(int fd);
    virtual int modifyEvents(int fd, uint32_t events, IOHandler* handler);
    virtual int wait(uint32_t wait_time_ms);
    virtual void notify();
    
private:
    class SelectItem
    {
    public:
        SelectItem() {}
        
    public:
        IOHandler* handler;
        uint32_t events;
    };
    
private:
    Notifier    m_notifier;
    SelectItem* m_poll_items;
    int         m_fds_alloc;
    int         m_fds_used;
};

SelectPoll::SelectPoll()
{
    m_poll_items = NULL;
    m_fds_alloc = 0;
    m_fds_used = 0;
}

SelectPoll::~SelectPoll()
{
    if(m_poll_items) {
        delete[] m_poll_items;
        m_poll_items = NULL;
    }
}

bool SelectPoll::init()
{
    m_notifier.init();
    registerFD(m_notifier.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, &m_notifier);
    return true;
}

int SelectPoll::registerFD(int fd, uint32_t events, IOHandler* handler)
{
    KUMA_INFOTRACE("SelectPoll::registerFD, fd="<<fd);
    if (fd >= m_fds_alloc) {
        int tmp_num = m_fds_alloc + 1024;
        if (tmp_num < fd + 1)
            tmp_num = fd + 1;
        
        SelectItem *newItems = new SelectItem[tmp_num];
        if(m_fds_alloc > 0) {
            memcpy(newItems, m_poll_items, m_fds_alloc*sizeof(SelectItem));
        }
        
        delete[] m_poll_items;
        m_poll_items = newItems;
    }
    m_poll_items[fd].handler = handler;
    m_poll_items[fd].events = events;
    ++m_fds_used;
    return KUMA_ERROR_NOERR;
}

int SelectPoll::unregisterFD(int fd)
{
    KUMA_INFOTRACE("SelectPoll::unregisterFD, fd="<<fd);
    if ((fd < 0) || (fd >= m_fds_alloc) || 0 == m_fds_used) {
        KUMA_WARNTRACE("SelectPoll::unregisterFD, failed, alloced="<<m_fds_alloc<<", used="<<m_fds_used);
        return KUMA_ERROR_INVALID_PARAM;
    }
    
    m_poll_items[fd].handler = NULL;
    m_poll_items[fd].events = 0;
    --m_fds_used;
    return KUMA_ERROR_NOERR;
}

int SelectPoll::modifyEvents(int fd, uint32_t events, IOHandler* handler)
{
    return KUMA_ERROR_NOERR;
}

int SelectPoll::wait(uint32_t wait_time_ms)
{
    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    struct timeval tval;
    if(wait_time_ms > 0) {
        tval.tv_sec = wait_time_ms/1000;
        tval.tv_usec = (wait_time_ms - tval.tv_sec*1000)*1000;
    }
    int maxfd = 0;
    for (int i=0; i<m_fds_alloc; ++i) {
        if(m_poll_items[i].handler != NULL && m_poll_items[i].events != 0) {
            if(m_poll_items[i].events|KUMA_EV_READ) {
                FD_SET(i, &readfds);
            }
            if(m_poll_items[i].events|KUMA_EV_WRITE) {
                FD_SET(i, &writefds);
            }
            if(m_poll_items[i].events|KUMA_EV_ERROR) {
                FD_SET(i, &exceptfds);
            }
            if(i > maxfd) {
                maxfd = i;
            }
        }
    }
    int nready = ::select(maxfd+1, &readfds, &writefds, &exceptfds, wait_time_ms == -1 ? NULL : &tval);
    if (nready <= 0) {
        return KUMA_ERROR_NOERR;
    }
    for (int i=0; i<=maxfd && nready > 0; ++i) {
        uint32_t events = 0;
        if(FD_ISSET(i, &readfds)) {
            events |= KUMA_EV_READ;
            --nready;
        }
        if(nready > 0 && FD_ISSET(i, &writefds)) {
            events |= KUMA_EV_WRITE;
            --nready;
        }
        if(nready > 0 && FD_ISSET(i, &exceptfds)) {
            events |= KUMA_EV_ERROR;
            --nready;
        }
        IOHandler* handler = m_poll_items[i].handler;
        if(handler) {
            handler->onEvent(events);
        }
    }
    return KUMA_ERROR_NOERR;
}

void SelectPoll::notify()
{
    m_notifier.notify();
}

IOPoll* createSelectPoll() {
    return new SelectPoll();
}

KUMA_NS_END