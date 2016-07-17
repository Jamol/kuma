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

#ifndef __H2Stream_H__
#define __H2Stream_H__

#include "kmdefs.h"
#include "util/kmobject.h"
#include <memory>

#include "h2defs.h"
#include "H2Frame.h"

KUMA_NS_BEGIN

class H2ConnectionImpl;
using H2ConnectionPtr = std::shared_ptr<H2ConnectionImpl>;

class H2Stream : public KMObject
{
public:
    using HeadersCallback = std::function<void(const HeaderVector &, bool)>;
    using DataCallback = std::function<void(uint8_t *, size_t, bool)>;
    using RSTStreamCallback = std::function<void(int)>;
    
public:
	H2Stream(uint32_t streamId);
    
    uint32_t getStreamId() { return streamId_; }
    
    int sendHeaders(const H2ConnectionPtr &conn, const HeaderVector &headers, size_t headersSize,bool endStream);
    int sendData(const H2ConnectionPtr &conn, const uint8_t *data, size_t len, bool endStream = false);
    
    void close(const H2ConnectionPtr &conn);
    
    void setHeadersCallback(HeadersCallback cb) { cb_headers_ = std::move(cb); }
    void setDataCallback(DataCallback cb) { cb_data_ = std::move(cb); }
    void setRSTStreamCallback(RSTStreamCallback cb) { cb_reset_ = std::move(cb); }
    
public:
    void handleDataFrame(DataFrame *frame);
    void handleHeadersFrame(HeadersFrame *frame);
    void handlePriorityFrame(PriorityFrame *frame);
    void handleRSTStreamFrame(RSTStreamFrame *frame);
    void handleSettingsFrame(SettingsFrame *frame);
    void handlePushFrame(PushPromiseFrame *frame);
    void handlePingFrame(PingFrame *frame);
    void handleWindowUpdateFrame(WindowUpdateFrame *frame);
    void handleContinuationFrame(ContinuationFrame *frame);
    
private:
    enum State {
        IDLE,
        RESERVED_L,
        RESERVED_R,
        OPEN,
        HALF_CLOSED_L,
        HALF_CLOSED_R,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() { return state_; }
    
    void localStreamEnd();
    void remoteStreamEnd();

private:
    uint32_t streamId_;
    State state_ = State::IDLE;
    HeadersCallback cb_headers_;
    DataCallback cb_data_;
    RSTStreamCallback cb_reset_;
};

using H2StreamPtr = std::shared_ptr<H2Stream>;

KUMA_NS_END

#endif
