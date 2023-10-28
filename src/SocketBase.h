/* Copyright (c) 2014-2017, Fengping Bao <jamol@live.com>
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

#ifndef __SocketBase_H__
#define __SocketBase_H__

#include "kmdefs.h"
#include "EventLoopImpl.h"
#include "DnsResolver.h"
#include "libkev/src/utils/kmobject.h"
#include "libkev/src/utils/DestroyDetector.h"
KUMA_NS_BEGIN

class SocketBase : public kev::KMObject, public kev::DestroyDetector
{
public:
    using EventCallback = std::function<void(KMError)>;

    SocketBase(const EventLoopPtr &loop);
    SocketBase(const SocketBase&) = delete;
    SocketBase(SocketBase&& other) = delete;
    virtual ~SocketBase();

    SocketBase& operator= (const SocketBase&) = delete;
    SocketBase& operator= (SocketBase&& other) = delete;

    KMError bind(const std::string &bind_host, uint16_t bind_port);
    KMError connect(const std::string &host, uint16_t port, EventCallback cb, uint32_t timeout_ms = 0);
    virtual KMError attachFd(SOCKET_FD fd);
    virtual KMError detachFd(SOCKET_FD &fd);
    virtual int send(const void *data, size_t length);
    virtual int send(const iovec *iovs, int count);
    virtual int send(const KMBuffer &buf);
    virtual int receive(void *data, size_t length);
    virtual KMError pause();
    virtual KMError resume();
    virtual KMError close();

    virtual void notifySendBlocked();
    SOCKET_FD getFd() const { return fd_; }
    EventLoopPtr eventLoop() const { return loop_.lock(); }
    bool isReady() const { return getState() == State::OPEN; }
    bool isConnecting() const
    {
        return getState() == State::RESOLVING ||
               getState() == State::CONNECTING;
    }

    void setReadCallback(EventCallback cb) { read_cb_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { write_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }

protected:
    enum State {
        IDLE,
        RESOLVING,
        CONNECTING,
        OPEN,
        CLOSED
    };
    State getState() const { return state_; }
    void setState(State state) { state_ = state; }
    void setSocketOption();
    KMError connect_i(const std::string &addr, uint16_t port, uint32_t timeout_ms);
    virtual KMError connect_i(const sockaddr_storage &ss_addr, uint32_t timeout_ms);
    void cleanup();
    virtual bool registerFd(SOCKET_FD fd);
    virtual void unregisterFd(SOCKET_FD fd, bool close_fd);
    virtual SOCKET_FD createFd(int addr_family);
    virtual void notifySendReady();

protected:
    void onResolved(KMError err, const sockaddr_storage &addr);

    virtual void ioReady(KMEvent events, void *ol, size_t io_size);
    virtual void onConnect(KMError err);
    virtual void onSend(KMError err);
    virtual void onReceive(KMError err);
    virtual void onClose(KMError err);

protected:
    SOCKET_FD           fd_{ INVALID_FD };
    EventLoopWeakPtr    loop_;
    State               state_{ State::IDLE };
    bool                registered_{ false };
    DnsResolver::Token  dns_token_;

    EventCallback       connect_cb_;
    EventCallback       read_cb_;
    EventCallback       write_cb_;
    EventCallback       error_cb_;

    std::unique_ptr<Timer::Impl> timer_;
};

KUMA_NS_END

#endif
