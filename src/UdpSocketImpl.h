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

#ifndef __UdpSocketImpl_H__
#define __UdpSocketImpl_H__

#include "kmdefs.h"
#include "evdefs.h"
#include "util/kmobject.h"
#include "util/DestroyDetector.h"
#include <stdint.h>
#ifdef KUMA_OS_WIN
# include <Ws2tcpip.h>
#else
# include <netinet/in.h>
#endif

KUMA_NS_BEGIN

class EventLoopImpl;

class UdpSocketImpl : public KMObject, public DestroyDetector
{
public:
    typedef std::function<void(int)> EventCallback;
    
    UdpSocketImpl(EventLoopImpl* loop);
    ~UdpSocketImpl();
    
    int bind(const char* bind_host, uint16_t bind_port, uint32_t udp_flags);
    int send(const uint8_t* data, size_t length, const char* host, uint16_t port);
    int send(iovec* iovs, int count, const char* host, uint16_t port);
    int receive(uint8_t* data, size_t length, char* ip, size_t ip_len, uint16_t& port);
    int close();
    
    int mcastJoin(const char* mcast_addr, uint16_t mcast_port);
    int mcastLeave(const char* mcast_addr, uint16_t mcast_port);

    void setReadCallback(EventCallback cb) { read_cb_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { error_cb_ = std::move(cb); }
    
private:
    void setSocketOption();
    void ioReady(uint32_t events);
    void onSend(int err);
    void onReceive(int err);
    void onClose(int err);
    void cleanup();
    
private:
    SOCKET_FD       fd_ = INVALID_FD;
    EventLoopImpl*  loop_;
    bool            registered_ = false;
    uint32_t        flags_ = 0;
    
    EventCallback   read_cb_;
    EventCallback   error_cb_;
    
    sockaddr_storage    bind_addr_;
    sockaddr_storage    mcast_addr_;
    uint16_t            mcast_port_;
    struct ip_mreq      mcast_req_v4_;
    struct ipv6_mreq    mcast_req_v6_;
};

KUMA_NS_END

#endif
