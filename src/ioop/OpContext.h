/* Copyright (c) 2014-2024, Fengping Bao <jamol@live.com>
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

#ifndef __OPCONTEXT_H__
#define __OPCONTEXT_H__

#include "kmdefs.h"
#include "libkev/src/utils/kmtrace.h"
#include "utils/utils.h"
#include "libkev/src/utils/skutils.h"
#include "libkev/include/kevops.h"
#include "EventLoopImpl.h"

#if defined(KUMA_OS_WIN)
# include <MSWSock.h>
# include <Ws2tcpip.h>
# include <windows.h>
#endif

using kev::OpCode;

KUMA_NS_BEGIN

const size_t kUDPRecvPacketSize = 64 * 1024;

extern int to_iovecs(const KMBuffer &buf, iovec* iovs, int sz, iovec** new_iovs);

class OpContext;

struct OpBase
{
    OpCode          oc = OpCode::CANCEL;
    OpContext*      ctx{nullptr};
    bool            pending{false};
    kev::OpData     data;
    OpBase*         prev = nullptr;
    OpBase*         next = nullptr;

    OpBase()
    {
        data.fd = INVALID_FD;
        data.context = this;
        data.handler = [] (SOCKET_FD, int res, void* ctx) {
            auto * _this = static_cast<OpBase*>(ctx);
            _this->onComplete(res);
        };
    }
    virtual ~OpBase()
    {
#if defined(KUMA_OS_WIN)
        if (oc == OpCode::ACCEPT && data.fd != INVALID_FD) {
            kev::SKUtils::close(data.fd);
            data.fd = INVALID_FD;
        }
#endif
    }
    virtual void onComplete(int res);
};


struct ConnAcctOp : public OpBase
{
    sockaddr_storage addr;
    sockaddr_storage addr2;
#if !defined(KUMA_OS_WIN)
    socklen_t addrlen;
#endif

    void prepare(OpCode oc, OpContext* ctx)
    {
        this->oc = oc;
        this->ctx = ctx;
    }
};

struct SendOp : public OpBase
{
    SOCKET_FD       fd{ INVALID_FD };
    KMBuffer        buf;
    iovec           iovs[3];
    iovec*          p_iovs{ nullptr };
    int             n_iovs{ 0 };
#if defined(KUMA_OS_LINUX)
    bool            poll_out{ false };
    size_t          bytes_sent{ 0 };
#endif

    ~SendOp()
    {
        if (p_iovs && p_iovs != iovs) {
            delete[] p_iovs;
        }
    }

    void prepare(OpCode oc, SOCKET_FD fd, OpContext* ctx)
    {
        this->fd = fd;
        this->oc = oc;
        this->ctx = ctx;
        if (p_iovs && p_iovs != iovs) {
            delete[] p_iovs;
            p_iovs = nullptr;
        }
        p_iovs = iovs;
        n_iovs = to_iovecs(buf, iovs, ARRAY_SIZE(iovs), &p_iovs);
#if defined(KUMA_OS_LINUX)
        poll_out = false;
        bytes_sent = 0;
#endif
    }

    void onComplete(int res) override;
};

struct RecvOp : public OpBase
{
    KMBuffer        buf;
    iovec           iovs[1];
    iovec*          p_iovs{ nullptr };
    int             n_iovs{ 0 };

    void prepare(OpCode oc, OpContext* ctx)
    {
        this->oc = oc;
        this->ctx = ctx;
        iovs[0].iov_base = (char*)buf.writePtr();
        iovs[0].iov_len = (unsigned long)buf.space();
        p_iovs = iovs;
        n_iovs = 1;
    }

    void onComplete(int res) override;
};

struct SendMsgOp : public OpBase
{
    KMBuffer        buf;
#if defined(KUMA_OS_WIN)
    WSAMSG          msg;
#else
    msghdr          msg;
#endif
    sockaddr_storage addr;
    iovec           iovs[3];
    iovec*          p_iovs{ nullptr };
    int             n_iovs{ 0 };

    ~SendMsgOp()
    {
        if (p_iovs && p_iovs != iovs) {
            delete[] p_iovs;
        }
    }

    void prepare(OpCode oc, OpContext* ctx, const sockaddr_storage *addr, int flags)
    {
        this->oc = oc;
        this->ctx = ctx;
        if (p_iovs && p_iovs != iovs) {
            delete[] p_iovs;
            p_iovs = nullptr;
        }
        p_iovs = iovs;
        n_iovs = to_iovecs(buf, iovs, ARRAY_SIZE(iovs), &p_iovs);
#if defined(KUMA_OS_WIN)
        msg.lpBuffers = (WSABUF*)p_iovs;
        msg.dwBufferCount = n_iovs;
        msg.Control.buf = nullptr;
        msg.Control.len = 0;
        msg.dwFlags = flags;
        if (addr) {
            this->addr = *addr;
            msg.name = (sockaddr*)&this->addr;
            msg.namelen = (int)kev::km_get_addr_length(this->addr);
        }
#else
        msg.msg_iov = p_iovs;
        msg.msg_iovlen = n_iovs;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = flags;
        if (addr) {
            this->addr = *addr;
            msg.msg_name = &this->addr;
            msg.msg_namelen = kev::km_get_addr_length(this->addr);
        }
#endif
    }

    void onComplete(int res) override;
};

struct RecvMsgOp : public OpBase
{
    KMBuffer        buf;
#if defined(KUMA_OS_WIN)
    WSAMSG          msg;
#else
    msghdr          msg;
#endif
    sockaddr_storage addr;
    iovec           iovs[1];
    iovec*          p_iovs{ nullptr };
    int             n_iovs{ 0 };

    void prepare(OpCode oc, OpContext* ctx, const sockaddr_storage *addr, int flags)
    {
        this->oc = oc;
        this->ctx = ctx;
        iovs[0].iov_base = (char*)buf.writePtr();
        iovs[0].iov_len = (unsigned long)buf.space();
        p_iovs = iovs;
        n_iovs = 1;
#if defined(KUMA_OS_WIN)
        msg.lpBuffers = (WSABUF*)p_iovs;
        msg.dwBufferCount = n_iovs;
        msg.Control.buf = nullptr;
        msg.Control.len = 0;
        msg.dwFlags = flags;
        msg.name = (sockaddr*)&this->addr;
        msg.namelen = sizeof(this->addr);
#else
        msg.msg_iov = p_iovs;
        msg.msg_iovlen = n_iovs;
        msg.msg_control = nullptr;
        msg.msg_controllen = 0;
        msg.msg_flags = flags;
        msg.msg_name = &this->addr;
        msg.msg_namelen = sizeof(this->addr);
#endif
    }

    void onComplete(int res) override;
};

// OpContext holds the structs and buffers used by IOCP it can only be deleted
// after all pending operations are completed, or event loop is stopped
class OpContext : public kev::PendingObject
{
public:
    using AcceptCallback = std::function<void(int, SOCKET_FD, sockaddr_storage&)>;
    using ConnectCallback = std::function<void(int)>;
    using SendCallback = std::function<void(int)>;
    using RecvCallback = std::function<void(int, const KMBuffer&)>;

    bool isPending() const override
    {
        return !!pending_ops_;
    }

    void onLoopExit() override
    {
        // loop exited, there are no more IO events
        loop_.reset();
        resetPending();
    }

    void cancel(const EventLoopPtr &loop, SOCKET_FD fd)
    {
#if defined(KUMA_OS_WIN)
        if (fd != INVALID_FD) {
            postCalcelOp(loop, fd, nullptr);
        }
#else
        auto* op = pending_ops_;
        while(op) {
            postCalcelOp(loop, fd, &op->data);
            op = op->next;
        }
#endif
    }

    bool registerFd(const EventLoopPtr &loop, SOCKET_FD fd)
    {
        if (!loop || fd == INVALID_FD) {
            return false;
        }
        kev::Op op1;
        op1.oc = OpCode::REGISTER;
        op1.buf = nullptr;
        op1.buflen = 0;
        op1.data = nullptr;
        auto ret = submitOp(loop, fd, op1);
        if (ret != KMError::NOERR) {
            return  false;
        }
        return true;
    }

    /**
     * return false if there are pending operations
     */
    bool unregisterFd(const EventLoopPtr &loop, SOCKET_FD fd, bool close_fd)
    {
        if (fd == INVALID_FD) {
            return true;
        }
        if (loop && isPending()) {
            closing_ = true;
            loop->appendPendingObject(this);

            // wait until all pending operations are completed, or loop exit
            if (close_fd) {
                shutdown(fd, 2);
                pending_fd_ = fd;
            }
            cancel(loop, fd);
            return false;
        }
        if (close_fd) {
            kev::SKUtils::close(fd);
        }
        resetPending();
        return true;
    }

    KMError postConnectOp(const EventLoopPtr &loop, SOCKET_FD fd, const sockaddr_storage &ss_addr)
    {
        if (!loop) {
            return KMError::INVALID_STATE;
        }
        auto *op = static_cast<ConnAcctOp*>(getFreeOp(OpCode::CONNECT));
        auto addr_len = static_cast<int>(kev::km_get_addr_length(ss_addr));
        op->prepare(OpCode::CONNECT, this);
        op->addr = ss_addr;
        appendPendingOp(op);

        kev::Op op1;
        op1.oc = op->oc;
        op1.addr = (sockaddr*)&op->addr;
        op1.addrlen = addr_len;
        op1.flags = 0;
        op1.data = &op->data;
        increment();
        auto ret = submitOp(loop, fd, op1);
        if (ret != KMError::NOERR) {
            removePendingOp(op);
            appendFreeOp(op);
            decrement();
        }
        return ret;
    }

    KMError postAcceptOp(const EventLoopPtr &loop, SOCKET_FD fd,
#ifdef KUMA_OS_WIN
                      ADDRESS_FAMILY ss_family
#else
                      sa_family_t ss_family
#endif
    )
    {
        if (!loop) {
            return KMError::INVALID_STATE;
        }
#if defined(KUMA_OS_WIN)
        auto accept_fd = WSASocketW(ss_family, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (INVALID_FD == accept_fd) {
            KM_ERRTRACE("postAcceptOp, socket failed, fd=" << fd
                     << ", err=" << kev::SKUtils::getLastError());
            return KMError::SOCK_ERROR;
        }
#endif
        auto *op = static_cast<ConnAcctOp*>(getFreeOp(OpCode::ACCEPT));
        op->prepare(OpCode::ACCEPT, this);
#if defined(KUMA_OS_WIN)
        op->data.fd = accept_fd;
#endif
        appendPendingOp(op);

        kev::Op op1;
        op1.oc = op->oc;
        op1.addr = (sockaddr*)&op->addr;
#if defined(KUMA_OS_WIN)
        op1.addrlen = sizeof(sockaddr_storage);
#else
        op->addrlen = sizeof(sockaddr_storage);
        op1.addr2 = &op->addrlen;
#endif
        op1.flags = 0;
        op1.data = &op->data;
        increment();
        auto ret = submitOp(loop, fd, op1);
        if (ret != KMError::NOERR) {
            kev::SKUtils::close(op->data.fd);
            op->data.fd = INVALID_FD;
            removePendingOp(op);
            appendFreeOp(op);
            decrement();
        }
        return ret;
    }

    KMError postSendOp(const EventLoopPtr &loop, SOCKET_FD fd, const KMBuffer &buf)
    {
        if (!loop) {
            return KMError::INVALID_STATE;
        }
        auto *op = static_cast<SendOp*>(getFreeOp(OpCode::WRITEV));
        op->buf = buf;
        op->prepare(OpCode::WRITEV, fd, this);
        appendPendingOp(op);

        kev::Op op1;
        op1.oc = op->oc;
        op1.iovs = op->p_iovs;
        op1.count = op->n_iovs;
        op1.flags = 0;
        op1.data = &op->data;
        increment();
        auto ret = submitOp(loop, fd, op1);
        if (ret != KMError::NOERR) {
            KM_ERRTRACE("postSendOp, fd=" << fd << ", ret=" << int(ret));
            removePendingOp(op);
            appendFreeOp(op);
            decrement();
        }
        return ret;
    }

    KMError postRecvOp(const EventLoopPtr &loop, SOCKET_FD fd, const KMBuffer &buf)
    {
        if (!loop) {
            return KMError::INVALID_STATE;
        }
        auto *op = static_cast<RecvOp*>(getFreeOp(OpCode::READV));
        op->buf = buf;
        op->prepare(OpCode::READV, this);
        appendPendingOp(op);

        kev::Op op1;
        op1.oc = op->oc;
        op1.iovs = op->p_iovs;
        op1.count = op->n_iovs;
        op1.flags = 0;
        op1.data = &op->data;
        increment();
        auto ret = submitOp(loop, fd, op1);
        if (ret != KMError::NOERR) {
            KM_ERRTRACE("postRecvOp, fd=" << fd << ", ret=" << int(ret));
            removePendingOp(op);
            appendFreeOp(op);
            decrement();
        }
        return ret;
    }

    KMError postSendMsgOp(const EventLoopPtr &loop, SOCKET_FD fd, const KMBuffer &buf, const sockaddr_storage &addr)
    {
        if (!loop) {
            return KMError::INVALID_STATE;
        }
        auto *op = static_cast<SendMsgOp*>(getFreeOp(OpCode::SENDMSG));
        op->buf = buf;
        op->prepare(OpCode::SENDMSG, this, &addr, 0);
        appendPendingOp(op);

        kev::Op op1;
        op1.oc = op->oc;
        op1.buf = &op->msg;
        op1.buflen = 0;
        op1.flags = 0;
        op1.data = &op->data;
        increment();
        auto ret = submitOp(loop, fd, op1);
        if (ret != KMError::NOERR) {
            removePendingOp(op);
            appendFreeOp(op);
            decrement();
        }
        return ret;
    }

    KMError postRecvMsgOp(const EventLoopPtr &loop, SOCKET_FD fd, const KMBuffer &buf)
    {
        if (!loop) {
            return KMError::INVALID_STATE;
        }
        auto *op = static_cast<RecvMsgOp*>(getFreeOp(OpCode::RECVMSG));
        op->buf = buf;
        op->prepare(OpCode::RECVMSG, this, nullptr, 0);
        appendPendingOp(op);

        kev::Op op1;
        op1.oc = op->oc;
        op1.buf = &op->msg;
        op1.buflen = 0;
        op1.flags = 0;
        op1.data = &op->data;
        increment();
        auto ret = submitOp(loop, fd, op1);
        if (ret != KMError::NOERR) {
            removePendingOp(op);
            appendFreeOp(op);
            decrement();
        }
        return ret;
    }

    KMError postCalcelOp(const EventLoopPtr &loop, SOCKET_FD fd, void* data)
    {
        kev::Op op1;
        op1.oc = OpCode::CANCEL;
        op1.buf = data;
        op1.flags = 0;
        op1.data = nullptr;
        auto ret = submitOp(loop, fd, op1);
        return ret;
    }

    KMError submitOp(const EventLoopPtr &loop, SOCKET_FD fd, kev::Op op1)
    {
        return toKMError(loop->submitOp(fd, op1));
    }

    KMError submitOp(SOCKET_FD fd, kev::Op op1)
    {
        auto loop = loop_.lock();
        if (loop) {
            return toKMError(loop->submitOp(fd, op1));
        }
        return KMError::INVALID_STATE;
    }

    void onOpComplete(OpBase* base, int res)
    {
        //KM_INFOTRACE("onOpComplete, oc=" << int(base->oc) << ", res=" << res);
        if (!base->pending) {
            return;
        }
        removePendingOp(base);
        if (decrement() || closing_) {
            delete base;
            return;
        }
        switch (base->oc)
        {
        case OpCode::CONNECT: {
            delete base; // only once for connect op
            if (on_connect_) on_connect_(res);
            break;
        }
        case OpCode::ACCEPT: {
            auto *op = (ConnAcctOp*)base;
#if defined(KUMA_OS_WIN)
            SOCKET_FD accept_fd = op->data.fd;
            op->data.fd = INVALID_FD;
#else
            SOCKET_FD accept_fd = res;
#endif
            appendFreeOp(base);
            if (on_accept_) on_accept_(res, accept_fd, op->addr);
            break;
        }
        case OpCode::WRITEV: {
            appendFreeOp(base);
            if (on_send_) on_send_(res);
            break;
        }
        case OpCode::READV: {
            auto *op = (RecvOp*)base;
            auto buf = std::move(op->buf);
            appendFreeOp(base);
            if (on_recv_) on_recv_(res, std::move(buf));
            break;
        }
        case OpCode::SENDMSG: {
            appendFreeOp(base);
            if (on_send_) on_send_(res);
            break;
        }
        case OpCode::RECVMSG: {
            auto *op = (RecvMsgOp*)base;
            addr_ = op->addr;
            auto buf = std::move(op->buf);
            appendFreeOp(base);
            if (on_recv_) on_recv_(res, std::move(buf));
            break;
        }
        
        default:
            appendFreeOp(base);
            break;
        }
    }

    void setAcceptCallback(AcceptCallback cb)
    {
        on_accept_ = std::move(cb);
    }
    void setConnectCallback(ConnectCallback cb)
    {
        on_connect_ = std::move(cb);
    }
    void setSendCallback(SendCallback cb)
    {
        on_send_ = std::move(cb);
    }
    void setRecvCallback(RecvCallback cb)
    {
        on_recv_ = std::move(cb);
    }

    sockaddr_storage getSockAddr() const
    {
        return addr_;
    }

public:
    struct Deleter
    {
        void operator()(OpContext* ptr) {
            if (ptr) {
                ptr->closing_ = true;
                ptr->decrement();
            }
        }
    };
    using Ptr = std::unique_ptr<OpContext, Deleter>;
    static Ptr create(const EventLoopPtr &loop)
    {
        auto *p = new OpContext(loop);
        p->increment();
        return Ptr(p);
    }

protected:
    OpContext(const EventLoopPtr &loop) : loop_(loop) {}
    virtual ~OpContext()
    {
        if (pending_fd_ != INVALID_FD) {
            kev::SKUtils::close(pending_fd_);
            pending_fd_ = INVALID_FD;
        }
        assert(pending_ops_ == nullptr);

#define CLEAR_CTX_OP_LIST(op_head) \
        while (op_head) {          \
            auto *op = op_head;    \
            op_head = op->next;    \
            delete op;             \
        }

        CLEAR_CTX_OP_LIST(free_ca_ops_);
        CLEAR_CTX_OP_LIST(free_wv_ops_);
        CLEAR_CTX_OP_LIST(free_rv_ops_);
        CLEAR_CTX_OP_LIST(free_sm_ops_);
        CLEAR_CTX_OP_LIST(free_rm_ops_);

#undef CLEAR_CTX_OP_LIST
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

    void appendOp(OpBase* op, OpBase** op_head)
    {
        if (*op_head == nullptr) {
            *op_head = op;
        } else {
            op->next = *op_head;
            (*op_head)->prev = op;
            *op_head = op;
        }
    }

    void removeOp(OpBase* op, OpBase** op_head)
    {
        if (op->next) {
            op->next->prev = op->prev;
        }
        if (op->prev) {
            op->prev->next = op->next;
        }
        if (op == *op_head) {
            *op_head = op->next;
        }
        op->next = op->prev = nullptr;
    }

    void appendPendingOp(OpBase* op)
    {
        op->pending = true;
        appendOp(op, &pending_ops_);
    }

    void removePendingOp(OpBase* op)
    {
        op->pending = false;
        removeOp(op, &pending_ops_);
    }

    OpBase** getFreeOpList(OpCode oc)
    {
        switch (oc)
        {
        case OpCode::CONNECT:
        case OpCode::ACCEPT:
            return &free_ca_ops_;
        case OpCode::WRITEV:
            return &free_wv_ops_;
        case OpCode::READV:
            return &free_rv_ops_;
        case OpCode::SENDMSG:
            return &free_sm_ops_;
        case OpCode::RECVMSG:
            return &free_rm_ops_;
        default:
            return nullptr;
        }
    }

    void appendFreeOp(OpBase* op)
    {
        OpBase** free_ops = getFreeOpList(op->oc);
        if (!free_ops) {
            return;
        }
        appendOp(op, free_ops);
    }

    void removeFreeOp(OpBase* op)
    {
        OpBase** free_ops = getFreeOpList(op->oc);
        if (!free_ops) {
            return;
        }
        removeOp(op, free_ops);
    }

    OpBase* getFreeOp(OpCode oc)
    {
        OpBase** free_ops = getFreeOpList(oc);
        if (!free_ops) {
            return nullptr;
        }
        if (*free_ops != nullptr) {
            auto *op = *free_ops;
            removeOp(op, free_ops);
            return op;
        }
        switch (oc)
        {
        case OpCode::CONNECT:
        case OpCode::ACCEPT:
            return new ConnAcctOp();
        case OpCode::WRITEV:
            return new SendOp();
        case OpCode::READV:
            return new RecvOp();
        case OpCode::SENDMSG:
            return new SendMsgOp();
        case OpCode::RECVMSG:
            return new RecvMsgOp();
        default:
            return nullptr;
        }
    }

    void resetPending()
    {
        while (pending_ops_) {
            auto* op = pending_ops_;
            removePendingOp(op);
            if (op->oc != OpCode::CANCEL) {
                appendFreeOp(op);
                if (decrement()) return;
            } else {
                delete op;
            }
        }
    }

    void onDestroy()
    {
        auto loop = loop_.lock();
        if (loop) {
            loop->removePendingObject(this);
        }
        if (pending_fd_ != INVALID_FD) {
            kev::SKUtils::close(pending_fd_);
            pending_fd_ = INVALID_FD;
        }
        delete this;
    }

protected:
    AcceptCallback      on_accept_;
    ConnectCallback     on_connect_;
    SendCallback        on_send_;
    RecvCallback        on_recv_;

    OpBase*             pending_ops_{ nullptr };
    OpBase*             free_ca_ops_{ nullptr };
    OpBase*             free_wv_ops_{ nullptr };
    OpBase*             free_rv_ops_{ nullptr };
    OpBase*             free_sm_ops_{ nullptr };
    OpBase*             free_rm_ops_{ nullptr };
    sockaddr_storage    addr_;

    std::atomic_long    refcount_{ 0 };

    EventLoopWeakPtr    loop_;
    bool                closing_ = false;
    SOCKET_FD           pending_fd_ = INVALID_FD;
};

KUMA_NS_END

#endif // __OPCONTEXT_H__
