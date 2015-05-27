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

#include "IOPoll.h"
#include "Notifier.h"
#include "util/kmtrace.h"

#include <sys/epoll.h>

KUMA_NS_BEGIN

class EPoll : public IOPoll
{
public:
    EPoll();
    ~EPoll();
    
    bool init();
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb);
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb);
    int unregisterFd(SOCKET_FD fd);
    int updateFd(SOCKET_FD fd, uint32_t events);
    int wait(uint32_t wait_time_ms);
    void notify();
    PollType getType() { return POLL_TYPE_EPOLL; }
    
private:
    uint32_t get_events(uint32_t kuma_events);
    uint32_t get_kuma_events(uint32_t events);
    
private:
    typedef std::map<int, IOCallback> IOCallbackMap;

    int             epoll_fd_;
    Notifier        notifier_;
    IOCallbackMap   callbacks_;
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
    IOCallbackMap cb = [this](uint32_t ev) { notifier_.onEvent(ev); };
    registerFd(notifier_.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, std::move(cb));
    return true;
}

uint32_t EPoll::get_events(uint32_t kuma_events)
{
    uint32_t ev = EPOLLET;//EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
    if(kuma_events & KUMA_EV_READ) {
        ev |= EPOLLIN;
    }
    if(kuma_events & KUMA_EV_WRITE) {
        ev |= EPOLLOUT;
    }
    if(kuma_events & KUMA_EV_ERROR) {
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

int EPoll::registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb)
{
    if (callbacks_.find(fd) != callbacks_.end()) {
        return KUMA_ERROR_ALREADY_EXIST;
    }
    auto r = callbacks_.emplace(fd, cb);

    struct epoll_event evt = {0};
    evt.data.ptr = &r.first->second;
    evt.events = get_events(events);//EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
    if(epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &evt) < 0) {
        KUMA_ERRTRACE("EPoll::registerFd error, fd="<<fd<<", errno="<<errno);
        return -1;
    }
    KUMA_INFOTRACE("EPoll::registerFd, fd="<<fd);

    return KUMA_ERROR_NOERR;
}

int EPoll::registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb)
{
    if (callbacks_.find(fd) != callbacks_.end()) {
        return KUMA_ERROR_ALREADY_EXIST;
    }
    auto r = callbacks_.emplace(fd, std::move(cb));

    struct epoll_event evt = { 0 };
    evt.data.ptr = &r.first->second;
    evt.events = get_events(events);//EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &evt) < 0) {
        KUMA_ERRTRACE("EPoll::registerFd error, fd=" << fd << ", errno=" << errno);
        return KUMA_ERROR_FAILED;
    }
    KUMA_INFOTRACE("EPoll::registerFd, fd=" << fd);

    return KUMA_ERROR_NOERR;
}

int EPoll::unregisterFd(SOCKET_FD fd)
{
    KUMA_INFOTRACE("EPoll::unregisterFd, fd="<<fd);
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, NULL);
    callbacks_.erase(fd);
    return KUMA_ERROR_NOERR;
}

int EPoll::updateFd(SOCKET_FD fd, uint32_t events)
{
    struct epoll_event evt = {0};
    evt.data.ptr = handler;
    evt.events = get_events(events);
    if(epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &evt) < 0) {
        KUMA_INFOTRACE("EPoll::updateFd error, fd="<<fd<<", errno="<<errno);
        return KUMA_ERROR_FAILED;
    }
    return KUMA_ERROR_NOERR;
}

int EPoll::wait(uint32_t wait_ms)
{
    struct epoll_event events[MAX_EVENT_NUM];
    int nfds = epoll_wait(epoll_fd_, events, MAX_EVENT_NUM , wait_ms);
    if (nfds < 0) {
        if(errno != EINTR) {
            KUMA_ERRTRACE("EPoll::wait, errno="<<errno);
        }
        KUMA_INFOTRACE("EPoll::wait, epoll_wait, nfds="<<nfds<<", errno="<<errno);
    } else {
        for (int i=0; i<nfds; i++) {
            IOCallback* cb = (IOCallback*)events[i].data.ptr;
            if(cb && *cb) (*cb)(get_kuma_events(events[i].events));
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
