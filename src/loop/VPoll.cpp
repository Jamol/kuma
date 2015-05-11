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
    virtual int registerFd(int fd, uint32_t events, IOCallback& cb);
    virtual int registerFd(int fd, uint32_t events, IOCallback&& cb);
    virtual int unregisterFd(int fd);
    virtual int updateFd(int fd, uint32_t events);
    virtual int wait(uint32_t wait_ms);
    virtual void notify();
    
private:
    uint32_t get_events(uint32_t kuma_events);
    uint32_t get_kuma_events(uint32_t events);
    
private:
    class PollItem
    {
    public:
        PollItem() : fd(-1), index(-1) { }
        
        friend class VPoll;
    protected:
        int fd;
        IOCallback cb;
        int index;
    };
    
private:
    Notifier        notifier_;
    PollItem*       poll_items_;
    struct pollfd*  poll_fds_;
    int             fds_alloc_;
    int             fds_used_;
};

VPoll::VPoll()
{
    poll_fds_ = NULL;
    fds_alloc_ = 0;
    fds_used_ = 0;
}

VPoll::~VPoll()
{
    if(poll_items_) {
        delete[] poll_items_;
        poll_items_ = NULL;
    }
    if(poll_fds_) {
        delete[] poll_fds_;
        poll_fds_ = NULL;
    }
}

bool VPoll::init()
{
    notifier_.init();
    IOCallback cb ([this] (uint32_t ev) { notifier_.onEvent(ev); });
    registerFd(notifier_.getReadFD(), KUMA_EV_READ|KUMA_EV_ERROR, std::move(cb));
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

int VPoll::registerFd(int fd, uint32_t events, IOCallback& cb)
{
    if (fd >= fds_alloc_) {
        int tmp_num = fds_alloc_ + 1024;
        if (tmp_num < fd + 1)
            tmp_num = fd + 1;
        
        PollItem *newItems = new PollItem[tmp_num];
        if(fds_alloc_) {
            memcpy(newItems, poll_items_, fds_alloc_*sizeof(PollItem));
        }
        
        delete[] poll_items_;
        poll_items_ = newItems;
        
        pollfd *newpfds = new pollfd[tmp_num];
        memset((uint8_t*)newpfds, 0, tmp_num*sizeof(pollfd));
        if(fds_alloc_) {
            memcpy(newpfds, poll_fds_, fds_alloc_*sizeof(pollfd));
        }
        
        delete[] poll_fds_;
        poll_fds_ = newpfds;
        fds_alloc_ = tmp_num;
    }
    
    poll_items_[fd].fd = fd;
    poll_items_[fd].cb = cb;
    poll_items_[fd].index = fds_used_;
    poll_fds_[fds_used_].fd = fd;
    poll_fds_[fds_used_].events = get_events(events);
    KUMA_INFOTRACE("VPoll::registerFd, fd="<<fd<<", index="<<fds_used_);
    ++fds_used_;
    
    return KUMA_ERROR_NOERR;
}

int VPoll::registerFd(int fd, uint32_t events, IOCallback&& cb)
{
    if (fd >= fds_alloc_) {
        int tmp_num = fds_alloc_ + 1024;
        if (tmp_num < fd + 1)
            tmp_num = fd + 1;
        
        PollItem *newItems = new PollItem[tmp_num];
        if(fds_alloc_) {
            memcpy(newItems, poll_items_, fds_alloc_*sizeof(PollItem));
        }
        
        delete[] poll_items_;
        poll_items_ = newItems;
        
        pollfd *newpfds = new pollfd[tmp_num];
        memset((uint8_t*)newpfds, 0, tmp_num*sizeof(pollfd));
        if(fds_alloc_) {
            memcpy(newpfds, poll_fds_, fds_alloc_*sizeof(pollfd));
        }
        
        delete[] poll_fds_;
        poll_fds_ = newpfds;
        fds_alloc_ = tmp_num;
    }
    
    poll_items_[fd].fd = fd;
    poll_items_[fd].cb = std::move(cb);
    poll_items_[fd].index = fds_used_;
    poll_fds_[fds_used_].fd = fd;
    poll_fds_[fds_used_].events = get_events(events);
    KUMA_INFOTRACE("VPoll::registerFd, fd="<<fd<<", index="<<fds_used_);
    ++fds_used_;
    
    return KUMA_ERROR_NOERR;
}

int VPoll::unregisterFd(int fd)
{
    KUMA_INFOTRACE("VPoll::unregisterFd, fd="<<fd);
    if ((fd < 0) || (fd >= fds_alloc_) || 0 == fds_used_) {
        KUMA_WARNTRACE("VPoll::unregisterFd, failed, alloced="<<fds_alloc_<<", used="<<fds_used_);
        return KUMA_ERROR_INVALID_PARAM;
    }
    int pfds_index = poll_items_[fd].index;
    
    poll_items_[fd].cb = nullptr;
    poll_items_[fd].fd = INVALID_FD;
    poll_items_[fd].index = -1;
    
    if (pfds_index != fds_used_ - 1) {
        poll_fds_[pfds_index] = poll_fds_[fds_used_ - 1];
        poll_items_[poll_fds_[pfds_index].fd].index = pfds_index;
    }
    poll_fds_[fds_used_ - 1].fd = -1;
    poll_fds_[fds_used_ - 1].events = 0;
    --fds_used_;
    return KUMA_ERROR_NOERR;
}

int VPoll::updateFd(int fd, uint32_t events)
{
    if ((fd < 0) || (fd >= fds_alloc_) || 0 == fds_used_) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, alloced: "<<fds_alloc_<<", used: "<<fds_used_);
        return KUMA_ERROR_INVALID_PARAM;
    }
    if(poll_items_[fd].fd != fd) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, fd: "<<fd<<", m_fd: "<<poll_items_[fd].fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    int pfds_index = poll_items_[fd].index;
    if(pfds_index < 0 || pfds_index >= fds_used_) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, pfd_index: "<<pfds_index);
        return KUMA_ERROR_INVALID_STATE;
    }
    if(poll_fds_[pfds_index].fd != fd) {
        KUMA_WARNTRACE("VPoll::updateFd, failed, fd: "<<fd<<", pfds_fd: "<<poll_fds_[pfds_index].fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    poll_fds_[pfds_index].events = get_events(events);
    return KUMA_ERROR_NOERR;
}

int VPoll::wait(uint32_t wait_ms)
{
    int num_revts = poll(poll_fds_, fds_used_, wait_ms);
    if (-1 == num_revts) {
        if(EINTR == errno) {
            errno = 0;
        } else {
            KUMA_ERRTRACE("VPoll::wait, errno: "<<errno);
        }
        return KUMA_ERROR_INVALID_STATE;
    }

    int cur_pfds_index = 0;
    while(num_revts > 0 && cur_pfds_index < fds_used_) {
        if(poll_fds_[cur_pfds_index].revents) {
            --num_revts;
            IOCallback &cb = poll_items_[poll_fds_[cur_pfds_index].fd].cb;
            if(cb) cb(get_kuma_events(poll_fds_[cur_pfds_index].revents));
        }
        ++cur_pfds_index;
    }
    return KUMA_ERROR_NOERR;
}

void VPoll::notify()
{
    notifier_.notify();
}

IOPoll* createVPoll() {
    return new VPoll();
}

KUMA_NS_END

