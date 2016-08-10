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

#include "H2Stream.h"
#include "H2ConnectionImpl.h"
#include "util/kmtrace.h"

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
H2Stream::H2Stream(uint32_t streamId)
: streamId_(streamId)
{
    KM_SetObjKey("H2Stream_"<<streamId);
}

int H2Stream::sendHeaders(const H2ConnectionPtr &conn, const HeaderVector &headers, size_t headersSize, bool endStream)
{
    HeadersFrame frame;
    frame.setStreamId(getStreamId());
    frame.addFlags(H2_FRAME_FLAG_END_HEADERS);
    if (endStream) {
        frame.addFlags(H2_FRAME_FLAG_END_STREAM);
    }
    frame.setHeaders(std::move(headers), headersSize);
    int ret = conn->sendH2Frame(&frame);
    if (getState() == State::IDLE) {
        setState(State::OPEN);
    } else if (getState() == State::RESERVED_L) {
        setState(State::HALF_CLOSED_R);
    }
    
    if (endStream) {
        endStreamSent();
    }
    return ret;
}

int H2Stream::sendData(const H2ConnectionPtr &conn, const uint8_t *data, size_t len, bool endStream)
{
    if (remoteWindowSize_ < len) {
        return 0;
    }
    DataFrame frame;
    frame.setStreamId(getStreamId());
    if (endStream) {
        frame.addFlags(H2_FRAME_FLAG_END_STREAM);
    }
    frame.setData(data, len);
    int ret = conn->sendH2Frame(&frame);
    if (ret > 0) {
        remoteWindowSize_ -= ret;
        if (endStream) {
            endStreamSent();
        }
        return ret;
    }
    
    return 0;
}

void H2Stream::close(const H2ConnectionPtr &conn)
{
    if (conn) {
        conn->removeStream(getStreamId());
    }
}

void H2Stream::endStreamSent()
{
    if (getState() == State::HALF_CLOSED_R) {
        setState(State::CLOSED);
    } else {
        setState(State::HALF_CLOSED_L);
    }
}

void H2Stream::endStreamReceived()
{
    if (getState() == State::HALF_CLOSED_L) {
        setState(State::CLOSED);
    } else {
        setState(State::HALF_CLOSED_R);
    }
}

void H2Stream::handleDataFrame(DataFrame *frame)
{
    bool endStream = frame->getFlags() & H2_FRAME_FLAG_END_STREAM;
    if (endStream) {
        KUMA_INFOXTRACE("handleDataFrame, END_STREAM received");
        endStreamReceived();
    }
    if (cb_data_) {
        cb_data_((uint8_t*)frame->data(), frame->size(), endStream);
    }
}

void H2Stream::handleHeadersFrame(HeadersFrame *frame)
{
    if (getState() == State::RESERVED_R) {
        setState(State::HALF_CLOSED_L);
    } else if (getState() == State::IDLE) {
        setState(State::OPEN);
    }
    bool endStream = frame->getFlags() & H2_FRAME_FLAG_END_STREAM;
    if (endStream) {
        KUMA_INFOXTRACE("handleHeadersFrame, END_STREAM received");
        endStreamReceived();
    }
    if (cb_headers_) {
        cb_headers_(frame->getHeaders(), endStream);
    }
}

void H2Stream::handlePriorityFrame(PriorityFrame *frame)
{
    
}

void H2Stream::handleRSTStreamFrame(RSTStreamFrame *frame)
{
    KUMA_INFOXTRACE("handleRSTStreamFrame, err="<<frame->getErrorCode()<<", state="<<getState());
    setState(State::CLOSED);
    if (cb_reset_) {
        cb_reset_(frame->getErrorCode());
    }
}

void H2Stream::handleSettingsFrame(SettingsFrame *frame)
{
    
}

void H2Stream::handlePushFrame(PushPromiseFrame *frame)
{
    KUMA_ASSERT(getState() == State::IDLE);
    setState(State::RESERVED_R);
}

void H2Stream::handleWindowUpdateFrame(WindowUpdateFrame *frame)
{
    remoteWindowSize_ += frame->getWindowSizeIncrement();
}

void H2Stream::handleContinuationFrame(ContinuationFrame *frame)
{
    
}
