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

#ifndef __UdpSocketBase_H__
#define __UdpSocketBase_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "libkev/src/util/kmobject.h"
#include "libkev/src/util/DestroyDetector.h"
#include "EventLoopImpl.h"
#include <stdint.h>
#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <netinet/in.h>
#endif

KUMA_NS_BEGIN

class UdpSocketBase : public kev::KMObject, public kev::DestroyDetector
{
public:
    using EventCallback = UdpSocket::EventCallback;
    
    UdpSocketBase(const EventLoopPtr &loop);
    virtual ~UdpSocketBase();
    
    virtual KMError bind(const std::string &bind_host, uint16_t bind_port, uint32_t udp_flags);
    virtual int send(const void* data, size_t length, const std::string &host, uint16_t port);
    virtual int send(const iovec* iovs, int count, const std::string &host, uint16_t port);
    virtual int send(const KMBuffer &buf, const char* host, uint16_t port);
    virtual int receive(void* data, size_t length, char* ip, size_t ip_len, uint16_t& port);
    virtual KMError close();
    
    KMError mcastJoin(const std::string &mcast_addr, uint16_t mcast_port);
    KMError mcastLeave(const std::string &mcast_addr, uint16_t mcast_port);

    void setReadCallback(EventCallback cb) { read_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }

    EventLoopPtr eventLoop() const { return loop_.lock(); }
    
protected:
    void setSocketOption();
    virtual void onSend(KMError err);
    virtual void onReceive(KMError err);
    virtual void onClose(KMError err);
    void cleanup();
    virtual bool registerFd(SOCKET_FD fd);
    virtual void unregisterFd(SOCKET_FD fd, bool close_fd);
    virtual void ioReady(KMEvent events, void* ol, size_t io_size);

    virtual void notifySendBlocked();
    virtual void notifySendReady();
    
protected:
    SOCKET_FD           fd_{ INVALID_FD };
    EventLoopWeakPtr    loop_;
    bool                registered_{ false };
    uint32_t            flags_{ 0 };
    
    EventCallback       read_cb_;
    EventCallback       error_cb_;
    
    sockaddr_storage    bind_addr_;
    sockaddr_storage    mcast_addr_;
    uint16_t            mcast_port_;
    struct ip_mreq      mcast_req_v4_;
    struct ipv6_mreq    mcast_req_v6_;
};

KUMA_NS_END

#endif
