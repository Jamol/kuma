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

#ifndef __PollSocket_H__
#define __PollSocket_H__

#include "kmdefs.h"
#include "evdefs.h"
#include "EventLoopImpl.h"
#include "DnsResolver.h"
#include "SocketBase.h"
#include "util/DestroyDetector.h"
KUMA_NS_BEGIN

class PollSocket : public SocketBase, public DestroyDetector
{
public:
    PollSocket(const EventLoopPtr &loop);
    ~PollSocket();

    KMError attachFd(SOCKET_FD fd) override;
    KMError detachFd(SOCKET_FD &fd) override;
    int send(const void* data, size_t length) override;
    int send(iovec* iovs, int count) override;
    KMError pause() override;
    KMError resume() override;
    KMError close() override;
    void notifySendBlocked() override;
protected:
    KMError connect_i(const sockaddr_storage &ss_addr, uint32_t timeout_ms) override;
    void registerFd(SOCKET_FD fd) override;
    void unregisterFd(SOCKET_FD fd, bool close_fd) override;

protected:
    void onSend(KMError err) override;
    void ioReady(KMEvent events);
};

KUMA_NS_END

#endif
