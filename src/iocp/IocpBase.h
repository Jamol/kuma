/* Copyright (c) 2014-2017, Fengping Bao <jamol@live.com>
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

#ifndef __IocpBase_H__
#define __IocpBase_H__

#include "kmdefs.h"
#include "util/kmtrace.h"
#include "EventLoopImpl.h"
#include "Iocp.h"

KUMA_NS_BEGIN

class IocpBase
{
public:
    IocpBase(IocpWrapper::Ptr && ctx)
        : iocp_ctx_(std::move(ctx))
    {
        iocp_ctx_->setCallback([this](IocpContext::Op op, size_t io_size) {
            ioReady(op, io_size);
        });
    }

    virtual ~IocpBase() {}

    bool registerFd(const EventLoopPtr &loop, SOCKET_FD fd)
    {
        registered_ = iocp_ctx_->registerFd(loop, fd);
        return registered_;
    }

    void unregisterFd(const EventLoopPtr &loop, SOCKET_FD fd, bool close_fd)
    {
        if (registered_) {
            registered_ = false;
            iocp_ctx_->setCallback(nullptr);
            iocp_ctx_->unregisterFd(loop, fd, close_fd);
            iocp_ctx_.reset();
        }
        else if (close_fd && fd != INVALID_FD) {
            closeFd(fd);
        }
    }

    virtual void ioReady(IocpContext::Op op, size_t io_size) = 0;

    SKBuffer& sendBuffer()
    {
        return iocp_ctx_->sendBuffer();
    }

    SKBuffer& recvBuffer()
    {
        return iocp_ctx_->recvBuffer();
    }

    bool sendPending() const
    {
        return iocp_ctx_->sendPending();
    }

    bool recvPending() const
    {
        return iocp_ctx_->recvPending();
    }

    bool postConnectOperation(SOCKET_FD fd, const sockaddr_storage &ss_addr)
    {
        return iocp_ctx_->postConnectOperation(fd, ss_addr);
    }

    bool postAcceptOperation(SOCKET_FD fd, SOCKET_FD accept_fd)
    {
        return iocp_ctx_->postAcceptOperation(fd, accept_fd);
    }

    int postSendOperation(SOCKET_FD fd)
    {
        return iocp_ctx_->postSendOperation(fd);
    }

    int postRecvOperation(SOCKET_FD fd)
    {
        return iocp_ctx_->postRecvOperation(fd);
    }

    bool hasPendingOperation() const
    {
        return iocp_ctx_->isPending();
    }

protected:
    bool                registered_ = false;
    IocpWrapper::Ptr    iocp_ctx_;
};

KUMA_NS_END

#endif
