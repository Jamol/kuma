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

#ifndef __TcpListenerImpl_H__
#define __TcpListenerImpl_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "evdefs.h"
#include "util/kmobject.h"
KUMA_NS_BEGIN

class TcpListener::Impl : public KMObject
{
public:
    using ListenCallback = TcpListener::ListenCallback;
    using ErrorCallback = TcpListener::ErrorCallback;
    
    Impl(EventLoop::Impl* loop);
    ~Impl();
    
    KMError startListen(const char* host, uint16_t port);
    KMError stopListen(const char* host, uint16_t port);
    KMError close();
    
    void setListenCallback(ListenCallback cb) { accept_cb_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { error_cb_ = std::move(cb); }
    
    SOCKET_FD getFd() const { return fd_; }
    
private:
    void setSocketOption();
    void ioReady(uint32_t events);
    void onAccept();
    void onClose(KMError err);
    
private:
    void cleanup();
    
private:
    SOCKET_FD           fd_{ INVALID_FD };
    EventLoop::Impl*    loop_;
    bool                registered_{ false };
    uint32_t            flags_{ 0 };
    bool                stopped_{ false };
    
    ListenCallback      accept_cb_;
    ErrorCallback       error_cb_;
};

KUMA_NS_END

#endif
