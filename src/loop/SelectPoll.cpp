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
    virtual int register_fd(int fd, uint32_t events, IOHandler* handler);
    virtual int unregister_fd(int fd);
    virtual int modify_events(int fd, uint32_t events, IOHandler* handler);
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
    Notifier    notifier_;
    SelectItem* poll_items_;
    int         fds_alloc_;
    int         fds_used_;
};

SelectPoll::SelectPoll()
{
    poll_items_ = NULL;
    fds_alloc_ = 0;
    fds_used_ = 0;
}

SelectPoll::~SelectPoll()
{
    if(poll_items_) {
        delete[] poll_items_;
        poll_items_ = NULL;
    }
}

bool SelectPoll::init()
{
    notifier_.init();
    register_fd(notifier_.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, &notifier_);
    return true;
}

int SelectPoll::register_fd(int fd, uint32_t events, IOHandler* handler)
{
    KUMA_INFOTRACE("SelectPoll::register_fd, fd="<<fd);
    if (fd >= fds_alloc_) {
        int tmp_num = fds_alloc_ + 1024;
        if (tmp_num < fd + 1)
            tmp_num = fd + 1;
        
        SelectItem *newItems = new SelectItem[tmp_num];
        if(fds_alloc_ > 0) {
            memcpy(newItems, poll_items_, fds_alloc_*sizeof(SelectItem));
        }
        
        delete[] poll_items_;
        poll_items_ = newItems;
    }
    poll_items_[fd].handler = handler;
    poll_items_[fd].events = events;
    ++fds_used_;
    return KUMA_ERROR_NOERR;
}

int SelectPoll::unregister_fd(int fd)
{
    KUMA_INFOTRACE("SelectPoll::unregister_fd, fd="<<fd);
    if ((fd < 0) || (fd >= fds_alloc_) || 0 == fds_used_) {
        KUMA_WARNTRACE("SelectPoll::unregister_fd, failed, alloced="<<fds_alloc_<<", used="<<fds_used_);
        return KUMA_ERROR_INVALID_PARAM;
    }
    
    poll_items_[fd].handler = NULL;
    poll_items_[fd].events = 0;
    --fds_used_;
    return KUMA_ERROR_NOERR;
}

int SelectPoll::modify_events(int fd, uint32_t events, IOHandler* handler)
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
    for (int i=0; i<fds_alloc_; ++i) {
        if(poll_items_[i].handler != NULL && poll_items_[i].events != 0) {
            if(poll_items_[i].events|KUMA_EV_READ) {
                FD_SET(i, &readfds);
            }
            if(poll_items_[i].events|KUMA_EV_WRITE) {
                FD_SET(i, &writefds);
            }
            if(poll_items_[i].events|KUMA_EV_ERROR) {
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
        IOHandler* handler = poll_items_[i].handler;
        if(handler) {
            handler->onEvent(events);
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