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
#include "util/kmtrace.h"
#include "util/util.h"

KUMA_NS_BEGIN

class IocpPoll : public IOPoll
{
public:
    IocpPoll();
    ~IocpPoll();
    
    bool init();
    KMError registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb);
    KMError unregisterFd(SOCKET_FD fd);
    KMError updateFd(SOCKET_FD fd, uint32_t events);
    KMError wait(uint32_t wait_ms);
    void notify();
    PollType getType() const { return PollType::IOCP; }
    bool isLevelTriggered() const { return true; }
    
protected:
    uint32_t get_events(uint32_t kuma_events);
    uint32_t get_kuma_events(uint32_t events);

protected:
    HANDLE hCompPort_ = nullptr;
};

IocpPoll::IocpPoll()
{
    
}

IocpPoll::~IocpPoll()
{
    if (hCompPort_) {
        CloseHandle(hCompPort_);
        hCompPort_ = nullptr;
    }
}

bool IocpPoll::init()
{
    hCompPort_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (!hCompPort_) {
        KUMA_ERRTRACE("IocpPoll::init, CreateIoCompletionPort failed, err=" << GetLastError());
        return false;
    }
    return true;
}

uint32_t IocpPoll::get_events(uint32_t kuma_events)
{
    uint32_t ev = 0;
    if(kuma_events & KUMA_EV_READ) {
        ev |= FD_READ;
    }
    if(kuma_events & KUMA_EV_WRITE) {
        ev |= FD_WRITE;
    }
    if(kuma_events & KUMA_EV_ERROR) {
        ev |= FD_CLOSE;
    }
    return ev;
}

uint32_t IocpPoll::get_kuma_events(uint32_t events)
{
    uint32_t ev = 0;
    if (events & FD_CONNECT) {
        ev |= KUMA_EV_WRITE;
    }
    if (events & FD_ACCEPT) {
        ev |= KUMA_EV_READ;
    }
    if(events & FD_READ) {
        ev |= KUMA_EV_READ;
    }
    if(events & FD_WRITE) {
        ev |= KUMA_EV_WRITE;
    }
    if(events & FD_CLOSE) {
        ev |= KUMA_EV_ERROR;
    }
    return ev;
}

KMError IocpPoll::registerFd(SOCKET_FD fd, uint32_t events, IOCallback cb)
{
    KUMA_INFOTRACE("WinPoll::registerFd, fd=" << fd << ", events=" << events);
    if (CreateIoCompletionPort((HANDLE)fd, hCompPort_, (ULONG_PTR)fd, 0) == NULL) {
        return KMError::POLL_ERROR;
    }
    resizePollItems(fd);
    poll_items_[fd].fd = fd;
    poll_items_[fd].cb = std::move(cb);
    return KMError::NOERR;
}

KMError IocpPoll::unregisterFd(SOCKET_FD fd)
{
    KUMA_INFOTRACE("WinPoll::unregisterFd, fd="<<fd);
    SOCKET_FD max_fd = poll_items_.size() - 1;
    if (fd < 0 || -1 == max_fd || fd > max_fd) {
        KUMA_WARNTRACE("WinPoll::unregisterFd, failed, max_fd=" << max_fd);
        return KMError::INVALID_PARAM;
    }
    if (fd == max_fd) {
        poll_items_.pop_back();
    } else {
        poll_items_[fd].cb = nullptr;
        poll_items_[fd].fd = INVALID_FD;
    }
    
    return KMError::NOERR;
}

KMError IocpPoll::updateFd(SOCKET_FD fd, uint32_t events)
{
    SOCKET_FD max_fd = poll_items_.size() - 1;
    if (fd < 0 || -1 == max_fd || fd > max_fd) {
        KUMA_WARNTRACE("WinPoll::updateFd, failed, fd="<<fd<<", max_fd=" << max_fd);
        return KMError::INVALID_PARAM;
    }
    if(poll_items_[fd].fd != fd) {
        KUMA_WARNTRACE("WinPoll::updateFd, failed, fd="<<fd<<", fd1="<<poll_items_[fd].fd);
        return KMError::INVALID_PARAM;
    }
    return KMError::NOERR;
}

KMError IocpPoll::wait(uint32_t wait_ms)
{
    OVERLAPPED_ENTRY entries[128];
    ULONG count = 0;
    auto success = GetQueuedCompletionStatusEx(hCompPort_, entries, ARRAY_SIZE(entries), &count, wait_ms, FALSE);
    if (success) {
        for (ULONG i = 0; i < count; ++i) {
            if (entries[i].lpOverlapped) {
                SOCKET_FD fd = (SOCKET_FD)entries[i].lpCompletionKey;
                if (fd < poll_items_.size()) {
                    IOCallback &cb = poll_items_[fd].cb;
                    uint32_t revents = get_kuma_events(0);
                    if (cb) cb(revents);
                }
            }
        }
    }
    else {
        auto err = getLastError();
        if (err != WAIT_TIMEOUT) {
            KUMA_ERRTRACE("IocpPoll::wait, err="<<err);
        }
    }
    return KMError::NOERR;
}

void IocpPoll::notify()
{
    if (hCompPort_ != nullptr) {
        PostQueuedCompletionStatus(hCompPort_, 0, 0, NULL);
    }
}

IOPoll* createIocpPoll() {
    return new IocpPoll();
}

KUMA_NS_END
