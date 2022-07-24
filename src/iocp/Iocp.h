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

#ifndef __IOCP_H__
#define __IOCP_H__

#include "kmdefs.h"
#include "libkev/src/utils/utils.h"
#include "libkev/src/utils/kmtrace.h"
#include "utils/skbuffer.h"
#include "libkev/src/utils/skutils.h"
#include "EventLoopImpl.h"

#include <MSWSock.h>
#include <Ws2tcpip.h>
#include <windows.h>

namespace kev {
    extern LPFN_CONNECTEX connect_ex;
    extern LPFN_ACCEPTEX accept_ex;
    extern LPFN_CANCELIOEX cancel_io_ex;
}

KUMA_NS_BEGIN

const size_t TCPRecvPacketSize = 4 * 1024;
const size_t UDPRecvPacketSize = 64 * 1024;

struct IocpContext
{
    enum class Op
    {
        NONE,
        CONNECT,
        ACCEPT,
        SEND,
        RECV
    };

    OVERLAPPED      ol;
    Op              op = Op::NONE;
    SKBuffer        buf;
    WSABUF          wbuf{ 0, nullptr };

    bool bufferEmpty() const
    {
        return buf.empty();
    }

    void prepare(Op op)
    {
        memset(&ol, 0, sizeof(ol));
        this->op = op;
        if (op == Op::SEND) {
            wbuf.buf = (char*)buf.ptr();
            wbuf.len = static_cast<ULONG>(buf.size());
        }
        else if (op == Op::RECV) {
            wbuf.buf = (char*)buf.wr_ptr();
            wbuf.len = static_cast<ULONG>(buf.space());
        }
    }
};
using IocpContextPtr = std::unique_ptr < IocpContext >;

// IocpWrapper holds the structs and buffers used by IOCP it can only be deleted
// after all pending operations are completed, or event loop is stopped
class IocpWrapper : public kev::PendingObject
{
public:
    using IocpCallback = std::function<void(IocpContext::Op op, size_t io_size)>;

    bool isPending() const override
    {
        return send_pending_ || recv_pending_;
    }

    void onLoopExit() override
    {
        // loop exited, there are no more IO events
        loop_.reset();
        resetPending();
    }

    void cancel(SOCKET_FD fd)
    {
        if (fd != INVALID_FD) {
            if (kev::cancel_io_ex) {
                kev::cancel_io_ex(reinterpret_cast<HANDLE>(fd), nullptr);
            }
            else {
                CancelIo(reinterpret_cast<HANDLE>(fd));
            }
        }
    }

    bool registerFd(const EventLoopPtr &loop, SOCKET_FD fd)
    {
        if (!loop || fd == INVALID_FD) {
            return false;
        }
        if (loop->registerFd(fd, kEventNetwork, [this](KMEvent ev, void* ol, size_t io_size) {
            ioReady(ev, ol, io_size);
        }) == kev::Result::OK)
        {
            return true;
        }
        return false;
    }

    /**
     * return false if there are pending operations
     */
    bool unregisterFd(const EventLoopPtr &loop, SOCKET_FD fd, bool close_fd)
    {
        if (fd == INVALID_FD) {
            return true;
        }
        if (loop) {
            if (isPending()) {
                // wait until all pending operations are completed, or loop exit
                shutdown(fd, 2);

                closing_ = true;
                pending_fd_ = fd;
                loop_ = loop;
                loop->appendPendingObject(this);

                cancel(fd);
                return false;
            }
            loop->unregisterFd(fd, close_fd);
        }
        else if (close_fd) {
            kev::SKUtils::close(fd);
            resetPending();
        }
        return true;
    }

    bool postConnectOperation(SOCKET_FD fd, const sockaddr_storage &ss_addr)
    {
        if (!recv_ctx_) {
            recv_ctx_.reset(new IocpContext);
        }
        auto addr_len = static_cast<int>(kev::km_get_addr_length(ss_addr));
        recv_ctx_->prepare(IocpContext::Op::CONNECT);
        auto ret = kev::connect_ex(fd, (LPSOCKADDR)&ss_addr, addr_len, NULL, 0, NULL, &recv_ctx_->ol);
        if (!ret && kev::SKUtils::getLastError() != WSA_IO_PENDING) {
            KM_ERRTRACE("postConnectOperation, error, fd=" << fd << ", err=" << kev::SKUtils::getLastError());
            return false;
        }
        else {
            recv_pending_ = true;
            increment();
            return true;
        }
    }

    bool postAcceptOperation(SOCKET_FD fd, SOCKET_FD accept_fd)
    {
        if (!recv_ctx_) {
            recv_ctx_.reset(new IocpContext);
        }
        DWORD bytes_recv = 0;
        DWORD addr_len = static_cast<DWORD>(sizeof(sockaddr_storage) + 16);
        recv_ctx_->buf.expand(2 * addr_len);
        recv_ctx_->prepare(IocpContext::Op::ACCEPT);
        auto ret = kev::accept_ex(fd, accept_fd, recv_ctx_->buf.wr_ptr(), 0, addr_len, addr_len, &bytes_recv, &recv_ctx_->ol);
        if (!ret && kev::SKUtils::getLastError() != WSA_IO_PENDING) {
            KM_ERRTRACE("postAcceptOperation, fd=" << fd << ", err=" << kev::SKUtils::getLastError());
            return false;
        }
        else {
            recv_pending_ = true;
            increment();
            return true;
        }
    }

    virtual int postSendOperation(SOCKET_FD fd)
    {
        if (!send_ctx_) {
            send_ctx_.reset(new IocpContext);
        }
        if (send_ctx_->bufferEmpty() || send_pending_) {
            return 0;
        }
        DWORD bytes_sent = 0;
        send_ctx_->prepare(IocpContext::Op::SEND);
        auto ret = WSASend(fd, &send_ctx_->wbuf, 1, &bytes_sent, 0, &send_ctx_->ol, NULL);
        if (ret == SOCKET_ERROR) {
            if (WSA_IO_PENDING == WSAGetLastError()) {
                send_pending_ = true;
                increment();
                return 0;
            }
            return -1;
        }
        else if (ret == 0) {
            // operation completed, continue to wait for the completion notification
            // or set FILE_SKIP_COMPLETION_PORT_ON_SUCCESS
            // SetFileCompletionNotificationModes
            send_pending_ = true;
            increment();
        }
        return ret;
    }

    virtual int postRecvOperation(SOCKET_FD fd)
    {
        if (!recv_ctx_) {
            recv_ctx_.reset(new IocpContext);
        }
        if (recv_pending_) {
            return 0;
        }
        if (!recv_ctx_->bufferEmpty()) {
            KM_WARNTRACE("postRecvOperation, fd=" << fd << ", buf=" << recv_ctx_->buf.size());
        }
        DWORD flags = 0, bytes_recv = 0;
        recv_ctx_->buf.expand(TCPRecvPacketSize);
        recv_ctx_->prepare(IocpContext::Op::RECV);
        auto ret = WSARecv(fd, &recv_ctx_->wbuf, 1, &bytes_recv, &flags, &recv_ctx_->ol, NULL);
        if (ret == SOCKET_ERROR) {
            if (WSA_IO_PENDING == WSAGetLastError()) {
                recv_pending_ = true;
                increment();
                return 0;
            }
            return -1;
        }
        else if (ret == 0) {
            // operation completed, continue to wait for the completion notification
            // or set FILE_SKIP_COMPLETION_PORT_ON_SUCCESS
            // SetFileCompletionNotificationModes
            recv_pending_ = true;
            increment();
        }
        return ret;
    }

    void ioReady(KMEvent events, void* ol, size_t io_size)
    {
        if (recv_ctx_ && ol == &recv_ctx_->ol) {
            recv_pending_ = false;
            if (decrement() || closing_) {
                return;
            }
            if (recv_ctx_->op == IocpContext::Op::RECV) {
                if (io_size > recvBuffer().space()) {
                    KM_ERRTRACE("ioReady, recv error, io_size=" << io_size << ", space=" << recvBuffer().space());
                }
                recvBuffer().bytes_written(io_size);
            }
            if (callback_) callback_(recv_ctx_->op, io_size);
        }
        else if (send_ctx_ && ol == &send_ctx_->ol) {
            send_pending_ = false;
            if (decrement() || closing_) {
                return;
            }
            if (io_size != sendBuffer().size()) {
                KM_ERRTRACE("ioReady, send error, io_size=" << io_size << ", buffer=" << sendBuffer().size());
            }
            sendBuffer().bytes_read(io_size);
            if (callback_) callback_(send_ctx_->op, io_size);
        }
        else {
            KM_WARNTRACE("ioReady, invalid overlapped");
        }
    }

    SKBuffer& sendBuffer()
    {
        if (!send_ctx_) {
            send_ctx_.reset(new IocpContext);
        }
        return send_ctx_->buf;
    }

    SKBuffer& recvBuffer()
    {
        if (!recv_ctx_) {
            recv_ctx_.reset(new IocpContext);
        }
        return recv_ctx_->buf;
    }

    bool sendPending() const
    {
        return send_pending_;
    }

    bool recvPending() const
    {
        return recv_pending_;
    }

    void setCallback(IocpCallback cb)
    {
        callback_ = std::move(cb);
    }

    void increment()
    {
        ++refcount_;
    }

    bool decrement()
    {
        if (--refcount_ == 0) {
            onDestroy();
            return true;
        }
        return false;
    }

public:
    struct Deleter
    {
        void operator()(IocpWrapper* ptr) {
            if (ptr) {
                ptr->decrement();
            }
        }
    };
    using Ptr = std::unique_ptr<IocpWrapper, Deleter>;
    static Ptr create()
    {
        auto *p = new IocpWrapper();
        p->increment();
        return Ptr(p);
    }

protected:
    IocpWrapper() = default;
    virtual ~IocpWrapper() {
        if (pending_fd_ != INVALID_FD) {
            kev::SKUtils::close(pending_fd_);
            pending_fd_ = INVALID_FD;
        }
    }

    void resetPending()
    {
        if (send_pending_) {
            send_pending_ = false;
            if (decrement()) return;
        }
        if (recv_pending_) {
            recv_pending_ = false;
            if (decrement()) return;
        }
    }

    void onDestroy()
    {
        auto loop = loop_.lock();
        if (loop) {
            if (pending_fd_ != INVALID_FD) {
                loop->unregisterFd(pending_fd_, true);
                pending_fd_ = INVALID_FD;
            }
            loop->removePendingObject(this);
        }
        else {
            if (pending_fd_ != INVALID_FD) {
                kev::SKUtils::close(pending_fd_);
                pending_fd_ = INVALID_FD;
            }
        }
        delete this;
    }

protected:
    bool                send_pending_ = false;
    bool                recv_pending_ = false;
    IocpContextPtr      send_ctx_;
    IocpContextPtr      recv_ctx_;
    IocpCallback        callback_;

    std::atomic_long    refcount_{ 0 };

    EventLoopWeakPtr    loop_;
    bool                closing_ = false;
    SOCKET_FD           pending_fd_ = INVALID_FD;
};

class IocpUdpWrapper : public IocpWrapper
{
public:
    int postRecvOperation(SOCKET_FD fd) override
    {
        if (!recv_ctx_) {
            recv_ctx_.reset(new IocpContext);
        }
        if (recv_pending_) {
            return 0;
        }
        if (!recv_ctx_->bufferEmpty()) {
            KM_WARNTRACE("postRecvOperation, fd=" << fd << ", buf=" << recv_ctx_->buf.size());
        }

        DWORD flags = 0;
        addr_len_ = sizeof(ss_addr_);
        recv_ctx_->buf.expand(UDPRecvPacketSize);
        recv_ctx_->prepare(IocpContext::Op::RECV);
        auto ret = WSARecvFrom(fd, &recv_ctx_->wbuf, 1, nullptr, &flags, (sockaddr*)&ss_addr_, &addr_len_, &recv_ctx_->ol, NULL);
        if (ret == SOCKET_ERROR) {
            if (WSA_IO_PENDING == WSAGetLastError()) {
                recv_pending_ = true;
                increment();
                return 0;
            }
            return -1;
        }
        else if (ret == 0) {
            // operation completed, continue to wait for the completion notification
            // or set FILE_SKIP_COMPLETION_PORT_ON_SUCCESS
            // SetFileCompletionNotificationModes
            recv_pending_ = true;
            increment();
        }
        return ret;
    }

    static Ptr create()
    {
        auto *p = new IocpUdpWrapper();
        p->increment();
        return Ptr(p);
    }

protected:
    IocpUdpWrapper() = default;
    ~IocpUdpWrapper() = default;

public:
    sockaddr_storage    ss_addr_;
    int                 addr_len_ = 0;
};
KUMA_NS_END

#endif
