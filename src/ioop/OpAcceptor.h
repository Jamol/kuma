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

#ifndef __OpAcceptor_H__
#define __OpAcceptor_H__

#include "kmdefs.h"
#include "kmapi.h"
#include "AcceptorBase.h"
#include "OpContext.h"

KUMA_NS_BEGIN

class OpAcceptor : public AcceptorBase
{
public:
    OpAcceptor(const EventLoopPtr &loop);
    ~OpAcceptor();
    KMError listen(const std::string &host, uint16_t port) override;
    
protected:
    bool registerFd(SOCKET_FD fd) override;
    void unregisterFd(SOCKET_FD fd, bool close_fd) override;

protected:
    OpContext::Ptr op_ctx_;
};

KUMA_NS_END

#endif
