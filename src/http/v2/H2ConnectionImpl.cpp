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

#include "H2ConnectionImpl.h"
#include "libkev/src/utils/kmtrace.h"
#include "H2ConnectionMgr.h"
#include "H2Handshake.h"
#include "PushClient.h"


using namespace kuma;

namespace {
#ifdef KUMA_HAS_OPENSSL
    static const AlpnProtos alpnProtos{ 2, 'h', '2' };
#endif
}

//////////////////////////////////////////////////////////////////////////
H2ConnectionImpl::H2ConnectionImpl(const EventLoopPtr &loop)
: tcp_conn_(loop), thread_id_(loop->threadId()), frame_parser_(this)
, flow_ctrl_(0, [this] (uint32_t w) { sendWindowUpdate(0, w); })
{
    loop_token_.eventLoop(loop);
    tcp_conn_.setDataCallback([this](uint8_t *data, size_t size) {
        return handleInputData(data, size);
    });
    tcp_conn_.setWriteCallback([this] (KMError) {
        onWrite();
    });
    tcp_conn_.setErrorCallback([this] (KMError err) {
        onError(err);
    });
    
    flow_ctrl_.initLocalWindowSize(H2_LOCAL_CONN_INITIAL_WINDOW_SIZE);
    flow_ctrl_.setMinLocalWindowSize(init_local_window_size_);
    flow_ctrl_.setLocalWindowStep(H2_LOCAL_CONN_INITIAL_WINDOW_SIZE);
    frame_parser_.setMaxFrameSize(max_local_frame_size_);
    KM_SetObjKey("H2Connection");
    KM_INFOXTRACE("H2Connection");
}

H2ConnectionImpl::~H2ConnectionImpl()
{
    KM_INFOXTRACE("~H2Connection");
    close();
    loop_token_.reset();
}

void H2ConnectionImpl::cleanup()
{
    setState(State::CLOSED);
    tcp_conn_.close();
    push_clients_.clear();
}

void H2ConnectionImpl::cleanupAndRemove()
{
    bool secure = tcp_conn_.sslEnabled();
    cleanup();
    H2ConnectionMgr::removeConnection(key_, secure);
}

void H2ConnectionImpl::close_i()
{
    if (getState() > State::IDLE && getState() <= State::OPEN) {
        sendGoaway(H2Error::NOERR);
        auto streams = std::move(streams_);
        for (auto it : streams) {
            it.second->onError((int)H2Error::CANCELLED);
        }
        streams = std::move(promised_streams_);
        for (auto it : streams) {
            it.second->onError((int)H2Error::CANCELLED);
        }
    }
    setState(State::CLOSED);
    cleanupAndRemove();
}

void H2ConnectionImpl::setConnectionKey(const std::string &key)
{
    key_ = key;
    if (!key.empty()) {
        KM_INFOXTRACE("setConnectionKey, key="<<key);
    }
}

KMError H2ConnectionImpl::setProxyInfo(const ProxyInfo &proxy_info)
{
    return tcp_conn_.setProxyInfo(proxy_info);
}

KMError H2ConnectionImpl::connect(const std::string &host, uint16_t port)
{
    if(getState() != State::IDLE) {
        KM_ERRXTRACE("connect, invalid state, state=" << (int)getState());
        return KMError::INVALID_STATE;
    }
    // add to EventLoop to get notification when loop exit
    eventLoop()->appendObserver([this] (kev::LoopActivity acti) {
        onLoopActivity(acti);
    }, &loop_token_);
    return connect_i(host, port);
}

KMError H2ConnectionImpl::connect_i(const std::string &host, uint16_t port)
{
    next_stream_id_ = 1;
    setState(State::CONNECTING);
    
    if (0 == port) {
        port = tcp_conn_.sslEnabled() ? 443 : 80;
    }
    
#ifdef KUMA_HAS_OPENSSL
    if (tcp_conn_.sslEnabled()) {
        tcp_conn_.setAlpnProtocols(alpnProtos);
    }
#endif

    return tcp_conn_.connect(host, port, [this, host] (KMError err) {
        onConnect(err, host);
    });
}

KMError H2ConnectionImpl::attachFd(SOCKET_FD fd, const KMBuffer *init_buf)
{
    if (getState() != State::IDLE || getState() != State::CLOSED) {
        return KMError::INVALID_STATE;
    }
    setupH2Handshake();
    next_stream_id_ = 2;
    auto ret = tcp_conn_.attachFd(fd, init_buf);
    if (ret != KMError::NOERR) {
        return ret;
    }
    
    setState(State::HANDSHAKE);
    return handshake_->start(true, tcp_conn_.sslEnabled());
}

KMError H2ConnectionImpl::attachSocket(TcpSocket::Impl&& tcp, HttpParser::Impl&& parser, const KMBuffer *init_buf)
{
    if (getState() != State::IDLE || getState() != State::CLOSED) {
        return KMError::INVALID_STATE;
    }
    KM_ASSERT(parser.isRequest());
    setupH2Handshake();
    handshake_->setHttpParser(std::move(parser));
    next_stream_id_ = 2;
    
    auto ret = tcp_conn_.attachSocket(std::move(tcp), init_buf);
    if (ret != KMError::NOERR) {
        return ret;
    }
    
    setState(State::HANDSHAKE);
    return handshake_->start(true, tcp_conn_.sslEnabled());
}

KMError H2ConnectionImpl::close()
{
    if (getState() == State::IDLE || getState() == State::CLOSED) {
        return KMError::NOERR;
    }
    KM_INFOXTRACE("close, state=" << (int)getState());
    
    auto loop = eventLoop();
    if (loop) {
        loop->sync([this] { close_i(); });
    }
    else {
        close_i();
    }
    return KMError::NOERR;
}

KMError H2ConnectionImpl::sendData(const KMBuffer &buf)
{
    auto ret = tcp_conn_.send(buf);
    if (ret > 0) {
        return KMError::NOERR;
    } else if (ret == 0) {
        // send blocked
        tcp_conn_.appendSendBuffer(buf);
        return KMError::NOERR;
    } else {
        return KMError::SOCK_ERROR;
    }
}

KMError H2ConnectionImpl::sendH2Frame(H2Frame *frame)
{
    if (getState() == State::CLOSED) {
        return KMError::INVALID_STATE;
    }
    if (!tcp_conn_.sendBufferEmpty() && !isControlFrame(frame) &&
        !(frame->getFlags() & H2_FRAME_FLAG_END_STREAM)) {
        appendBlockedStream(frame->getStreamId());
        return KMError::AGAIN;
    }
    
    if (isControlFrame(frame)) {
        KM_INFOXTRACE("sendH2Frame, type="<<H2FrameTypeToString(frame->type())<<", streamId="<<frame->getStreamId()<<", flags="<<int(frame->getFlags()));
    } else if (frame->getFlags() & H2_FRAME_FLAG_END_STREAM) {
        KM_INFOXTRACE("sendH2Frame, end stream, type="<<H2FrameTypeToString(frame->type())<<", streamId="<<frame->getStreamId());
    }
    
    if (frame->type() == H2FrameType::HEADERS) {
        auto *headers = dynamic_cast<HeadersFrame*>(frame);
        return sendHeadersFrame(headers);
    } else if (frame->type() == H2FrameType::DATA) {
        if (flow_ctrl_.remoteWindowSize() < frame->getPayloadLength()) {
            KM_INFOXTRACE("sendH2Frame, BUFFER_TOO_SMALL, win="<<flow_ctrl_.remoteWindowSize()<<", len="<<frame->getPayloadLength());
            appendBlockedStream(frame->getStreamId());
            return KMError::BUFFER_TOO_SMALL;
        }
        flow_ctrl_.bytesSent(frame->getPayloadLength());
    } else if (frame->type() == H2FrameType::WINDOW_UPDATE && frame->getStreamId() != 0) {
        //WindowUpdateFrame *wu = dynamic_cast<WindowUpdateFrame*>(frame);
        //flow_ctrl_.increaseLocalWindowSize(wu->getWindowSizeIncrement());
    }
    
    size_t payloadSize = frame->calcPayloadSize();
    size_t frameSize = payloadSize + H2_FRAME_HEADER_SIZE;
    
    KMBuffer buf(frameSize);
    int ret = frame->encode((uint8_t*)buf.writePtr(), buf.space());
    if (ret < 0) {
        KM_ERRXTRACE("sendH2Frame, failed to encode frame");
        return KMError::INVALID_PARAM;
    }
    KM_ASSERT(ret == (int)frameSize);
    buf.bytesWritten(ret);
    return sendData(buf);
}

KMError H2ConnectionImpl::sendHeadersFrame(HeadersFrame *frame)
{
    h2_priority_t pri;
    frame->setPriority(pri);
    size_t len1 = H2_FRAME_HEADER_SIZE + (frame->hasPriority()?H2_PRIORITY_PAYLOAD_SIZE:0);
    auto &headers = frame->getHeaders();
    size_t hdrSize = frame->getHeadersSize();
    
    size_t hpackSize = hdrSize * 3 / 2;
    size_t frameSize = len1 + hpackSize;
    
    KMBuffer buf(frameSize);
    int ret = hp_encoder_.encode(headers, (uint8_t*)buf.writePtr() + len1, hpackSize);
    if (ret < 0) {
        return KMError::FAILED;
    }
    size_t bsize = ret;
    ret = frame->encode((uint8_t*)buf.writePtr(), len1, bsize);
    KM_ASSERT(ret == (int)len1);
    buf.bytesWritten(len1 + bsize);
    return sendData(buf);
}

H2StreamPtr H2ConnectionImpl::createStream()
{
    H2StreamPtr stream(new H2Stream(next_stream_id_, this, init_local_window_size_, init_remote_window_size_));
    next_stream_id_ += 2;
    addStream(stream);
    return stream;
}

H2StreamPtr H2ConnectionImpl::createStream(uint32_t stream_id)
{
    H2StreamPtr stream(new H2Stream(stream_id, this, init_local_window_size_, init_remote_window_size_));
    addStream(stream);
    return stream;
}

bool H2ConnectionImpl::handleDataFrame(DataFrame *frame)
{
    if (frame->getStreamId() == 0) {
        // RFC 7540, 6.1
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    flow_ctrl_.bytesReceived(frame->getPayloadLength());
    H2StreamPtr stream = getStream(frame->getStreamId());
    if (stream) {
        return stream->handleDataFrame(frame);
    } else {
        KM_WARNXTRACE("handleDataFrame, no stream, streamId="<<frame->getStreamId()<<", size="<<frame->size()<<", flags="<<int(frame->getFlags()));
        return false;
    }
}

bool H2ConnectionImpl::handleHeadersFrame(HeadersFrame *frame)
{
    KM_INFOXTRACE("handleHeadersFrame, streamId="<<frame->getStreamId()<<", flags="<<int(frame->getFlags()));
    if (frame->getStreamId() == 0) {
        // RFC 7540, 6.2
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    H2StreamPtr stream = getStream(frame->getStreamId());
    if (!stream) {
        if (frame->getStreamId() < last_stream_id_) {
            KM_ERRXTRACE("handleHeadersFrame, PROTOCOL_ERROR, streamId="<<frame->getStreamId()<<", last_id="<<last_stream_id_);
            // RFC 7540, 5.1.1
            connectionError(H2Error::PROTOCOL_ERROR);
            return false;
        }
        if (opened_stream_count_ + 1 > max_concurrent_streams_) {
            KM_WARNXTRACE("handleHeadersFrame, too many concurrent streams, streamId="<<frame->getStreamId()<<", opened="<<opened_stream_count_<<", max="<<max_concurrent_streams_);
            // RFC 7540, 5.1.2
            streamError(frame->getStreamId(), H2Error::REFUSED_STREAM);
            return false;
        }
        if (!tcp_conn_.isServer()) {
            KM_WARNXTRACE("handleHeadersFrame, no local stream or promised stream, streamId="<<frame->getStreamId());
            return false; // client: no local steram or promised stream
        }
    }
    
    if (frame->hasEndHeaders()) {
        HeaderVector headers;
        if (hp_decoder_.decode(frame->getBlock(), frame->getBlockSize(), headers) < 0) {
            KM_ERRXTRACE("handleHeadersFrame, hpack decode failed");
            // RFC 7540, 4.3
            connectionError(H2Error::COMPRESSION_ERROR);
            return false;
        }
        frame->setHeaders(std::move(headers), 0);
    } else {
        expect_continuation_frame_ = true;
        stream_id_of_expected_continuation_ = frame->getStreamId();
        headers_block_buf_.assign(frame->getBlock(), frame->getBlock() + frame->getBlockSize());
    }
    
    if (!stream) {
        // new stream arrived on server side
        stream = createStream(frame->getStreamId());
        last_stream_id_ = frame->getStreamId();
        if (frame->hasEndHeaders()) {
            if (!handleHeadersComplete(frame->getStreamId(), frame->getHeaders())) {
                // user rejects the request
                return false;
            }
        }
    }
    return stream->handleHeadersFrame(frame);
}

bool H2ConnectionImpl::handlePriorityFrame(PriorityFrame *frame)
{
    KM_INFOXTRACE("handlePriorityFrame, streamId="<<frame->getStreamId()<<", dep="<<frame->getPriority().stream_id<<", weight="<<frame->getPriority().weight);
    if (frame->getStreamId() == 0) {
        // RFC 7540, 6.3
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    H2StreamPtr stream = getStream(frame->getStreamId());
    if (stream) {
        return stream->handlePriorityFrame(frame);
    } else {
        return false;
    }
}

bool H2ConnectionImpl::handleRSTStreamFrame(RSTStreamFrame *frame)
{
    KM_INFOXTRACE("handleRSTStreamFrame, streamId="<<frame->getStreamId()<<", err="<<frame->getErrorCode());
    if (frame->getStreamId() == 0) {
        // RFC 7540, 6.4
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    H2StreamPtr stream = getStream(frame->getStreamId());
    if (stream) {
        return stream->handleRSTStreamFrame(frame);
    } else {
        return false;
    }
}

bool H2ConnectionImpl::handleSettingsFrame(SettingsFrame *frame)
{
    KM_INFOXTRACE("handleSettingsFrame, streamId="<<frame->getStreamId()<<", count="<<frame->getParams().size()<<", flags="<<int(frame->getFlags()));
    if (frame->getStreamId() != 0) {
        // RFC 7540, 6.5
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    if (frame->isAck()) {
        if (frame->getParams().size() != 0) {
            // RFC 7540, 6.5
            connectionError(H2Error::FRAME_SIZE_ERROR);
            return false;
        }
        return true;
    } else { // send setings ack
        SettingsFrame settings;
        settings.setStreamId(frame->getStreamId());
        settings.setAck(true);
        sendH2Frame(&settings);
    }

    if (!applySettings(frame->getParams())) {
        return false;
    }
    return true;
}

bool H2ConnectionImpl::handlePushFrame(PushPromiseFrame *frame)
{
    KM_INFOXTRACE("handlePushFrame, streamId="<<frame->getStreamId()<<", promStreamId="<<frame->getPromisedStreamId()<<", bsize="<<frame->getBlockSize()<<", flags="<<int(frame->getFlags()));

    if (frame->getStreamId() == 0) {
        // RFC 7540, 6.6
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    
    // TODO: if SETTINGS_ENABLE_PUSH was set to 0 and got the ack,
    // then response connection error of type PROTOCOL_ERROR
    
    if (!isPromisedStream(frame->getPromisedStreamId())) {
        KM_ERRXTRACE("handlePushFrame, invalid stream id");
        // RFC 7540, 5.1.1
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    
    H2StreamPtr associatedStream = getStream(frame->getStreamId());
    if (!associatedStream) {
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    if (associatedStream->getState() != H2Stream::State::OPEN &&
        associatedStream->getState() != H2Stream::State::HALF_CLOSED_L) {
        // RFC 7540, 6.6
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    
    if (frame->hasEndHeaders()) {
        HeaderVector headers;
        if (hp_decoder_.decode(frame->getBlock(), frame->getBlockSize(), headers) < 0) {
            KM_ERRXTRACE("handlePushFrame, hpack decode failed");
            // RFC 7540, 4.3
            connectionError(H2Error::COMPRESSION_ERROR);
            return false;
        }
        frame->setHeaders(std::move(headers), 0);
    } else {
        expect_continuation_frame_ = true;
        stream_id_of_expected_continuation_ = frame->getPromisedStreamId();
        headers_block_buf_.assign(frame->getBlock(), frame->getBlock() + frame->getBlockSize());
    }
    
    auto stream = createStream(frame->getPromisedStreamId());
    PushClientPtr client(new PushClient());
    client->attachStream(this, stream);
    addPushClient(stream->getStreamId(), std::move(client));
    if (frame->hasEndHeaders()) {
        if (!handleHeadersComplete(frame->getPromisedStreamId(), frame->getHeaders())) {
            return false;
        }
    }
    return stream->handlePushFrame(frame);
}

bool H2ConnectionImpl::handlePingFrame(PingFrame *frame)
{
    KM_INFOXTRACE("handlePingFrame, streamId="<<frame->getStreamId());
    if (frame->getStreamId() != 0) {
        // RFC 7540, 6.7
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    if (!frame->isAck()) {
        PingFrame pingFrame;
        pingFrame.setStreamId(0);
        pingFrame.setAck(true);
        pingFrame.setData(frame->getData(), H2_PING_PAYLOAD_SIZE);
        sendH2Frame(&pingFrame);
    }
    return true;
}

bool H2ConnectionImpl::handleGoawayFrame(GoawayFrame *frame)
{
    KM_INFOXTRACE("handleGoawayFrame, streamId="<<frame->getLastStreamId()<<", err="<<frame->getErrorCode());
    if (frame->getStreamId() != 0) {
        // RFC 7540, 6.8
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    tcp_conn_.close();
    auto streams = std::move(streams_);
    for (auto it : streams) {
        it.second->onError(frame->getErrorCode());
    }
    streams = std::move(promised_streams_);
    for (auto it : streams) {
        it.second->onError(frame->getErrorCode());
    }
    auto error_cb = std::move(error_cb_);
    removeSelf();
    if (error_cb) {
        error_cb(frame->getErrorCode());
    }
    return true;
}

bool H2ConnectionImpl::handleWindowUpdateFrame(WindowUpdateFrame *frame)
{
    if (frame->getWindowSizeIncrement() == 0) {
        // RFC 7540, 6.9
        streamError(frame->getStreamId(), H2Error::PROTOCOL_ERROR);
        return false;
    }
    if (flow_ctrl_.remoteWindowSize() + frame->getWindowSizeIncrement() > H2_MAX_WINDOW_SIZE) {
        if (frame->getStreamId() == 0) {
            connectionError(H2Error::FLOW_CONTROL_ERROR);
        } else {
            streamError(frame->getStreamId(), H2Error::FLOW_CONTROL_ERROR);
        }
        return false;
    }
    if (frame->getStreamId() == 0) {
        KM_INFOXTRACE("handleWindowUpdateFrame, streamId="<<frame->getStreamId()<<", delta=" << frame->getWindowSizeIncrement()<<", window="<<flow_ctrl_.remoteWindowSize());
        if (frame->getWindowSizeIncrement() == 0) {
            connectionError(H2Error::PROTOCOL_ERROR);
            return false;
        }
        bool need_notify = !blocked_streams_.empty();
        flow_ctrl_.updateRemoteWindowSize(frame->getWindowSizeIncrement());
        if (need_notify && flow_ctrl_.remoteWindowSize() > 0) {
            notifyBlockedStreams();
        }
        return true;
    } else {
        H2StreamPtr stream = getStream(frame->getStreamId());
        if (!stream && tcp_conn_.isServer()) {
            // new stream arrived on server side
            stream = createStream(frame->getStreamId());
            last_stream_id_ = frame->getStreamId();
        }
        if (stream) {
            return stream->handleWindowUpdateFrame(frame);
        } else {
            return false;
        }
    }
}

bool H2ConnectionImpl::handleContinuationFrame(ContinuationFrame *frame)
{
    KM_INFOXTRACE("handleContinuationFrame, streamId="<<frame->getStreamId()<<", flags="<<int(frame->getFlags()));
    if (frame->getStreamId() == 0) {
        // RFC 7540, 6.10
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    if (!expect_continuation_frame_) {
        // RFC 7540, 6.10
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    H2StreamPtr stream = getStream(frame->getStreamId());
    if (stream) {
        headers_block_buf_.insert(headers_block_buf_.end(),
                                  frame->getBlock(),
                                  frame->getBlock() + frame->getBlockSize());
        if (frame->hasEndHeaders()) {
            HeaderVector headers;
            uint8_t *headers_block = headers_block_buf_.empty()?nullptr:&headers_block_buf_[0];
            if (hp_decoder_.decode(headers_block, headers_block_buf_.size(), headers) < 0) {
                KM_ERRXTRACE("handleContinuationFrame, hpack decode failed");
                // RFC 7540, 4.3
                connectionError(H2Error::COMPRESSION_ERROR);
                return false;
            }
            frame->setHeaders(std::move(headers), 0);
            expect_continuation_frame_ = false;
            headers_block_buf_.clear();
            if (!handleHeadersComplete(frame->getStreamId(), frame->getHeaders())) {
                // the stream is rejected by user
                return false;
            }
        }
        return stream->handleContinuationFrame(frame);
    }
    return false;
}

bool H2ConnectionImpl::handleHeadersComplete(uint32_t stream_id, const HeaderVector &header_vec)
{
    if (tcp_conn_.isServer() && !isPromisedStream(stream_id) && accept_cb_) {
        std::string method, path, host, protocol;
        for (auto const &kv : header_vec) {
            if (kv.first[0] != ':') {
                break;
            }
            if (method.empty() && kev::is_equal(kv.first, H2HeaderMethod)) {
                method = kv.second;
            } else if (path.empty() && kev::is_equal(kv.first, H2HeaderPath)) {
                path = kv.second;
            } else if (protocol.empty() && kev::is_equal(kv.first, H2HeaderProtocol)) {
                protocol = kv.second;
            } else if (host.empty() && kev::is_equal(kv.first, H2HeaderAuthority)) {
                host = kv.second;
            }
            if (!method.empty() && !path.empty() && !host.empty() && !protocol.empty()) {
                break;
            }
        }
        if (!accept_cb_(stream_id, method.c_str(), path.c_str(), host.c_str(), protocol.c_str())) {
            removeStream(stream_id);
            return false;
        }
    }
    return true;
}

KMError H2ConnectionImpl::handleInputData(uint8_t *buf, size_t len)
{
    if (getState() == State::OPEN) {
        return parseInputData(buf, len);
    } else if (getState() == State::HANDSHAKE) {
        // H2 connection will be destroyed when invalid http request received
        DESTROY_DETECTOR_SETUP();
        auto ret = handshake_->parseInputData(buf, len);
        DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
        if (getState() == State::IN_ERROR) {
            return KMError::FAILED;
        }
        if (getState() == State::CLOSED) {
            return KMError::NOERR;
        }
        if (ret >= len) {
            return KMError::NOERR;
        }
        
        len -= ret;
        buf += ret;
        if (len > 0) {
            if (getState() == State::OPEN) {
                return parseInputData(buf, len);
            } else {
                KM_WARNXTRACE("handleInputData, handshake is not complete, len=" << len
                    << ", state=" << (int)getState());
            }
        }
    } else {
        KM_WARNXTRACE("handleInputData, invalid state, len="<< len
            << ", state=" << (int)getState());
    }
    return KMError::NOERR;
}

KMError H2ConnectionImpl::parseInputData(const uint8_t *buf, size_t len)
{
    DESTROY_DETECTOR_SETUP();
    auto parse_state = frame_parser_.parseInputData(buf, len);
    DESTROY_DETECTOR_CHECK(KMError::DESTROYED);
    if(getState() == State::IN_ERROR || getState() == State::CLOSED) {
        return KMError::INVALID_STATE;
    }
    if(parse_state == FrameParser::ParseState::FAILURE ||
       parse_state == FrameParser::ParseState::STOPPED) {
        KM_ERRXTRACE("parseInputData, failed, len="<< len <<", state=" << (int)getState());
        setState(State::CLOSED);
        cleanupAndRemove();
        return KMError::FAILED;
    }
    return KMError::NOERR;
}

bool H2ConnectionImpl::onFrame(H2Frame *frame)
{
    if (getState() == State::HANDSHAKE && frame->type() != H2FrameType::SETTINGS) {
        // RFC 7540, 3.5
        // the first frame must be SETTINGS, otherwise PROTOCOL_ERROR on connection
        KM_ERRXTRACE("onFrame, the first frame is not SETTINGS, type="<<H2FrameTypeToString(frame->type()));
        // don't send goaway since connection is not open
        setState(State::CLOSED);
        if (error_cb_) {
            error_cb_(int(H2Error::PROTOCOL_ERROR));
        }
        return false;
    }
    if (expect_continuation_frame_ &&
        (frame->type() != H2FrameType::CONTINUATION ||
         frame->getStreamId() != stream_id_of_expected_continuation_)) {
        connectionError(H2Error::PROTOCOL_ERROR);
        return false;
    }
    switch (frame->type()) {
        case H2FrameType::DATA:
            handleDataFrame(dynamic_cast<DataFrame*>(frame));
            break;
            
        case H2FrameType::HEADERS:
            handleHeadersFrame(dynamic_cast<HeadersFrame*>(frame));
            break;
            
        case H2FrameType::PRIORITY:
            handlePriorityFrame(dynamic_cast<PriorityFrame*>(frame));
            break;
            
        case H2FrameType::RST_STREAM:
            handleRSTStreamFrame(dynamic_cast<RSTStreamFrame*>(frame));
            break;
            
        case H2FrameType::SETTINGS:
            handleSettingsFrame(dynamic_cast<SettingsFrame*>(frame));
            break;
            
        case H2FrameType::PUSH_PROMISE:
            handlePushFrame(dynamic_cast<PushPromiseFrame*>(frame));
            break;
            
        case H2FrameType::PING:
            handlePingFrame(dynamic_cast<PingFrame*>(frame));
            break;
            
        case H2FrameType::GOAWAY:
            handleGoawayFrame(dynamic_cast<GoawayFrame*>(frame));
            break;
            
        case H2FrameType::WINDOW_UPDATE:
            handleWindowUpdateFrame(dynamic_cast<WindowUpdateFrame*>(frame));
            break;
            
        case H2FrameType::CONTINUATION:
            handleContinuationFrame(dynamic_cast<ContinuationFrame*>(frame));
            break;
            
        default:
            break;
    }
    return true;
}

void H2ConnectionImpl::onFrameError(const FrameHeader &hdr, H2Error err, bool stream_err)
{
    KM_ERRXTRACE("onFrameError, streamId="<<hdr.getStreamId()<<", type="<<hdr.getType()<<", err="<<int(err)<<", stream_err="<<stream_err);
    if (stream_err) {
        streamError(hdr.getStreamId(), err);
    } else {
        connectionError(err);
    }
}

void H2ConnectionImpl::addStream(H2StreamPtr stream)
{
    KM_INFOXTRACE("addStream, streamId="<<stream->getStreamId());
    if (isPromisedStream(stream->getStreamId())) {
        promised_streams_[stream->getStreamId()] = stream;
    } else {
        streams_[stream->getStreamId()] = stream;
    }
}

H2StreamPtr H2ConnectionImpl::getStream(uint32_t stream_id)
{
    auto &streams = isPromisedStream(stream_id) ? promised_streams_ : streams_;
    auto it = streams.find(stream_id);
    if (it != streams.end()) {
        return it->second;
    }
    return H2StreamPtr();
}

void H2ConnectionImpl::removeStream(uint32_t stream_id)
{
    KM_INFOXTRACE("removeStream, streamId="<<stream_id);
    if (isPromisedStream(stream_id)) {
        promised_streams_.erase(stream_id);
    } else {
        streams_.erase(stream_id);
    }
}

void H2ConnectionImpl::addPushClient(uint32_t push_id, PushClientPtr client)
{
    push_clients_[push_id] = std::move(client);
}

void H2ConnectionImpl::removePushClient(uint32_t push_id)
{
    push_clients_.erase(push_id);
}

PushClient* H2ConnectionImpl::getPushClient(const std::string &cache_key)
{
    for (auto &it : push_clients_) {
        if (kev::is_equal(cache_key, it.second->getCacheKey())) {
            return it.second.get();
        }
    }
    return nullptr;
}

void H2ConnectionImpl::addConnectListener(long uid, ConnectCallback cb)
{
    connect_listeners_[uid] = std::move(cb);
}

void H2ConnectionImpl::removeConnectListener(long uid)
{
    connect_listeners_.erase(uid);
}

void H2ConnectionImpl::appendBlockedStream(uint32_t stream_id)
{
    blocked_streams_[stream_id] = stream_id;
}

void H2ConnectionImpl::notifyBlockedStreams()
{
    if (!tcp_conn_.sendBufferEmpty() || remoteWindowSize() == 0) {
        return;
    }
    auto streams = std::move(blocked_streams_);
    auto it = streams.begin();
    while (it != streams.end() && tcp_conn_.sendBufferEmpty() && remoteWindowSize() > 0) {
        uint32_t stream_id = it->second;
        it = streams.erase(it);
        auto stream = getStream(stream_id);
        if (stream) {
            stream->onWrite();
        }
    }
    if (!streams.empty()) {
        blocked_streams_.insert(streams.begin(), streams.end());
    }
}

void H2ConnectionImpl::onLoopActivity(kev::LoopActivity acti)
{
    if (acti == kev::LoopActivity::EXIT) {
        KM_INFOXTRACE("loop exit");
        setState(State::CLOSED);
        tcp_conn_.close();
        push_clients_.clear();
        loop_token_.reset();
        removeSelf();
    }
}

bool H2ConnectionImpl::sync(EventLoop::Task task)
{
    if (isInSameThread()) {
        task();
        return true;
    }
    auto loop = eventLoop();
    if (loop) {
        return loop->sync(std::move(task)) == kev::Result::OK;
    }
    return false;
}

bool H2ConnectionImpl::async(EventLoop::Task task, EventLoopToken *token)
{
    if (isInSameThread()) {
        task();
        return true;
    }
    auto loop = eventLoop();
    if (loop) {
        return loop->async(std::move(task), token) == kev::Result::OK;
    }
    return false;
}

void H2ConnectionImpl::setupH2Handshake()
{
    handshake_.reset(new H2Handshake());
    handshake_->setLocalWindowSize(flow_ctrl_.localWindowSize());
    handshake_->setHandshakeSender([this] (KMBuffer &buf) {
        return sendData(buf);
    });
    handshake_->setHandshakeCallback([this] (SettingsFrame *frame) {
        onHandshakeComplete(frame);
    });
    handshake_->setErrorCallback([this] (KMError err) {
        onHandshakeError(err);
    });
}

void H2ConnectionImpl::onHandshakeComplete(SettingsFrame *frame)
{
    KM_INFOXTRACE("onHandshakeComplete, state=" << (int)getState());
    if (handleSettingsFrame(frame)) {
        onStateOpen();
    }
}

void H2ConnectionImpl::onHandshakeError(KMError err)
{
    KM_INFOXTRACE("onHandshakeError, err=" << int(err));
    onConnectError(err);
}

void H2ConnectionImpl::onConnect(KMError err, const std::string &host)
{
    KM_INFOXTRACE("onConnect, err="<<int(err));
    if(err != KMError::NOERR) {
        onConnectError(err);
        return ;
    }
    setState(State::HANDSHAKE);
    setupH2Handshake();
    handshake_->setHost(host);
    handshake_->start(false, tcp_conn_.sslEnabled());
}

void H2ConnectionImpl::onWrite()
{// send_buffer_ must be empty
    if (getState() == State::OPEN) {
        notifyBlockedStreams();
    }
}

void H2ConnectionImpl::onError(KMError err)
{
    KM_INFOXTRACE("onError, err="<<int(err));
    auto error_cb(std::move(error_cb_));
    setState(State::IN_ERROR);
    cleanupAndRemove();
    if (error_cb) {
        error_cb(int(err));
    }
}

void H2ConnectionImpl::onConnectError(KMError err)
{
    setState(State::IN_ERROR);
    cleanup();
    auto conn_key(std::move(key_));
    auto secure = tcp_conn_.sslEnabled();
    notifyListeners(err);
    H2ConnectionMgr::removeConnection(conn_key, secure);
}

KMError H2ConnectionImpl::sendWindowUpdate(uint32_t stream_id, uint32_t delta)
{
    WindowUpdateFrame frame;
    frame.setStreamId(stream_id);
    frame.setWindowSizeIncrement(delta);
    return sendH2Frame(&frame);
}

bool H2ConnectionImpl::isControlFrame(H2Frame *frame)
{
    return frame->type() != H2FrameType::DATA;
}

bool H2ConnectionImpl::applySettings(const ParamVector &params)
{
    for (auto &kv : params) {
        KM_INFOXTRACE("applySettings, id="<<kv.first<<", value="<<kv.second);
        switch (kv.first) {
            case HEADER_TABLE_SIZE:
                hp_decoder_.setMaxTableSize(kv.second);
                break;
            case INITIAL_WINDOW_SIZE:
                if (kv.second > H2_MAX_WINDOW_SIZE) {
                    // RFC 7540, 6.5.2
                    connectionError(H2Error::FLOW_CONTROL_ERROR);
                    return false;
                }
                updateInitialWindowSize(kv.second);
                break;
            case MAX_FRAME_SIZE:
                if (kv.second < H2_DEFAULT_FRAME_SIZE || kv.second > H2_MAX_FRAME_SIZE) {
                    // RFC 7540, 6.5.2
                    connectionError(H2Error::PROTOCOL_ERROR);
                    return false;
                }
                max_remote_frame_size_ = kv.second;
                break;
            case MAX_CONCURRENT_STREAMS:
                break;
            case ENABLE_PUSH:
                if (kv.second != 0 && kv.second != 1) {
                    // RFC 7540, 6.5.2
                    connectionError(H2Error::PROTOCOL_ERROR);
                    return false;
                }
                break;
            case ENABLE_CONNECT_PROTOCOL:
                enable_connect_protocol_ = kv.second == 1;
                break;
        }
    }
    return true;
}

void H2ConnectionImpl::updateInitialWindowSize(uint32_t ws)
{
    if (ws != init_remote_window_size_) {
        long delta = int32_t(ws - init_remote_window_size_);
        init_remote_window_size_ = ws;
        for (auto it : streams_) {
            it.second->updateRemoteWindowSize(delta);
        }
        for (auto it : promised_streams_) {
            it.second->updateRemoteWindowSize(delta);
        }
    }
}

void H2ConnectionImpl::sendGoaway(H2Error err)
{
    KM_INFOXTRACE("sendGoaway, err="<<int(err)<<", last="<<last_stream_id_);
    GoawayFrame frame;
    frame.setErrorCode(uint32_t(err));
    frame.setStreamId(0);
    frame.setLastStreamId(last_stream_id_);
    sendH2Frame(&frame);
}

void H2ConnectionImpl::connectionError(H2Error err)
{
    sendGoaway(err);
    setState(State::CLOSED);
    if (error_cb_) {
        error_cb_(int(err));
    }
}

void H2ConnectionImpl::streamError(uint32_t stream_id, H2Error err)
{
    H2StreamPtr stream = getStream(stream_id);
    if (stream) {
        stream->streamError(err);
    } else {
        RSTStreamFrame frame;
        frame.setStreamId(stream_id);
        frame.setErrorCode(uint32_t(err));
        sendH2Frame(&frame);
    }
}

void H2ConnectionImpl::streamOpened(uint32_t stream_id)
{
    ++opened_stream_count_;
}

void H2ConnectionImpl::streamClosed(uint32_t stream_id)
{
    --opened_stream_count_;
}

void H2ConnectionImpl::onStateOpen()
{
    KM_INFOXTRACE("onStateOpen");
    setState(State::OPEN);
    handshake_.reset();
    if (!tcp_conn_.isServer()) {
        // stream 1 for upgrade response
        // stream 1 data is discarded
        if (!tcp_conn_.sslEnabled()) {
            auto stream = createStream();
        }
        notifyListeners(KMError::NOERR);
    }
}

void H2ConnectionImpl::notifyListeners(KMError err)
{
    auto listeners(std::move(connect_listeners_));
    for (auto it : listeners) {
        if (it.second) {
            it.second(err);
        }
    }
}

void H2ConnectionImpl::removeSelf()
{
    if (!key_.empty()) {
        std::string key(std::move(key_));
        // will destroy self when calling from loop stop
        H2ConnectionMgr::removeConnection(key, tcp_conn_.sslEnabled());
    }
}
