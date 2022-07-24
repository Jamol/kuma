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
#include "libkev/src/utils/kmtrace.h"

#include <algorithm>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
H2Stream::H2Stream(uint32_t stream_id, H2Connection::Impl* conn, uint32_t init_local_window_size, uint32_t init_remote_window_size)
: stream_id_(stream_id), conn_(conn), flow_ctrl_(stream_id, [this] (uint32_t w) { sendWindowUpdate(w); })
{
    flow_ctrl_.initLocalWindowSize(init_local_window_size);
    flow_ctrl_.initRemoteWindowSize(init_remote_window_size);
    flow_ctrl_.setLocalWindowStep(init_local_window_size);
    
    if (stream_id == 1 && !conn->sslEnabled()) { // upgrade stream, set state to HALF_CLOSED_L
        endStreamSent();
    }
    KM_SetObjKey("H2Stream_"<<stream_id);
}

KMError H2Stream::sendPushPromise(const HeaderVector &headers, size_t headers_size, uint32_t stream_id)
{
    if (!isPromisedStream(getStreamId())) {
        return KMError::INVALID_STATE;
    }
    if (getState() != State::IDLE) {
        return KMError::INVALID_STATE;
    }
    PushPromiseFrame frame;
    frame.setStreamId(stream_id);
    frame.setPromisedStreamId(getStreamId());
    frame.addFlags(H2_FRAME_FLAG_END_HEADERS);
    frame.setHeaders(std::move(headers), headers_size);
    auto ret = conn_->sendH2Frame(&frame);
    setState(State::RESERVED_L);
    return ret;
}

KMError H2Stream::sendHeaders(const HeaderVector &headers, size_t headers_size, bool end_stream)
{
    HeadersFrame frame;
    frame.setStreamId(getStreamId());
    frame.addFlags(H2_FRAME_FLAG_END_HEADERS);
    if (end_stream) {
        frame.addFlags(H2_FRAME_FLAG_END_STREAM);
    }
    frame.setHeaders(std::move(headers), headers_size);
    auto ret = conn_->sendH2Frame(&frame);
    if (getState() == State::IDLE) {
        setState(State::OPEN);
    } else if (getState() == State::RESERVED_L) {
        setState(State::HALF_CLOSED_R);
    }
    
    if (end_stream) {
        endStreamSent();
    }
    return ret;
}

int H2Stream::sendData(const void *data, size_t len, bool end_stream)
{
    if (getState() == State::HALF_CLOSED_L || getState() == State::CLOSED) {
        return -1;
    }
    if (write_blocked_) {
        return 0;
    }
    size_t stream_window_size = flow_ctrl_.remoteWindowSize();
    size_t conn_window_size = conn_->remoteWindowSize();
    size_t window_size = std::min<size_t>(stream_window_size, conn_window_size);
    if (0 == window_size && (!end_stream || len != 0)) {
        write_blocked_ = true;
        KM_INFOXTRACE("sendData, remote window 0, cws="<<conn_window_size<<", sws="<<stream_window_size);
        if (conn_window_size == 0) {
            conn_->appendBlockedStream(stream_id_);
        }
        return 0;
    }
    size_t send_len = std::min<size_t>(window_size, len);
    DataFrame frame;
    frame.setStreamId(getStreamId());
    if (end_stream) {
        frame.addFlags(H2_FRAME_FLAG_END_STREAM);
    }
    frame.setData(data, send_len);
    auto ret = conn_->sendH2Frame(&frame);
    //KM_INFOXTRACE("sendData, len="<<len<<", send_len="<<send_len<<", ret="<<int(ret)<<", win="<<flow_ctrl_.remoteWindowSize());
    if (KMError::NOERR == ret) {
        if (end_stream) {
            endStreamSent();
        }
        flow_ctrl_.bytesSent(send_len);
        if (send_len < len) {
            write_blocked_ = true;
            conn_->appendBlockedStream(stream_id_);
        }
        return int(send_len);
    } else if (KMError::AGAIN == ret || KMError::BUFFER_TOO_SMALL == ret) {
        write_blocked_ = true;
        return 0;
    }
    
    return -1;
}

int H2Stream::sendData(const KMBuffer &buf, bool end_stream)
{
    if (getState() == State::HALF_CLOSED_L || getState() == State::CLOSED) {
        return -1;
    }
    if (write_blocked_) {
        return 0;
    }
    auto buf_len = buf.chainLength();
    size_t stream_window_size = flow_ctrl_.remoteWindowSize();
    size_t conn_window_size = conn_->remoteWindowSize();
    size_t window_size = std::min<size_t>(stream_window_size, conn_window_size);
    if (0 == window_size && (!end_stream || buf_len != 0)) {
        write_blocked_ = true;
        KM_INFOXTRACE("sendData, remote window 0, cws="<<conn_window_size<<", sws="<<stream_window_size);
        if (conn_window_size == 0) {
            conn_->appendBlockedStream(stream_id_);
        }
        return 0;
    }
    size_t send_len = window_size < buf_len ? window_size : buf_len;
    DataFrame frame;
    frame.setStreamId(getStreamId());
    if (end_stream) {
        frame.addFlags(H2_FRAME_FLAG_END_STREAM);
    }
    std::unique_ptr<KMBuffer> sub_buf;
    if (send_len < buf_len) {
        sub_buf.reset(buf.subbuffer(0, send_len));
        frame.setData(*sub_buf);
    } else {
        frame.setData(buf);
    }
    auto ret = conn_->sendH2Frame(&frame);
    //KM_INFOXTRACE("sendData, len="<<len<<", send_len="<<send_len<<", ret="<<int(ret)<<", win="<<flow_ctrl_.remoteWindowSize());
    if (KMError::NOERR == ret) {
        if (end_stream) {
            endStreamSent();
        }
        flow_ctrl_.bytesSent(send_len);
        if (send_len < buf_len) {
            write_blocked_ = true;
            conn_->appendBlockedStream(stream_id_);
        }
        return int(send_len);
    } else if (KMError::AGAIN == ret || KMError::BUFFER_TOO_SMALL == ret) {
        write_blocked_ = true;
        return 0;
    }
    
    return -1;
}

KMError H2Stream::sendWindowUpdate(uint32_t delta)
{
    if (getState() == State::CLOSED || getState() == State::HALF_CLOSED_R) {
        return KMError::INVALID_STATE;
    }
    WindowUpdateFrame frame;
    frame.setStreamId(getStreamId());
    frame.setWindowSizeIncrement(delta);
    return conn_->sendH2Frame(&frame);
}

void H2Stream::close()
{
    if (getState() == State::CLOSED || getState() == State::IDLE) {
        return;
    }
    streamError(H2Error::CANCEL);
    if (conn_) {
        conn_->removeStream(getStreamId());
    }
}

void H2Stream::endStreamSent()
{
    end_stream_sent_ = true;
    if (getState() == State::HALF_CLOSED_R) {
        setState(State::CLOSED);
    } else {
        setState(State::HALF_CLOSED_L);
    }
}

void H2Stream::endStreamReceived()
{
    end_stream_received_ = true;
    if (getState() == State::HALF_CLOSED_L) {
        setState(State::CLOSED);
    } else {
        setState(State::HALF_CLOSED_R);
    }
}

void H2Stream::onHeaderCompleted(HeaderVector &headers, bool end_stream)
{
    if (isPromisedStream(getStreamId())) {
        if (getState() == State::RESERVED_R) {
            // headers of server push request
            headers_received_ = false;
            headers_end_ = false;
            if (promise_cb_) {
                promise_cb_(headers);
            }
        } else if (getState() == State::HALF_CLOSED_L) {
            // headers of server push response
            if (headers_cb_) {
                headers_cb_(headers, end_stream);
            }
        }
    } else {
        if (headers_cb_) {
            headers_cb_(headers, end_stream);
        }
    }
}

KMError H2Stream::sendRSTStream(H2Error err)
{
    rst_stream_sent_ = true;
    
    if (conn_) {
        RSTStreamFrame frame;
        frame.setStreamId(stream_id_);
        frame.setErrorCode(uint32_t(err));
        return conn_->sendH2Frame(&frame);
    }
    
    return KMError::NOERR;
}

void H2Stream::connectionError(H2Error err)
{
    if (conn_) {
        conn_->connectionError(err);
    }
}

void H2Stream::streamError(H2Error err)
{
    setState(State::CLOSED);
    sendRSTStream(err);
}

bool H2Stream::handleDataFrame(DataFrame *frame)
{
    if (!verifyFrame(frame)) {
        return false;
    }
    if (isPromisedStream(getStreamId()) && getState() == State::RESERVED_R) {
        // RFC 7540, 8.2
        // Promised request MUST NOT include a request body
        streamError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    if (getState() != State::OPEN && getState() != State::HALF_CLOSED_L) {
        // RFC 7540, 6.1
        streamError(H2Error::STREAM_CLOSED);
        return false;
    }
    bool end_stream = frame->getFlags() & H2_FRAME_FLAG_END_STREAM;
    if (end_stream) {
        KM_INFOXTRACE("handleDataFrame, END_STREAM received");
        endStreamReceived();
    }
    flow_ctrl_.bytesReceived(frame->size());
    if (data_cb_) {
        KMBuffer buf(frame->data(), frame->size(), frame->size());
        data_cb_(buf, end_stream);
    }
    return true;
}

bool H2Stream::handleHeadersFrame(HeadersFrame *frame)
{
    if (!verifyFrame(frame)) {
        return false;
    }
    bool is_tailer = false;
    if (headers_received_ && (getState() == State::OPEN || getState() == State::HALF_CLOSED_L)) {
        // must be tailer
        is_tailer = true;
        tailers_received_ = true;
        tailers_end_ = frame->hasEndHeaders();
    } else {
        headers_received_ = true;
        headers_end_ = frame->hasEndHeaders();
    }
    if (getState() == State::RESERVED_R) {
        setState(State::HALF_CLOSED_L);
    } else if (getState() == State::IDLE) {
        setState(State::OPEN);
    }
    bool end_stream = frame->getFlags() & H2_FRAME_FLAG_END_STREAM;
    if (end_stream) {
        KM_INFOXTRACE("handleHeadersFrame, END_STREAM received");
        endStreamReceived();
    }
    if (!is_tailer && headers_end_) {
        onHeaderCompleted(frame->getHeaders(), end_stream);
    }
    return true;
}

bool H2Stream::handlePriorityFrame(PriorityFrame *frame)
{
    if (!verifyFrame(frame)) {
        return false;
    }
    if (frame->getPriority().stream_id == stream_id_) {
        // RFC 7540, 5.3.1
        streamError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    return true;
}

bool H2Stream::handleRSTStreamFrame(RSTStreamFrame *frame)
{
    //KM_INFOXTRACE("handleRSTStreamFrame, err="<<frame->getErrorCode()<<", state="<<getState());
    if (!verifyFrame(frame)) {
        return false;
    }
    if (getState() == State::CLOSED) {
        return true;
    }
    rst_stream_received_ = true;
    if (reset_cb_) {
        reset_cb_(frame->getErrorCode());
    }
    return true;
}

bool H2Stream::handlePushFrame(PushPromiseFrame *frame)
{
    if (!verifyFrame(frame)) {
        return false;
    }
    headers_received_ = true;
    headers_end_ = frame->hasEndHeaders();
    setState(State::RESERVED_R);
    if (headers_end_) {
        onHeaderCompleted(frame->getHeaders(), false);
    }
    return true;
}

bool H2Stream::handleWindowUpdateFrame(WindowUpdateFrame *frame)
{
    KM_INFOXTRACE("handleWindowUpdateFrame, streamId="<<frame->getStreamId()<<", delta=" << frame->getWindowSizeIncrement()<<", window="<<flow_ctrl_.remoteWindowSize());
    if (!verifyFrame(frame)) {
        return false;
    }
    if (getState() == State::CLOSED) {
        return true;
    }
    if (frame->getWindowSizeIncrement() == 0) {
        // RFC 7540, 6.9
        streamError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    bool need_on_write = 0 == flow_ctrl_.remoteWindowSize();
    flow_ctrl_.updateRemoteWindowSize(frame->getWindowSizeIncrement());
    if (need_on_write && getState() != State::IDLE && flow_ctrl_.remoteWindowSize() > 0) {
        onWrite();
    }
    return true;
}

bool H2Stream::handleContinuationFrame(ContinuationFrame *frame)
{
    if (!verifyFrame(frame)) {
        return false;
    }
    if (getState() != State::OPEN && getState() != State::HALF_CLOSED_L) {
        // invalid status
        return false;
    }
    if ((!headers_received_ || headers_end_) && (!tailers_received_ || tailers_end_)) {
        // PROTOCOL_ERROR
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    bool is_tailer = headers_end_;
    bool end_stream = frame->getFlags() & H2_FRAME_FLAG_END_STREAM;
    if (end_stream) {
        KM_INFOXTRACE("handleContinuationFrame, END_STREAM received");
        endStreamReceived();
    }
    bool end_headers = frame->hasEndHeaders();
    if (end_headers) {
        if (!is_tailer) {
            headers_end_ = true;
        } else {
            tailers_end_ = true;
        }
    }
    if (!is_tailer && headers_end_) {
        onHeaderCompleted(frame->getHeaders(), end_stream);
    }
    return true;
}

void H2Stream::updateRemoteWindowSize(long delta)
{
    flow_ctrl_.updateRemoteWindowSize(delta);
}

bool H2Stream::verifyFrame(H2Frame *frame)
{
    // RFC 7540, 5.1
    switch (getState()) {
        case State::IDLE:
            if (frame->type() != H2FrameType::HEADERS &&
                frame->type() != H2FrameType::PRIORITY &&
                frame->type() != H2FrameType::PUSH_PROMISE) {
                connectionError(H2Error::PROTOCOL_ERROR);
                return false;
            }
            break;
            
        case State::RESERVED_L:
            if (frame->type() != H2FrameType::RST_STREAM &&
                frame->type() != H2FrameType::PRIORITY &&
                frame->type() != H2FrameType::WINDOW_UPDATE) {
                connectionError(H2Error::PROTOCOL_ERROR);
                return false;
            }
            break;
            
        case State::RESERVED_R:
            if (frame->type() != H2FrameType::HEADERS &&
                frame->type() != H2FrameType::RST_STREAM &&
                frame->type() != H2FrameType::PRIORITY) {
                connectionError(H2Error::PROTOCOL_ERROR);
                return false;
            }
            break;
            
        case State::OPEN:
        case State::HALF_CLOSED_L:
            return true;
            
        case State::HALF_CLOSED_R:
            if (frame->type() != H2FrameType::RST_STREAM &&
                frame->type() != H2FrameType::PRIORITY &&
                frame->type() != H2FrameType::WINDOW_UPDATE) {
                streamError(H2Error::STREAM_CLOSED);
                return false;
            }
            break;
            
        case State::CLOSED:
            if (rst_stream_received_ && frame->type() != H2FrameType::PRIORITY) {
                streamError(H2Error::STREAM_CLOSED);
                return false;
            }
            if (end_stream_received_ && frame->type() != H2FrameType::PRIORITY) {
                connectionError(H2Error::STREAM_CLOSED);
                return false;
            }
            break;
            
        default:
            break;
    }
    return true;
}

void H2Stream::setState(State state)
{
    if (conn_) {
        if (!isInOpenState(state_) && isInOpenState(state)) {
            conn_->streamOpened(stream_id_);
        } else if (isInOpenState(state_) && state == State::CLOSED) {
            conn_->streamClosed(stream_id_);
        }
    }
    state_ = state;
}

bool H2Stream::isInOpenState(State state)
{
    return (state == State::OPEN ||
            state == State::HALF_CLOSED_L ||
            state == State::HALF_CLOSED_R);
}

void H2Stream::onWrite()
{
    write_blocked_ = false;
    if (write_cb_) write_cb_();
}

void H2Stream::onError(int err)
{
    conn_ = nullptr;
    if (reset_cb_) reset_cb_(err);
}
