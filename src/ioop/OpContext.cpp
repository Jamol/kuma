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

#include "OpContext.h"

#if defined(KUMA_OS_LINUX)
# include <sys/poll.h>
#endif

KUMA_NS_USING

void OpBase::onComplete(int res)
{
    if (ctx) {
        ctx->onOpComplete(this, res);
    }
}

void SendOp::onComplete(int res)
{
#if defined(KUMA_OS_LINUX)
    if (poll_out) {
        poll_out = false;
        if (res & (POLLERR | POLLHUP | POLLNVAL)) {
            KM_ERRTRACE("send op poll error, fd=" << fd << ", res=" << res);
            buf.reset();
            OpBase::onComplete(-1);
            return;
        }

        // prepare
        if (p_iovs && p_iovs != iovs) {
            delete[] p_iovs;
            p_iovs = nullptr;
        }
        p_iovs = iovs;
        n_iovs = to_iovecs(buf, iovs, ARRAY_SIZE(iovs), &p_iovs);

        kev::Op op1;
        op1.oc = this->oc;
        op1.iovs = p_iovs;
        op1.count = n_iovs;
        op1.flags = 0;
        op1.data = &data;
        auto ret = ctx->submitOp(fd, op1);
        if (ret != KMError::NOERR) {
            KM_ERRTRACE("failed to submit send op, fd=" << fd);
            buf.reset();
            OpBase::onComplete(-1);
        }
        return;
    }
    if (res == EAGAIN || res == EWOULDBLOCK || (res > 0 && res < (int)buf.chainLength())) {
        //KM_WARNTRACE("send op error, res=" << res << ", buffer=" << buf.chainLength());
        if (res > 0 && res < (int)buf.chainLength()) {
            buf.bytesRead(res);
            bytes_sent += res;
        }
        kev::Op op1;
        op1.oc = OpCode::POLL_ADD;
        op1.buflen = 0; // one shot
        op1.flags = POLLOUT | POLLERR | POLLHUP | POLLNVAL;
        op1.data = &data;
        poll_out = true;
        auto ret = ctx->submitOp(fd, op1);
        if (ret != KMError::NOERR) {
            KM_ERRTRACE("failed to submit poll add op, fd=" << fd);
            buf.reset();
            OpBase::onComplete(-1);
        }
        return;
    }
    if (res > 0) {
        res += (int)bytes_sent;
    }
#endif
    buf.reset();
    OpBase::onComplete(res);
}

void RecvOp::onComplete(int res)
{
    if (res > (int)buf.space()) {
        KM_ERRTRACE("recv op error, res=" << res << ", space=" << buf.space());
        res = (int)buf.space();
    }
    buf.bytesWritten(res);
    OpBase::onComplete(res);
}

void SendMsgOp::onComplete(int res)
{
    if (res != buf.chainLength()) {
        KM_ERRTRACE("sendmsg op error, res=" << res << ", buffer=" << buf.chainLength());
    }
    buf.reset();
    OpBase::onComplete(res);
}

void RecvMsgOp::onComplete(int res)
{
    if (res > (int)buf.space()) {
        KM_ERRTRACE("recvmsg op error, res=" << res << ", space=" << buf.space());
        res = (int)buf.space();
    }
    buf.bytesWritten(res);
    OpBase::onComplete(res);
}
