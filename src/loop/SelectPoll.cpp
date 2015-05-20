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
    
    bool init();
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb);
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb);
    int unregisterFd(SOCKET_FD fd);
    int updateFd(SOCKET_FD fd, uint32_t events);
    int wait(uint32_t wait_time_ms);
    void notify();
    PollType getType() { return POLL_TYPE_SELECT; }

private:
    void resizePollItems(SOCKET_FD fd);
    
private:
    Notifier                notifier_;
    PollItemVector          poll_items_;
    std::vector<SOCKET_FD>  poll_fds_;
};

SelectPoll::SelectPoll()
{
    
}

SelectPoll::~SelectPoll()
{
    poll_fds_.clear();
    poll_items_.clear();
}

bool SelectPoll::init()
{
    notifier_.init();
    IOCallback cb ([this] (uint32_t ev) { notifier_.onEvent(ev); });
    registerFd(notifier_.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, std::move(cb));
    return true;
}

void SelectPoll::resizePollItems(SOCKET_FD fd)
{
    if (fd >= poll_items_.size()) {
        poll_items_.resize(fd + 1);
    }
}

int SelectPoll::registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb)
{
    KUMA_INFOTRACE("SelectPoll::registerFd, fd="<<fd);
    resizePollItems(fd);
    if (INVALID_FD == poll_items_[fd].fd || -1 == poll_items_[fd].idx) {
        poll_fds_.push_back(fd);
        poll_items_[fd].idx = int(poll_fds_.size() - 1);
    }
    poll_items_[fd].fd = fd;
    poll_items_[fd].cb = cb;
    poll_items_[fd].ev = events;
    return KUMA_ERROR_NOERR;
}

int SelectPoll::registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb)
{
    KUMA_INFOTRACE("SelectPoll::registerFd, fd=" << fd);
    resizePollItems(fd);
    if (INVALID_FD == poll_items_[fd].fd || -1 == poll_items_[fd].idx) {
        poll_fds_.push_back(fd);
        poll_items_[fd].idx = int(poll_fds_.size() - 1);
    }
    poll_items_[fd].fd = fd;
    poll_items_[fd].cb = std::move(cb);
    poll_items_[fd].ev = events;
    return KUMA_ERROR_NOERR;
}

int SelectPoll::unregisterFd(SOCKET_FD fd)
{
    KUMA_INFOTRACE("SelectPoll::unregisterFd, fd="<<fd);
    int max_fd = int(poll_items_.size() - 1);
    if (fd < 0 || fd > max_fd) {
        KUMA_WARNTRACE("SelectPoll::unregisterFd, failed, max_fd=" << max_fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    int idx = poll_items_[fd].idx;
    if (fd == max_fd) {
        poll_items_.pop_back();
    } else {
        poll_items_[fd].fd = INVALID_FD;
        poll_items_[fd].cb = nullptr;
        poll_items_[fd].ev = 0;
        poll_items_[fd].idx = -1;
    }
    int last_idx = int(poll_fds_.size() - 1);
    if (idx > last_idx || -1 == idx) {
        return KUMA_ERROR_NOERR;
    }
    if (idx != last_idx) {
        poll_fds_[idx] = poll_fds_[last_idx];
        std::iter_swap(poll_fds_.begin() + idx, poll_fds_.end() - 1);
        poll_items_[poll_fds_[idx]].idx = idx;
    }
    poll_fds_.pop_back();
    return KUMA_ERROR_NOERR;
}

int SelectPoll::updateFd(SOCKET_FD fd, uint32_t events)
{
    return KUMA_ERROR_NOERR;
}

int SelectPoll::wait(uint32_t wait_ms)
{
    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    struct timeval tval;
    if(wait_ms > 0) {
        tval.tv_sec = wait_ms/1000;
        tval.tv_usec = (wait_ms - tval.tv_sec*1000)*1000;
    }
    SOCKET_FD max_fd = 0;
    int fds_count = int(poll_fds_.size());
    int item_count = int(poll_items_.size());
    for (int i = 0; i<fds_count; ++i) {
        SOCKET_FD fd = poll_fds_[i];
        if (fd != INVALID_FD && fd < item_count && poll_items_[fd].ev != 0) {
            if (poll_items_[fd].ev | KUMA_EV_READ) {
                FD_SET(fd, &readfds);
            }
            if (poll_items_[fd].ev | KUMA_EV_WRITE) {
                FD_SET(fd, &writefds);
            }
            if (poll_items_[fd].ev | KUMA_EV_ERROR) {
                FD_SET(fd, &exceptfds);
            }
            if (fd > max_fd) {
                max_fd = fd;
            }
        }
    }
    int nready = ::select(max_fd + 1, &readfds, &writefds, &exceptfds, wait_ms == -1 ? NULL : &tval);
    if (nready <= 0) {
        return KUMA_ERROR_NOERR;
    }
    for (int i = 0; i < fds_count && nready > 0; ++i) {
        uint32_t events = 0;
        SOCKET_FD fd = poll_fds_[i];
        if(FD_ISSET(fd, &readfds)) {
            events |= KUMA_EV_READ;
            --nready;
        }
        if(nready > 0 && FD_ISSET(fd, &writefds)) {
            events |= KUMA_EV_WRITE;
            --nready;
        }
        if(nready > 0 && FD_ISSET(fd, &exceptfds)) {
            events |= KUMA_EV_ERROR;
            --nready;
        }
        if (fd < item_count) {
            IOCallback& cb = poll_items_[fd].cb;
            if (cb) {
                cb(events);
            }
        }
    }
    return KUMA_ERROR_NOERR;
}

void SelectPoll::notify()
{
    notifier_.notify();
}

IOPoll* createSelectPoll() {
    return new SelectPoll();
}

KUMA_NS_END
