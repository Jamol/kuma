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

#ifndef __UdpSocketImpl_H__
#define __UdpSocketImpl_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "evdefs.h"
#include "UdpSocketBase.h"
#include <stdint.h>
#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <netinet/in.h>
#endif

KUMA_NS_BEGIN

class UdpSocket::Impl
{
public:
    using EventCallback = UdpSocket::EventCallback;
    
    Impl(const EventLoopPtr &loop);
    ~Impl();
    
    KMError bind(const std::string &bind_host, uint16_t bind_port, uint32_t udp_flags);
    int send(const void* data, size_t length, const std::string &host, uint16_t port);
    int send(const iovec* iovs, int count, const std::string &host, uint16_t port);
    int send(const KMBuffer &buf, const char* host, uint16_t port);
    int receive(void* data, size_t length, char* ip, size_t ip_len, uint16_t& port);
    KMError close();
    
    KMError mcastJoin(const std::string &mcast_addr, uint16_t mcast_port);
    KMError mcastLeave(const std::string &mcast_addr, uint16_t mcast_port);

    void setReadCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);
    
private:
    std::unique_ptr<UdpSocketBase> socket_;
};

KUMA_NS_END

#endif
