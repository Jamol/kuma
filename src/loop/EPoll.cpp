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

#include <sys/epoll.h>

KUMA_NS_BEGIN

class EPoll : public IOPoll
{
public:
    EPoll();
    ~EPoll();
    
    virtual bool init();
    virtual int registerFD(int fd, uint32_t events, IOHandler* handler);
    virtual int unregisterFD(int fd);
    virtual int modifyEvents(int fd, uint32_t events, IOHandler* handler);
    virtual int wait(uint32_t wait_time_ms);
    virtual void notify();
    
private:
    uint32_t getEvents(uint32_t kuma_events);
    uint32_t getKumaEvents(uint32_t events);
    
private:
    int         m_epoll_fd;
    Notifier    m_notifier;
};

EPoll::EPoll()
{
    m_epoll_fd = INVALID_FD;
}

EPoll::~EPoll()
{
    if(INVALID_FD != m_epoll_fd) {
        close(m_epoll_fd);
        m_epoll_fd = INVALID_FD;
    }
}

bool EPoll::init()
{
    m_epoll_fd = epoll_create(MAX_EPOLL_FDS);
    if(INVALID_FD == m_epoll_fd) {
        return false;
    }
    m_notifier.init();
    registerFD(m_notifier.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, &m_notifier);
    return true;
}

uint32_t EPoll::getEvents(uint32_t kuma_events)
{
    uint32_t ev = EPOLLET;//EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
    if(kuma_events | KUMA_EV_READ) {
        ev |= EPOLLIN;
    }
    if(kuma_events | KUMA_EV_WRITE) {
        ev |= EPOLLOUT;
    }
    if(kuma_events | KUMA_EV_ERROR) {
        ev |= EPOLLERR | EPOLLHUP;
    }
    return ev;
}

uint32_t EPoll::getKumaEvents(uint32_t events)
{
    uint32_t ev = 0;
    if(events & EPOLLIN) {
        ev |= KUMA_EV_READ;
    }
    if(events & EPOLLOUT) {
        ev |= KUMA_EV_WRITE;
    }
    if(events & (EPOLLERR | EPOLLHUP)) {
        ev |= KUMA_EV_ERROR;
    }
    return ev;
}

int EPoll::registerFD(int fd, uint32_t events, IOHandler* handler)
{
    struct epoll_event evt = {0};
    evt.data.ptr = handler;
    evt.events = getEvents(events);//EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
    if(epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &evt) < 0) {
        KUMA_INFOTRACE("EPoll::registerFD error, fd="<<fd<<", errno="<<errno);
        return -1;
    }
    KUMA_INFOTRACE("EPoll::registerFD, fd="<<fd);

    return KUMA_ERROR_NOERR;
}

int EPoll::unregisterFD(int fd)
{
    KUMA_INFOTRACE("EPoll::unregisterFD, fd="<<fd);
    epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    return KUMA_ERROR_NOERR;
}

int EPoll::modifyEvents(int fd, uint32_t events, IOHandler* handler)
{
    struct epoll_event evt = {0};
    evt.data.ptr = handler;
    evt.events = getEvents(events);
    if(epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &evt) < 0) {
        KUMA_INFOTRACE("EPoll::modifyEvents error, fd="<<fd<<", errno="<<errno);
        return -1;
    }
    return KUMA_ERROR_NOERR;
}

int EPoll::wait(uint32_t wait_time_ms)
{
    struct epoll_event events[MAX_EVENT_NUM];
    IOHandler* handler = NULL;
    int nfds = epoll_wait(m_epoll_fd, events, MAX_EVENT_NUM , wait_time_ms);
    if (nfds < 0) {
        if(errno != EINTR) {
            KUMA_ERRTRACE("EPoll::wait, errno="<<errno);
        }
        KUMA_INFOTRACE("EPoll::wait, epoll_wait, nfds="<<nfds<<", errno="<<errno);
    } else {
        for (int i=0; i<nfds; i++) {
            handler = (IOHandler*)events[i].data.ptr;
            if(handler) handler->onEvent(getKumaEvents(events[i].events));
        }
    }
    return KUMA_ERROR_NOERR;
}

void EPoll::notify()
{
    m_notifier.notify();
}

IOPoll* createEPoll() {
    return new EPoll();
}

KUMA_NS_END
