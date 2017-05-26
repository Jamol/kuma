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
#include "util/kmtrace.h"

#include <Ws2tcpip.h>
#include <windows.h>
#include <atomic>

KUMA_NS_BEGIN

struct IocpContext;
using IocpContextPtr = std::unique_ptr < IocpContext, void(*)(IocpContext*) > ;

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

    int incReference()
    {
        return ++ref;
    }

    int decReference()
    {
        int tmp = --ref;
        if (0 == tmp) {
            delete this;
        }
        return tmp;
    }

    void prepare(Op op)
    {
        memset(&ol, 0, sizeof(ol));
        this->op = op;
        incReference(); // decreased after IO completion
    }

    static void destroy(IocpContext *ctx)
    {
        if (ctx) {
            ctx->decReference();
        }
    }

    static IocpContextPtr create()
    {
        return IocpContextPtr(new IocpContext, IocpContext::destroy);
    }

    IocpContext() : ref(1) {}

private:
    ~IocpContext() {}

    std::atomic_int ref = 0;
};

KUMA_NS_END

#endif
