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
#include "kmapi.h"
#include "util/kmobject.h"
#include <memory>

#include "h2defs.h"
#include "H2Frame.h"
#include "FlowControl.h"

KUMA_NS_BEGIN

class H2Stream : public KMObject
{
public:
    using HeadersCallback = std::function<void(const HeaderVector &, bool, bool)>;
    using DataCallback = std::function<void(uint8_t *, size_t, bool)>;
    using RSTStreamCallback = std::function<void(int)>;
    using WriteCallback = std::function<void(void)>;
    
public:
    H2Stream(uint32_t streamId, H2Connection::Impl* conn, uint32_t remoteWindowSize);
    
    uint32_t getStreamId() { return streamId_; }
    
    KMError sendHeaders(const HeaderVector &headers, size_t headersSize,bool endStream);
    int sendData(const uint8_t *data, size_t len, bool endStream = false);
    KMError sendWindowUpdate(uint32_t increment);
    
    void close();
    
    void setHeadersCallback(HeadersCallback cb) { headers_cb_ = std::move(cb); }
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setRSTStreamCallback(RSTStreamCallback cb) { reset_cb_ = std::move(cb); }
    void setWriteCallback(WriteCallback cb) { write_cb_ = std::move(cb); }
    
public:
    void handleDataFrame(DataFrame *frame);
    void handleHeadersFrame(HeadersFrame *frame);
    void handlePriorityFrame(PriorityFrame *frame);
    void handleRSTStreamFrame(RSTStreamFrame *frame);
    void handlePushFrame(PushPromiseFrame *frame);
    void handleWindowUpdateFrame(WindowUpdateFrame *frame);
    void handleContinuationFrame(ContinuationFrame *frame);
    void onWrite();
    void onError(int err);
    
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
    
    void endStreamSent();
    void endStreamReceived();
    
    void sendRSTStream(H2Error err);
    void streamError(H2Error err);

private:
    uint32_t streamId_;
    H2Connection::Impl * conn_;
    State state_ = State::IDLE;
    HeadersCallback headers_cb_;
    DataCallback data_cb_;
    RSTStreamCallback reset_cb_;
    WriteCallback write_cb_;
    
    bool write_blocked_ { false };
    bool headers_received_ { false };
    bool headers_end_ { false };
    bool tailers_received_ {false };
    bool tailers_end_ {false };
    
    FlowControl flow_ctrl_;
};

using H2StreamPtr = std::shared_ptr<H2Stream>;

KUMA_NS_END

#endif
