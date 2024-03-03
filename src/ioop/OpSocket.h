/* Copyright (c) 2014-2024, Fengping Bao <jamol@live.com>
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

#ifndef __OpSocket_H__
#define __OpSocket_H__

#include "kmdefs.h"
#include "EventLoopImpl.h"
#include "DnsResolver.h"
#include "SocketBase.h"
#include "OpContext.h"

KUMA_NS_BEGIN

class OpSocket : public SocketBase
{
public:
    OpSocket(const EventLoopPtr &loop);
    ~OpSocket();

    KMError attachFd(SOCKET_FD fd) override;
    KMError detachFd(SOCKET_FD &fd) override;
    int send(const void* data, size_t length) override;
    int send(const iovec* iovs, int count) override;
    int send(const KMBuffer &buf) override;
    int receive(void* data, size_t length) override;
    KMError pause() override;
    KMError resume() override;
    
protected:
    KMError connect_i(const sockaddr_storage &ss_addr, uint32_t timeout_ms) override;
#if defined(KUMA_OS_WIN)
    SOCKET_FD createFd(int addr_family) override;
#endif
    bool registerFd(SOCKET_FD fd) override;
    void unregisterFd(SOCKET_FD fd, bool close_fd) override;

    bool postRecvOp();
    bool sendBlocked() const { return send_blocked_; }
    bool recvBlocked() const { return pending_recv_ops_ > 0; }

protected:
    void onConnect(int res);
    void onSend(int res);
    void onReceive(int res, const KMBuffer &buf);

    void notifySendBlocked() override {}
    void notifySendReady() override {}

protected:
    bool readable_ = false;
    bool paused_ = false;

    OpContext::Ptr op_ctx_;

    size_t pending_send_bytes_{0};
    int pending_send_ops_{0};
    int pending_recv_ops_{0};
    bool send_blocked_{false};

    KMBuffer recv_buf_;
};

KUMA_NS_END

#endif
