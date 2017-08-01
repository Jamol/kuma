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

// H2Stream works on H2Connection thread
class H2Stream : public KMObject
{
public:
    using HeadersCallback = std::function<void(const HeaderVector &, bool)>;
    using DataCallback = std::function<void(void *, size_t, bool)>;
    using RSTStreamCallback = std::function<void(int)>;
    using WriteCallback = std::function<void(void)>;
    
public:
    H2Stream(uint32_t stream_id, H2Connection::Impl* conn, uint32_t init_local_window_size, uint32_t init_remote_window_size);
    
    uint32_t getStreamId() { return stream_id_; }
    
    KMError sendHeaders(const HeaderVector &headers, size_t headers_size,bool end_stream);
    int sendData(const void *data, size_t len, bool end_stream = false);
    KMError sendWindowUpdate(uint32_t delta);
    
    void close();
    
    void setHeadersCallback(HeadersCallback cb) { headers_cb_ = std::move(cb); }
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setRSTStreamCallback(RSTStreamCallback cb) { reset_cb_ = std::move(cb); }
    void setWriteCallback(WriteCallback cb) { write_cb_ = std::move(cb); }
    
public:
    bool handleDataFrame(DataFrame *frame);
    bool handleHeadersFrame(HeadersFrame *frame);
    bool handlePriorityFrame(PriorityFrame *frame);
    bool handleRSTStreamFrame(RSTStreamFrame *frame);
    bool handlePushFrame(PushPromiseFrame *frame);
    bool handleWindowUpdateFrame(WindowUpdateFrame *frame);
    bool handleContinuationFrame(ContinuationFrame *frame);
    void onWrite();
    void onError(int err);
    void updateRemoteWindowSize(long delta);
    void streamError(H2Error err);
    
    enum State {
        IDLE,
        RESERVED_L,
        RESERVED_R,
        OPEN,
        HALF_CLOSED_L,
        HALF_CLOSED_R,
        CLOSED
    };
    State getState() const { return state_; }
    
protected:
    void setState(State state);
    bool isInOpenState(State state);
    
    void endStreamSent();
    void endStreamReceived();
    
    KMError sendRSTStream(H2Error err);
    bool verifyFrame(H2Frame *frame);
    
    void connectionError(H2Error err);

protected:
    uint32_t stream_id_;
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
    
    bool end_stream_sent_ { false };
    bool end_stream_received_ { false };
    bool rst_stream_sent_ { false };
    bool rst_stream_received_ { false };
    
    FlowControl flow_ctrl_;
};

using H2StreamPtr = std::shared_ptr<H2Stream>;

KUMA_NS_END

#endif
