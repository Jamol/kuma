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
#include "libkev/src/utils/kmtrace.h"

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
//
FlowControl::FlowControl(uint32_t stream_id, UpdateCallback cb)
: stream_id_(stream_id), update_cb_(std::move(cb))
{
    
}

void FlowControl::setLocalWindowStep(uint32_t window_size)
{
    local_window_step_ = window_size;
    if (min_local_window_size_ > local_window_step_/2) {
        min_local_window_size_ = local_window_step_/2;
    }
}

void FlowControl::setMinLocalWindowSize(uint32_t min_window_size)
{
    min_local_window_size_ = min_window_size;
    if (min_local_window_size_ > local_window_step_/2) {
        min_local_window_size_ = local_window_step_/2;
    }
}

void FlowControl::updateRemoteWindowSize(long delta)
{
    remote_window_size_ += delta;
}

void FlowControl::initLocalWindowSize(uint32_t window_size)
{
    local_window_size_ = window_size;
}

void FlowControl::initRemoteWindowSize(uint32_t window_size)
{
    remote_window_size_ = window_size;
}

uint32_t FlowControl::localWindowSize()
{
    return local_window_size_>0 ? uint32_t(local_window_size_) : 0;
}

uint32_t FlowControl::remoteWindowSize()
{
    return remote_window_size_>0 ? uint32_t(remote_window_size_) : 0;
}

void FlowControl::bytesSent(size_t bytes)
{
    bytes_sent_ += bytes;
    remote_window_size_ -= (long)bytes;
    if (remote_window_size_ <= 0 && bytes + remote_window_size_ > 0) {
        //KM_INFOTRACE("FlowControl::bytesSent, streamId="<<streamId_<<", bytesSent="<<bytesSent_<<", window="<<remoteWindowSize_);
    }
}

void FlowControl::bytesReceived(size_t bytes)
{
    bytes_received_ += bytes;
    local_window_size_ -= (long)bytes;
    if (local_window_size_ < long(min_local_window_size_)) {
        auto delta = local_window_step_ - local_window_size_;
        local_window_size_ += (long)delta;
        if (update_cb_) {
            update_cb_(uint32_t(delta));
        }
    }
}
