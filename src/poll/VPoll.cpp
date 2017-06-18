/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
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

#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <sys/poll.h>
#endif

KUMA_NS_BEGIN

class VPoll : public IOPoll
{
public:
    VPoll();
    ~VPoll();
    
    bool init() override;
    KMError registerFd(SOCKET_FD fd, KMEvent events, IOCallback cb) override;
    KMError unregisterFd(SOCKET_FD fd) override;
    KMError updateFd(SOCKET_FD fd, KMEvent events) override;
    KMError wait(uint32_t wait_ms) override;
    void notify() override;
    PollType getType() const override { return PollType::POLL; }
    bool isLevelTriggered() const override { return true; }
    
private:
    uint32_t get_events(KMEvent kuma_events);
    KMEvent get_kuma_events(uint32_t events);
    
private:
    typedef std::vector<pollfd> PollFdVector;
    NotifierPtr     notifier_ { std::move(Notifier::createNotifier()) };
    PollFdVector    poll_fds_;
};

VPoll::VPoll()
{
    
}

VPoll::~VPoll()
{
    poll_fds_.clear();
    poll_items_.clear();
}

bool VPoll::init()
{
    if (!notifier_->ready()) {
        if (!notifier_->init()) {
            return false;
        }
        IOCallback cb([this](KMEvent ev, void*, size_t) { notifier_->onEvent(ev); });
        registerFd(notifier_->getReadFD(), KUMA_EV_READ | KUMA_EV_ERROR, std::move(cb));
    }
    return true;
}

uint32_t VPoll::get_events(KMEvent kuma_events)
{
    uint32_t ev = 0;
    if(kuma_events & KUMA_EV_READ) {
        ev |= POLLIN;
#ifndef KUMA_OS_WIN
        ev |= POLLPRI;
#endif
    }
    if(kuma_events & KUMA_EV_WRITE) {
        ev |= POLLOUT;
#ifndef KUMA_OS_WIN
        ev |= POLLWRBAND;
#endif
    }
    if(kuma_events & KUMA_EV_ERROR) {
#ifndef KUMA_OS_WIN
        ev |= POLLERR | POLLHUP | POLLNVAL;
#endif
    }
    return ev;
}

KMEvent VPoll::get_kuma_events(uint32_t events)
{
    KMEvent ev = 0;
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

KMError VPoll::registerFd(SOCKET_FD fd, KMEvent events, IOCallback cb)
{
    if (fd < 0) {
        return KMError::INVALID_PARAM;
    }
    resizePollItems(fd);
    int idx = -1;
    if (INVALID_FD == poll_items_[fd].fd || -1 == poll_items_[fd].idx) { // new
        pollfd pfd;
        pfd.fd = fd;
        pfd.events = get_events(events);
        poll_fds_.push_back(pfd);
        idx = int(poll_fds_.size() - 1);
        poll_items_[fd].idx = idx;
    }
    poll_items_[fd].fd = fd;
    poll_items_[fd].events = events;
    poll_items_[fd].cb = std::move(cb);
    KUMA_INFOTRACE("VPoll::registerFd, fd="<<fd<<", events="<<events<<", index="<<idx);
    
    return KMError::NOERR;
}

KMError VPoll::unregisterFd(SOCKET_FD fd)
{
    int max_fd = int(poll_items_.size() - 1);
    KUMA_INFOTRACE("VPoll::unregisterFd, fd="<<fd<<", max_fd="<<max_fd);
    if (fd < 0 || -1 == max_fd || fd > max_fd) {
        KUMA_WARNTRACE("VPoll::unregisterFd, failed, max_fd="<<max_fd);
        return KMError::INVALID_PARAM;
    }
    int idx = poll_items_[fd].idx;
    if(fd < max_fd) {
        poll_items_[fd].reset();
    } else if (fd == max_fd) {
        poll_items_.pop_back();
    }
    
    int last_idx = int(poll_fds_.size() - 1);
    if (idx > last_idx || -1 == idx) {
        return KMError::NOERR;
    }
    if (idx != last_idx) {
        std::iter_swap(poll_fds_.begin()+idx, poll_fds_.end()-1);
        poll_items_[poll_fds_[idx].fd].idx = idx;
    }
    poll_fds_.pop_back();
    return KMError::NOERR;
}

KMError VPoll::updateFd(SOCKET_FD fd, KMEvent events)
{
    int max_fd = int(poll_items_.size() - 1);
    if (fd < 0 || -1 == max_fd || fd > max_fd) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, fd="<<fd<<", max_fd="<<max_fd);
        return KMError::INVALID_PARAM;
    }
    if(poll_items_[fd].fd != fd) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, fd="<<fd<<", item_fd="<<poll_items_[fd].fd);
        return KMError::INVALID_PARAM;
    }
    int idx = poll_items_[fd].idx;
    if (idx < 0 || idx >= poll_fds_.size()) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, index="<<idx);
        return KMError::INVALID_STATE;
    }
    if(poll_fds_[idx].fd != fd) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, fd="<<fd<<", pfds_fd="<<poll_fds_[idx].fd);
        return KMError::INVALID_PARAM;
    }
    poll_fds_[idx].events = get_events(events);
    poll_items_[fd].events = events;
    return KMError::NOERR;
}

KMError VPoll::wait(uint32_t wait_ms)
{
#ifdef KUMA_OS_WIN
    int num_revts = WSAPoll(&poll_fds_[0], poll_fds_.size(), wait_ms);
#else
    int num_revts = poll(&poll_fds_[0], (nfds_t)poll_fds_.size(), wait_ms);
#endif
    if (-1 == num_revts) {
        if(EINTR == errno) {
            errno = 0;
        } else {
            KUMA_ERRTRACE("VPoll::wait, err="<<getLastError());
        }
        return KMError::INVALID_STATE;
    }

    // copy poll fds since event handler may unregister fd
    PollFdVector poll_fds = poll_fds_;
    
    int idx = 0;
    int last_idx = int(poll_fds.size() - 1);
    while(num_revts > 0 && idx <= last_idx) {
        if(poll_fds[idx].revents) {
            --num_revts;
            if(poll_fds[idx].fd < poll_items_.size()) {
                IOCallback &cb = poll_items_[poll_fds[idx].fd].cb;
                if(cb) cb(get_kuma_events(poll_fds[idx].revents), nullptr, 0);
            }
        }
        ++idx;
    }
    return KMError::NOERR;
}

void VPoll::notify()
{
    notifier_->notify();
}

IOPoll* createVPoll() {
    return new VPoll();
}

KUMA_NS_END
