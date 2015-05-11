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
    virtual int registerFd(int fd, uint32_t events, IOCallback& cb);
    virtual int registerFd(int fd, uint32_t events, IOCallback&& cb);
    virtual int unregisterFd(int fd);
    virtual int updateFd(int fd, uint32_t events);
    virtual int wait(uint32_t wait_time_ms);
    virtual void notify();
    
private:
    struct IOItem
    {
        IOItem() : events(0) {}
        IOCallback cb;
        uint32_t events;
    };
    
private:
    Notifier    notifier_;
    IOItem*     io_items_;
    int         fds_alloc_;
    int         fds_used_;
};

SelectPoll::SelectPoll()
{
    io_items_ = NULL;
    fds_alloc_ = 0;
    fds_used_ = 0;
}

SelectPoll::~SelectPoll()
{
    if(io_items_) {
        delete[] io_items_;
        io_items_ = NULL;
    }
}

bool SelectPoll::init()
{
    notifier_.init();
    IOCallback cb ([this] (uint32_t ev) { notifier_.onEvent(ev); });
    registerFd(notifier_.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, std::move(cb));
    return true;
}

int SelectPoll::registerFd(int fd, uint32_t events, IOCallback& cb)
{
    KUMA_INFOTRACE("SelectPoll::registerFd, fd="<<fd);
    if (fd >= fds_alloc_) {
        int tmp_num = fds_alloc_ + 1024;
        if (tmp_num < fd + 1)
            tmp_num = fd + 1;
        
        IOItem *newItems = new IOItem[tmp_num];
        if(fds_alloc_ > 0) {
            memcpy(newItems, io_items_, fds_alloc_*sizeof(IOItem));
        }
        
        delete[] io_items_;
        io_items_ = newItems;
    }
    io_items_[fd].cb = cb;
    io_items_[fd].events = events;
    ++fds_used_;
    return KUMA_ERROR_NOERR;
}

int SelectPoll::registerFd(int fd, uint32_t events, IOCallback&& cb)
{
    KUMA_INFOTRACE("SelectPoll::registerFd, fd=" << fd);
    if (fd >= fds_alloc_) {
        int tmp_num = fds_alloc_ + 1024;
        if (tmp_num < fd + 1)
            tmp_num = fd + 1;

        IOItem *newItems = new IOItem[tmp_num];
        if (fds_alloc_ > 0) {
            memcpy(newItems, io_items_, fds_alloc_*sizeof(IOItem));
        }

        delete[] io_items_;
        io_items_ = newItems;
    }
    io_items_[fd].cb = std::move(cb);
    io_items_[fd].events = events;
    ++fds_used_;
    return KUMA_ERROR_NOERR;
}

int SelectPoll::unregisterFd(int fd)
{
    KUMA_INFOTRACE("SelectPoll::unregisterFd, fd="<<fd);
    if ((fd < 0) || (fd >= fds_alloc_) || 0 == fds_used_) {
        KUMA_WARNTRACE("SelectPoll::unregisterFd, failed, alloced="<<fds_alloc_<<", used="<<fds_used_);
        return KUMA_ERROR_INVALID_PARAM;
    }
    
    io_items_[fd].cb = nullptr;
    io_items_[fd].events = 0;
    --fds_used_;
    return KUMA_ERROR_NOERR;
}

int SelectPoll::updateFd(int fd, uint32_t events)
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
    int maxfd = 0;
    for (int i=0; i<fds_alloc_; ++i) {
        if(io_items_[i].cb && io_items_[i].events != 0) {
            if(io_items_[i].events|KUMA_EV_READ) {
                FD_SET(i, &readfds);
            }
            if(io_items_[i].events|KUMA_EV_WRITE) {
                FD_SET(i, &writefds);
            }
            if(io_items_[i].events|KUMA_EV_ERROR) {
                FD_SET(i, &exceptfds);
            }
            if(i > maxfd) {
                maxfd = i;
            }
        }
    }
    int nready = ::select(maxfd+1, &readfds, &writefds, &exceptfds, wait_ms == -1 ? NULL : &tval);
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
        IOCallback& cb = io_items_[i].cb;
        if(cb) {
            cb(events);
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