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

#include <algorithm>

KUMA_NS_BEGIN

class SelectPoll : public IOPoll
{
public:
    SelectPoll();
    ~SelectPoll();
    
    bool init() override;
    KMError registerFd(SOCKET_FD fd, KMEvent events, IOCallback cb) override;
    KMError unregisterFd(SOCKET_FD fd) override;
    KMError updateFd(SOCKET_FD fd, KMEvent events) override;
    KMError wait(uint32_t wait_time_ms) override;
    void notify() override;
    PollType getType() const override { return PollType::SELECT; }
    bool isLevelTriggered() const override { return true; }

private:
    void updateFdSet(SOCKET_FD fd, KMEvent events);
    
private:
    struct PollFD {
        SOCKET_FD fd = INVALID_FD;
        KMEvent events = 0;
    };

private:
    typedef std::vector<PollFD> PollFdVector;
    NotifierPtr     notifier_ { std::move(Notifier::createNotifier()) };
    PollFdVector    poll_fds_;
    
    fd_set          read_fds_;
    fd_set          write_fds_;
    fd_set          except_fds_;
    SOCKET_FD       max_fd_;
};

SelectPoll::SelectPoll()
: max_fd_(0)
{
    FD_ZERO(&read_fds_);
    FD_ZERO(&write_fds_);
    FD_ZERO(&except_fds_);
}

SelectPoll::~SelectPoll()
{
    poll_fds_.clear();
    poll_items_.clear();
}

bool SelectPoll::init()
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

KMError SelectPoll::registerFd(SOCKET_FD fd, KMEvent events, IOCallback cb)
{
    if (fd < 0) {
        return KMError::INVALID_PARAM;
    }
    KUMA_INFOTRACE("SelectPoll::registerFd, fd=" << fd);
    resizePollItems(fd);
    if (INVALID_FD == poll_items_[fd].fd || -1 == poll_items_[fd].idx) {
        PollFD pfd;
        pfd.fd = fd;
        pfd.events = events;
        poll_fds_.push_back(pfd);
        poll_items_[fd].idx = int(poll_fds_.size() - 1);
    }
    poll_items_[fd].fd = fd;
    poll_items_[fd].events = events;
    poll_items_[fd].cb = std::move(cb);
    updateFdSet(fd, events);
    return KMError::NOERR;
}

KMError SelectPoll::unregisterFd(SOCKET_FD fd)
{
    int max_fd = int(poll_items_.size() - 1);
    KUMA_INFOTRACE("SelectPoll::unregisterFd, fd="<<fd<<", max_fd="<<max_fd);
    if (fd < 0 || fd > max_fd) {
        KUMA_WARNTRACE("SelectPoll::unregisterFd, failed, max_fd=" << max_fd);
        return KMError::INVALID_PARAM;
    }
    updateFdSet(fd, 0);
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
        std::iter_swap(poll_fds_.begin() + idx, poll_fds_.end() - 1);
        poll_items_[poll_fds_[idx].fd].idx = idx;
    }
    poll_fds_.pop_back();
    return KMError::NOERR;
}

KMError SelectPoll::updateFd(SOCKET_FD fd, KMEvent events)
{
    int max_fd = int(poll_items_.size() - 1);
    if (fd < 0 || -1 == max_fd || fd > max_fd) {
        KUMA_WARNTRACE("SelectPoll::updateFd, failed, fd="<<fd<<", max_fd="<<max_fd);
        return KMError::INVALID_PARAM;
    }
    if(poll_items_[fd].fd != fd) {
        KUMA_WARNTRACE("SelectPoll::updateFd, failed, fd="<<fd<<", item_fd="<<poll_items_[fd].fd);
        return KMError::INVALID_PARAM;
    }
    int idx = poll_items_[fd].idx;
    if (idx < 0 || idx >= poll_fds_.size()) {
        KUMA_WARNTRACE("SelectPoll::updateFd, failed, index="<<idx);
        return KMError::INVALID_STATE;
    }
    if(poll_fds_[idx].fd != fd) {
        KUMA_WARNTRACE("SelectPoll::updateFd, failed, fd="<<fd<<", pfds_fd="<<poll_fds_[idx].fd);
        return KMError::INVALID_PARAM;
    }
    poll_fds_[idx].events = events;
    poll_items_[fd].events = events;
    updateFdSet(fd, events);
    return KMError::NOERR;
}

void SelectPoll::updateFdSet(SOCKET_FD fd, KMEvent events)
{
    if(events != 0) {
        if (events & KUMA_EV_READ) {
            FD_SET(fd, &read_fds_);
        } else {
            FD_CLR(fd, &read_fds_);
        }
        if (events & KUMA_EV_WRITE) {
            FD_SET(fd, &write_fds_);
        } else {
            FD_CLR(fd, &write_fds_);
        }
        if (events & KUMA_EV_ERROR) {
            FD_SET(fd, &except_fds_);
        }
        if (fd > max_fd_) {
            max_fd_ = fd;
        }
    } else {
        FD_CLR(fd, &read_fds_);
        FD_CLR(fd, &write_fds_);
        FD_CLR(fd, &except_fds_);
        if(max_fd_ == fd) {
            auto it = std::max_element(poll_fds_.begin(), poll_fds_.end(), [] (PollFD& pf1, PollFD& pf2){
                return pf1.fd < pf2.fd;
            });
            max_fd_ = it != poll_fds_.end()?(*it).fd:0;
        }
    }
}

KMError SelectPoll::wait(uint32_t wait_ms)
{
    fd_set readfds, writefds, exceptfds;
    memcpy(&readfds, &read_fds_, sizeof(read_fds_));
    memcpy(&writefds, &write_fds_, sizeof(write_fds_));
    memcpy(&exceptfds, &except_fds_, sizeof(except_fds_));
    struct timeval tval { 0, 0 };
    if(wait_ms != -1) {
        tval.tv_sec = wait_ms/1000;
        tval.tv_usec = (wait_ms - tval.tv_sec*1000)*1000;
    }
    int nready = ::select(max_fd_ + 1, &readfds, &writefds, &exceptfds, wait_ms == -1 ? NULL : &tval);
    if (nready <= 0) {
        return KMError::NOERR;
    }
    // copy poll fds since event handler may unregister fd
    PollFdVector poll_fds = poll_fds_;
    int fds_count = int(poll_fds.size());
    for (int i = 0; i < fds_count && nready > 0; ++i) {
        KMEvent revents = 0;
        SOCKET_FD fd = poll_fds[i].fd;
        if(FD_ISSET(fd, &readfds)) {
            revents |= KUMA_EV_READ;
            --nready;
        }
        if(nready > 0 && FD_ISSET(fd, &writefds)) {
            revents |= KUMA_EV_WRITE;
            --nready;
        }
        if(nready > 0 && FD_ISSET(fd, &exceptfds)) {
            revents |= KUMA_EV_ERROR;
            --nready;
        }
        if (fd < poll_items_.size()) {
            revents &= poll_items_[fd].events;
            if (revents) {
                auto &cb = poll_items_[fd].cb;
                if (cb) cb(revents, nullptr, 0);
            }
        }
    }
    return KMError::NOERR;
}

void SelectPoll::notify()
{
    notifier_->notify();
}

IOPoll* createSelectPoll() {
    return new SelectPoll();
}

KUMA_NS_END
