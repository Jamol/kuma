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

#include "kmconf.h"

#include <stdarg.h>
#include <errno.h>

#include <memory>

#include "EventLoopImpl.h"
#include "TcpListenerImpl.h"
#include "AcceptorBase.h"
#include "libkev/src/utils/utils.h"
#include "libkev/src/utils/kmtrace.h"
#ifdef KUMA_OS_WIN
//# include "iocp/IocpAcceptor.h"
# include "ioop/OpAcceptor.h"
#endif
#if defined(KUMA_OS_LINUX)
# include "ioop/OpAcceptor.h"
#endif

using namespace kuma;

TcpListener::Impl::Impl(const EventLoopPtr &loop)
{
#ifdef KUMA_OS_WIN
    if (loop->getPollType() == PollType::IOCP) {
        //acceptor_.reset(new IocpAcceptor(loop));
        acceptor_ = std::make_unique<OpAcceptor>(loop);
    }
    else
#elif defined(KUMA_OS_LINUX)
    if (loop->getPollType() == PollType::IORING) {
        acceptor_ = std::make_unique<OpAcceptor>(loop);
    }
    else
#endif
    {
        acceptor_ = std::make_unique<AcceptorBase>(loop);
    }
}

TcpListener::Impl::~Impl()
{
    if (acceptor_) {
        acceptor_->close();
    }
}

void TcpListener::Impl::setAcceptCallback(AcceptCallback cb)
{
    acceptor_->setAcceptCallback(std::move(cb));
}

void TcpListener::Impl::setErrorCallback(ErrorCallback cb)
{
    acceptor_->setErrorCallback(std::move(cb));
}

KMError TcpListener::Impl::startListen(const std::string &host, uint16_t port)
{
    return acceptor_->listen(host, port);
}

KMError TcpListener::Impl::stopListen(const std::string &host, uint16_t port)
{
    return close();
}

KMError TcpListener::Impl::close()
{
    return acceptor_->close();
}
