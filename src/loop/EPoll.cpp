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
    virtual int register_fd(int fd, uint32_t events, IOCallback& cb);
    virtual int register_fd(int fd, uint32_t events, IOCallback&& cb);
    virtual int unregister_fd(int fd);
    virtual int modify_events(int fd, uint32_t events);
    virtual int wait(uint32_t wait_time_ms);
    virtual void notify();
    
private:
    uint32_t get_events(uint32_t kuma_events);
    uint32_t get_kuma_events(uint32_t events);
    
private:
    int         epoll_fd_;
    Notifier    notifier_;
    IOCallback  cb_;
};

EPoll::EPoll
: epoll_fd_(INVALID_FD)
()
{
	
}

EPoll::~EPoll()
{
    if(INVALID_FD != epoll_fd_) {
        close(epoll_fd_);
        epoll_fd_ = INVALID_FD;
    }
}

bool EPoll::init()
{
    epoll_fd_ = epoll_create(MAX_EPOLL_FDS);
    if(INVALID_FD == epoll_fd_) {
        return false;
    }
    notifier_.init();
    cb_ = [this] (uint32_t ev) { notifier_.onEvent(ev); };
    register_fd(notifier_.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, _cb);
    return true;
}

uint32_t EPoll::get_events(uint32_t kuma_events)
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

uint32_t EPoll::get_kuma_events(uint32_t events)
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

int EPoll::register_fd(int fd, uint32_t events, IOCallback* cb)
{
    struct epoll_event evt = {0};
    evt.data.ptr = cb;
    evt.events = get_events(events);//EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
    if(epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &evt) < 0) {
        KUMA_INFOTRACE("EPoll::register_fd error, fd="<<fd<<", errno="<<errno);
        return -1;
    }
    KUMA_INFOTRACE("EPoll::register_fd, fd="<<fd);

    return KUMA_ERROR_NOERR;
}

int EPoll::unregister_fd(int fd)
{
    KUMA_INFOTRACE("EPoll::unregister_fd, fd="<<fd);
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, NULL);
    return KUMA_ERROR_NOERR;
}

int EPoll::modify_events(int fd, uint32_t events)
{
    struct epoll_event evt = {0};
    evt.data.ptr = handler;
    evt.events = get_events(events);
    if(epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &evt) < 0) {
        KUMA_INFOTRACE("EPoll::modify_events error, fd="<<fd<<", errno="<<errno);
        return -1;
    }
    return KUMA_ERROR_NOERR;
}

int EPoll::wait(uint32_t wait_time_ms)
{
    struct epoll_event events[MAX_EVENT_NUM];
    int nfds = epoll_wait(epoll_fd_, events, MAX_EVENT_NUM , wait_time_ms);
    if (nfds < 0) {
        if(errno != EINTR) {
            KUMA_ERRTRACE("EPoll::wait, errno="<<errno);
        }
        KUMA_INFOTRACE("EPoll::wait, epoll_wait, nfds="<<nfds<<", errno="<<errno);
    } else {
        for (int i=0; i<nfds; i++) {
            IOCallback* cb = (IOCallback*)events[i].data.ptr;
            if(cb) (*cb)(get_kuma_events(events[i].events));
        }
    }
    return KUMA_ERROR_NOERR;
}

void EPoll::notify()
{
    notifier_.notify();
}

IOPoll* createEPoll() {
    return new EPoll();
}

KUMA_NS_END
