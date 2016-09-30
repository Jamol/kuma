/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
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

#include "FlowControl.h"
#include "util/kmtrace.h"

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
//
FlowControl::FlowControl(uint32_t streamId, UpdateCallback cb)
: streamId_(streamId), update_cb_(std::move(cb))
{
    
}

void FlowControl::setLocalWindowStep(uint32_t windowSize)
{
    localWindowStep_ = windowSize;
}

void FlowControl::setMinLocalWindowSize(uint32_t minSize)
{
    minLocalWindowSize_ = minSize;
}

void FlowControl::increaseLocalWindowSize(uint32_t windowSize)
{
    localWindowSize_ += windowSize;
}

void FlowControl::increaseRemoteWindowSize(uint32_t windowSize)
{
    remoteWindowSize_ += windowSize;
}

void FlowControl::initLocalWindowSize(uint32_t windowSize)
{
    localWindowSize_ = windowSize;
}

void FlowControl::initRemoteWindowSize(uint32_t windowSize)
{
    remoteWindowSize_ = windowSize;
}

void FlowControl::notifyBytesSent(size_t bytes)
{
    bytesSent_ += bytes;
    if (bytes < remoteWindowSize_) {
        remoteWindowSize_ -= bytes;
    } else {
        remoteWindowSize_ = 0;
        KUMA_INFOTRACE("FlowControl::notifyBytesSent, window size is 0, streamId="<<streamId_<<", bytesSent="<<bytesSent_);
    }
}

void FlowControl::notifyBytesReceived(size_t bytes)
{
    bytesReceived_ += bytes;
    if (bytes < localWindowSize_) {
        localWindowSize_ -= bytes;
    } else {
        localWindowSize_ = 0;
    }
    if (localWindowSize_ < minLocalWindowSize_) {
        localWindowSize_ += localWindowStep_;
        if (update_cb_) {
            update_cb_(uint32_t(localWindowStep_));
        }
    }
}
