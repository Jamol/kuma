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

#ifndef __AcceptorBase_H__
#define __AcceptorBase_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "libkev/src/utils/kmobject.h"
#include "EventLoopImpl.h"
KUMA_NS_BEGIN

class AcceptorBase : public kev::KMObject
{
public:
    using AcceptCallback = TcpListener::AcceptCallback;
    using ErrorCallback = TcpListener::ErrorCallback;
    
    AcceptorBase(const EventLoopPtr &loop);
    virtual ~AcceptorBase();
    
    virtual KMError listen(const std::string &host, uint16_t port);
    virtual KMError close();
    
    void setAcceptCallback(AcceptCallback cb) { accept_cb_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { error_cb_ = std::move(cb); }
    
    SOCKET_FD getFd() const { return fd_; }
    EventLoopPtr eventLoop() const { return loop_.lock(); }
    
protected:
    virtual bool registerFd(SOCKET_FD fd);
    virtual void unregisterFd(SOCKET_FD fd, bool close_fd);

protected:
    void setSocketOption();
    virtual void onAccept();
    void onAccept(SOCKET_FD fd);
    void onClose(KMError err);
    void cleanup();
    virtual void ioReady(KMEvent events, void* ol, size_t io_size);
    
protected:
    SOCKET_FD           fd_{ INVALID_FD };
    EventLoopWeakPtr    loop_;
    bool                registered_{ false };
    uint32_t            flags_{ 0 };
    bool                closed_{ false };
#ifdef KUMA_OS_WIN
    ADDRESS_FAMILY
#else
    sa_family_t
#endif
                        ss_family_ = AF_INET;
    
    AcceptCallback      accept_cb_;
    ErrorCallback       error_cb_;
};

KUMA_NS_END

#endif
