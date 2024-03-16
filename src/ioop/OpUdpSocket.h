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

#ifndef __OpUdpSocket_H__
#define __OpUdpSocket_H__

#include "kmdefs.h"
#include "EventLoopImpl.h"
#include "DnsResolver.h"
#include "UdpSocketBase.h"
#include "OpContext.h"

KUMA_NS_BEGIN

class OpUdpSocket : public UdpSocketBase
{
public:
    OpUdpSocket(const EventLoopPtr &loop);
    ~OpUdpSocket();

    int receive(void* data, size_t length, char* ip, size_t ip_len, uint16_t& port) override;
    
protected:
#if defined(KUMA_OS_WIN)
    SOCKET_FD createFd(int addr_family) override;
#endif
    bool registerFd(SOCKET_FD fd) override;
    void unregisterFd(SOCKET_FD fd, bool close_fd) override;

    bool postRecvOp();
    bool recvBlocked() const { return pending_recv_ops_ > 0; }

protected:
    void onSocketInitialized() override;
    void onReceive(int res, const KMBuffer &buf);

protected:
    bool readable_ = false;

    OpContext::Ptr op_ctx_;

    int pending_recv_ops_{0};
    KMBuffer recv_buf_;

private:
    using UdpSocketBase::onReceive;
};

KUMA_NS_END

#endif // __OpUdpSocket_H__
