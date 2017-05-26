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

#include "kmconf.h"

#include "EventLoopImpl.h"
#include "IocpAcceptor.h"
#include "util/util.h"
#include "util/kmtrace.h"

#include <MSWSock.h>
#include <Ws2tcpip.h>
#include <windows.h>

using namespace kuma;

KUMA_NS_BEGIN
extern LPFN_CONNECTEX connect_ex;
extern LPFN_ACCEPTEX accept_ex;
extern LPFN_CANCELIOEX cancel_io_ex;
KUMA_NS_END

IocpAcceptor::IocpAcceptor(const EventLoopPtr &loop)
: AcceptorBase(loop), accept_ctx_(IocpContext::create())
{
    KM_SetObjKey("IocpAcceptor");
}

IocpAcceptor::~IocpAcceptor()
{

}

void IocpAcceptor::cancel(SOCKET_FD fd)
{
    if (fd != INVALID_FD) {
        if (cancel_io_ex) {
            cancel_io_ex(reinterpret_cast<HANDLE>(fd), nullptr);
        }
        else {
            CancelIo(reinterpret_cast<HANDLE>(fd));
        }
    }
}

void IocpAcceptor::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    if (hasPendingOperation()) {
        cancel(fd);
    }
    accept_ctx_.reset();
    AcceptorBase::unregisterFd(fd, close_fd);
}

KMError IocpAcceptor::listen(const std::string &host, uint16_t port)
{
    auto ret = AcceptorBase::listen(host, port);
    if (ret != KMError::NOERR) {
        return ret;
    }
    postAcceptOperation();
    return KMError::NOERR;
}

void IocpAcceptor::postAcceptOperation()
{
    accept_fd_ = WSASocketW(ss_family_, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (INVALID_FD == accept_fd_) {
        KUMA_ERRXTRACE("postAcceptOperation, socket failed, err=" << getLastError());
        return ;
    }
    DWORD bytes_recv = 0;
    DWORD addr_len = static_cast<DWORD>(sizeof(sockaddr_storage));
    accept_ctx_->prepare(IocpContext::Op::ACCEPT);
    auto ret = accept_ex(fd_, accept_fd_, &ss_addrs_, 0, addr_len, addr_len, &bytes_recv, &accept_ctx_->ol);
    if (!ret && getLastError() != WSA_IO_PENDING) {
        KUMA_ERRXTRACE("postAcceptOperation, AcceptEx, err=" << getLastError());
    }
    else {
        accept_pending_ = true;
    }
}

void IocpAcceptor::onAccept()
{
    accept_pending_ = false;
    auto fd = accept_fd_;
    accept_fd_ = INVALID_FD;
    if (closed_) {
        if (fd != INVALID_FD) {
            closeFd(fd);
        }
        cleanup();
        return;
    }
    if (fd != INVALID_FD) {
        setsockopt(fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&fd_, sizeof(fd_));
        AcceptorBase::onAccept(fd);
    }
    AcceptorBase::onAccept();
    if (!closed_) {
        postAcceptOperation();
    }
}

bool IocpAcceptor::hasPendingOperation() const
{
    return accept_pending_;
}

void IocpAcceptor::ioReady(KMEvent events, void* ol, size_t io_size)
{
    if (accept_ctx_ && ol == &accept_ctx_->ol) {
        onAccept();
    }
}
