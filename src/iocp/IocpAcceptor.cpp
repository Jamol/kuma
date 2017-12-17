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

IocpAcceptor::IocpAcceptor(const EventLoopPtr &loop)
: AcceptorBase(loop), IocpBase(IocpWrapper::create())
{
    KM_SetObjKey("IocpAcceptor");
}

IocpAcceptor::~IocpAcceptor()
{
    if (accept_fd_ != INVALID_FD) {
        closeFd(accept_fd_);
        accept_fd_ = INVALID_FD;
    }
}

bool IocpAcceptor::registerFd(SOCKET_FD fd)
{
    return IocpBase::registerFd(loop_.lock(), fd);
}

void IocpAcceptor::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    IocpBase::unregisterFd(loop_.lock(), fd, close_fd);
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

bool IocpAcceptor::postAcceptOperation()
{
    accept_fd_ = WSASocketW(ss_family_, SOCK_STREAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (INVALID_FD == accept_fd_) {
        KUMA_ERRXTRACE("postAcceptOperation, socket failed, err=" << getLastError());
        return false;
    }
    return IocpBase::postAcceptOperation(fd_, accept_fd_);
}

void IocpAcceptor::onAccept()
{
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

void IocpAcceptor::ioReady(IocpContext::Op op, size_t io_size)
{
    onAccept();
}
