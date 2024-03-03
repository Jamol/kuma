/* Copyright (c) 2014-2024, Fengping Bao <jamol@live.com>
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
#include "OpAcceptor.h"
#include "libkev/src/utils/utils.h"
#include "libkev/src/utils/kmtrace.h"
#include "libkev/src/utils/skutils.h"

using namespace kuma;

OpAcceptor::OpAcceptor(const EventLoopPtr &loop)
: AcceptorBase(loop), op_ctx_(OpContext::create())
{
    KM_SetObjKey("OpAcceptor");
    op_ctx_->setAcceptCallback([this] (int res, SOCKET_FD fd, sockaddr_storage &peer_addr) {
        if (closed_) {
            if (fd != INVALID_FD) {
                kev::SKUtils::close(fd);
            }
            return;
        }
        if (fd != INVALID_FD) {
#if defined(KUMA_OS_WIN)
            setsockopt(fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&fd_, sizeof(fd_));
#endif
            op_ctx_->postAcceptOp(loop_.lock(), fd_, ss_family_);
            AcceptorBase::onAccept(fd, peer_addr);
        }
    });
}

OpAcceptor::~OpAcceptor()
{
    op_ctx_.reset();
}

bool OpAcceptor::registerFd(SOCKET_FD fd)
{
    return op_ctx_->registerFd(loop_.lock(), fd);
}

void OpAcceptor::unregisterFd(SOCKET_FD fd, bool close_fd)
{
    op_ctx_->unregisterFd(loop_.lock(), fd, close_fd);
}

KMError OpAcceptor::listen(const std::string &host, uint16_t port)
{
    auto ret = AcceptorBase::listen(host, port);
    if (ret != KMError::NOERR) {
        return ret;
    }
    // post 3 accept ops
    op_ctx_->postAcceptOp(loop_.lock(), fd_, ss_family_);
    op_ctx_->postAcceptOp(loop_.lock(), fd_, ss_family_);
    op_ctx_->postAcceptOp(loop_.lock(), fd_, ss_family_);
    return KMError::NOERR;
}
