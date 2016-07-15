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
#include "kmtrace.h"
#include "util/base64.h"
#include "H2ConnectionMgr.h"

#include <sstream>

using namespace kuma;

KUMA_NS_BEGIN
static const AlpnProtos alpnProtos {2, 'h', '2'};
KUMA_NS_END

//////////////////////////////////////////////////////////////////////////
H2ConnectionImpl::H2ConnectionImpl(EventLoopImpl* loop)
: loop_(loop), parser_(this), tcp_(loop)
{
    
}

H2ConnectionImpl::~H2ConnectionImpl()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
    if(initData_) {
        delete [] initData_;
        initData_ = nullptr;
        initSize_ = 0;
    }
}

const char* H2ConnectionImpl::getObjKey() const
{
    return "H2Connection";
}

void H2ConnectionImpl::cleanup()
{
    if (!key_.empty()) {
        auto &connMgr = H2ConnectionMgr::getRequestConnMgr(tcp_.SslEnabled());
        connMgr.removeConnection(key_);
        key_.clear();
    }
    tcp_.close();
}

int H2ConnectionImpl::setSslFlags(uint32_t ssl_flags)
{
    return tcp_.setSslFlags(ssl_flags);
}

int H2ConnectionImpl::connect(const std::string &host, uint16_t port, ConnectCallback cb)
{
    if(getState() != State::IDLE) {
        KUMA_ERRXTRACE("connect, invalid state, state="<<getState());
        return KUMA_ERROR_INVALID_STATE;
    }
    cb_ = cb;
    return connect_i(host, port);
}

int H2ConnectionImpl::connect_i(const std::string &host, uint16_t port)
{
    isServer_ = false;
    nextStreamId_ = 1;
    host_ = host;
    port_ = port;
    tcp_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_.setErrorCallback([this] (int err) { onClose(err); });
    httpParser_.setDataCallback([this] (const char* data, size_t len) { onHttpData(data, len); });
    httpParser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    setState(State::CONNECTING);
    
    if (0 == port) {
        port = tcp_.SslEnabled() ? 443: 80;
    }
    
#ifdef KUMA_HAS_OPENSSL
    if (tcp_.SslEnabled()) {
        tcp_.setAlpnProtocols(alpnProtos);
    }
#endif

    return tcp_.connect(host.c_str(), port, [this] (int err) { onConnect(err); });
}

int H2ConnectionImpl::attachFd(SOCKET_FD fd, const uint8_t* data, size_t size)
{
    isServer_ = true;
    nextStreamId_ = 2;
    if(data && size > 0) {
        initData_ = new uint8_t(size);
        memcpy(initData_, data, size);
        initSize_ = size;
    }
    tcp_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_.setErrorCallback([this] (int err) { onClose(err); });
    if (tcp_.SslEnabled()) {
        setState(State::OPEN);
    } else {
        setState(State::HANDSHAKE);
    }
    return tcp_.attachFd(fd);
}

int H2ConnectionImpl::attachSocket(TcpSocketImpl&& tcp, HttpParserImpl&& parser)
{
    KUMA_ASSERT(parser.isRequest());
    httpParser_ = std::move(parser);
    isServer_ = true;
    nextStreamId_ = 2;
    tcp_.setReadCallback([this] (int err) { onReceive(err); });
    tcp_.setWriteCallback([this] (int err) { onSend(err); });
    tcp_.setErrorCallback([this] (int err) { onClose(err); });
    if (tcp.SslEnabled()) {
        setState(State::OPEN);
    } else {
        setState(State::HANDSHAKE);
    }
    
#ifdef KUMA_HAS_OPENSSL
    SOCKET_FD fd;
    SSL* ssl = nullptr;
    uint32_t ssl_flags = tcp.getSslFlags();
    int ret = tcp.detachFd(fd, ssl);
    tcp_.setSslFlags(ssl_flags);
    ret = tcp_.attachFd(fd, ssl);
#else
    SOCKET_FD fd;
    int ret = tcp.detachFd(fd);
    ret = tcp_.attachFd(fd);
#endif
    if (ret != KUMA_ERROR_NOERR) {
        return ret;
    }
    
    return handleRequest();
}

int H2ConnectionImpl::close()
{
    cleanup();
    return KUMA_ERROR_NOERR;
}

int H2ConnectionImpl::sendSetting(H2StreamPtr &stream, ParamVector &params)
{
    std::vector<uint8_t> buf;
    buf.resize(H2_FRAME_HEADER_SIZE + params.size() * 6);
    SettingsFrame frame;
    frame.encode(&buf[0], buf.size(), params, false);
    tcp_.send(&buf[0], buf.size());
    return KUMA_ERROR_NOERR;
}

int H2ConnectionImpl::sendHeaders(H2StreamPtr &stream, const HeaderVector &headers, size_t hdrSize)
{
    bool hasPriority = true;
    h2_priority_t pri;
    size_t len1 = H2_FRAME_HEADER_SIZE + (hasPriority?H2_PRIORITY_PAYLOAD_SIZE:0);
    send_buffer_.resize(len1 + hdrSize * 1.5);
    int ret = hpEncoder_.encode(headers, &send_buffer_[0] + len1, send_buffer_.size() - len1);
    if (ret < 0) {
        return KUMA_ERROR_FAILED;
    }
    size_t bsize = ret;
    HeadersFrame frame;
    ret = frame.encode(&send_buffer_[0], len1, stream->getStreamId(), bsize, &pri);
    KUMA_ASSERT(ret == len1);
    size_t total_len = len1 + bsize;
    send_buffer_.resize(total_len);
    send_offset_ = 0;
    onSend(0);
    return KUMA_ERROR_NOERR;
}

int H2ConnectionImpl::sendData(H2StreamPtr &stream, const uint8_t *data, size_t size)
{
    return 0;
}

H2StreamPtr H2ConnectionImpl::createStream() {
    H2StreamPtr stream(new H2Stream(nextStreamId_));
    nextStreamId_ += 2;
    addStream(stream);
    return stream;
}

void H2ConnectionImpl::handleDataFrame(DataFrame *frame)
{
    H2StreamPtr stream = getStream(frame->getStreamId());
}

void H2ConnectionImpl::handleHeadersFrame(HeadersFrame *frame)
{
    KUMA_INFOXTRACE("handleHeadersFrame, streamId="<<frame->getStreamId());
    H2StreamPtr stream = getStream(frame->getStreamId());
    
    HeaderVector headers;
    if (hpDecoder_.decode(frame->getBlock(), frame->getBlockSize(), headers) > 0) {
        
    }
}

void H2ConnectionImpl::handlePriorityFrame(PriorityFrame *frame)
{
    
}

void H2ConnectionImpl::handleRSTStreamFrame(RSTStreamFrame *frame)
{
    KUMA_INFOXTRACE("handleRSTStreamFrame, streamId="<<frame->getStreamId());
}

void H2ConnectionImpl::handleSettingsFrame(SettingsFrame *frame)
{
    KUMA_INFOXTRACE("handleSettingsFrame");
}

void H2ConnectionImpl::handleGoawayFrame(GoawayFrame *frame)
{
    KUMA_INFOXTRACE("handleGoawayFrame, streamId="<<frame->getLastStreamId()<<", err="<<frame->getErrorCode());
}

void H2ConnectionImpl::handleWindowUpdateFrame(WindowUpdateFrame *frame)
{
    KUMA_INFOXTRACE("handleWindowUpdateFrame, size=" << frame->getWindowSizeIncrement());
}

void H2ConnectionImpl::handleContinuationFrame(ContinuationFrame *frame)
{
    KUMA_INFOXTRACE("handleContinuationFrame, streamId="<<frame->getStreamId());
}

int H2ConnectionImpl::handleInputData(const uint8_t *buf, size_t len)
{
    if (getState() == State::HANDSHAKE) {
        httpParser_.parse((char*)buf, (uint32_t)len);
    } else if (getState() == State::OPEN) {
        bool destroyed = false;
        KUMA_ASSERT(nullptr == destroy_flag_ptr_);
        destroy_flag_ptr_ = &destroyed;
        auto parseState = parser_.parseInputData(buf, len);
        if(destroyed) {
            return KUMA_ERROR_DESTROYED;
        }
        destroy_flag_ptr_ = nullptr;
        if(getState() == State::ERROR || getState() == State::CLOSED) {
            return KUMA_ERROR_FAILED;
        }
        if(parseState == FrameParser::ParseState::FAILURE) {
            cleanup();
            setState(State::CLOSED);
            return KUMA_ERROR_FAILED;
        }
    } else {
        KUMA_WARNXTRACE("handleInputData, invalid state: "<<getState());
    }
    return KUMA_ERROR_NOERR;
}

void H2ConnectionImpl::onFrame(H2Frame *frame)
{
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
}

void H2ConnectionImpl::onFrameError(const FrameHeader &hdr, H2Error err)
{
    KUMA_ERRXTRACE("onFrameError, streamId="<<hdr.getStreamId()<<", type="<<hdr.getType()<<", err="<<err);
}

void H2ConnectionImpl::addStream(H2StreamPtr stream)
{
    streams_[stream->getStreamId()] = stream;
}

H2StreamPtr H2ConnectionImpl::getStream(uint32_t streamId)
{
    auto it = streams_.find(streamId);
    if (it != streams_.end()) {
        return it->second;
    }
    return H2StreamPtr();
}

void H2ConnectionImpl::removeStream(uint32_t streamId)
{
    streams_.erase(streamId);
}

void H2ConnectionImpl::remove()
{
    
}

std::string H2ConnectionImpl::buildRequest()
{
    SettingsFrame settings;
    ParamVector params;
    params.push_back(std::make_pair(HEADER_TABLE_SIZE, 4096));
    uint8_t buf[6];
    settings.encodePayload(buf, sizeof(buf), params);
    
    uint8_t x64_encode_buf[sizeof(buf) * 3 / 2] = {0};
    uint32_t x64_encode_len = x64_encode(buf, sizeof(buf), x64_encode_buf, sizeof(x64_encode_buf), false);
    std::string settings_str((char*)x64_encode_buf, x64_encode_len);
  
    std::stringstream ss;
    ss << "GET / HTTP/1.1\r\n";
    ss << "Host: " << host_ << "\r\n";
    ss << "Connection: Upgrade, HTTP2-Settings\r\n";
    ss << "Upgrade: h2c\r\n";
    ss << "HTTP2-Settings: " << settings_str << "\r\n";
    ss << "\r\n";
    return ss.str();
}

std::string H2ConnectionImpl::buildResponse()
{
    std::stringstream ss;
    ss << "HTTP/1.1 101 Switching Protocols\r\n";
    ss << "Connection: Upgrade\r\n";
    ss << "Upgrade: "<< httpParser_.getParamValue("Upgrade") <<"\r\n";
    ss << "\r\n";
    return ss.str();
}

void H2ConnectionImpl::onConnect(int err)
{
    if(err != KUMA_ERROR_NOERR) {
        cleanup();
        setState(State::ERROR);
        if (cb_) cb_(err);
        return ;
    }
    if (tcp_.SslEnabled()) {
        onStateOpen();
        return ;
    }
    nextStreamId_ += 2;
    std::string str(buildRequest());
    send_buffer_.clear();
    send_offset_ = 0;
    send_buffer_.insert(send_buffer_.end(), str.begin(), str.end());
    setState(State::HANDSHAKE);
    int ret = tcp_.send(&send_buffer_[0], (uint32_t)send_buffer_.size());
    if(ret < 0) {
        cleanup();
        setState(State::CLOSED);
        return;
    } else {
        send_offset_ += ret;
        if(send_offset_ == send_buffer_.size()) {
            send_offset_ = 0;
            send_buffer_.clear();
        }
    }
}

void H2ConnectionImpl::onSend(int err)
{
    if(!send_buffer_.empty() && send_offset_ < send_buffer_.size()) {
        int ret = tcp_.send(&send_buffer_[0] + send_offset_, (uint32_t)send_buffer_.size() - send_offset_);
        if(ret < 0) {
            cleanup();
            setState(State::CLOSED);
            return;
        } else {
            send_offset_ += ret;
            if(send_offset_ == send_buffer_.size()) {
                send_offset_ = 0;
                send_buffer_.clear();
                if(isServer_ && getState() == State::HANDSHAKE) {
                    onStateOpen(); // response is sent out
                }
            }
        }
    }
}

void H2ConnectionImpl::onReceive(int err)
{
    if(initData_ && initSize_ > 0) {
        uint8_t *buf = initData_;
        size_t len = initSize_;
        initData_ = nullptr;
        initSize_ = 0;
        int ret = handleInputData(buf, len);
        delete [] buf;
        if (ret != KUMA_ERROR_NOERR) {
            return;
        }
    }
    uint8_t buf[128*1024];
    do {
        int ret = tcp_.receive(buf, sizeof(buf));
        if (ret > 0) {
            if (handleInputData(buf, ret) != KUMA_ERROR_NOERR) {
                break;
            }
        } else if(ret < 0) {
            cleanup();
            setState(State::CLOSED);
        } else if (0 == ret) {
            break;
        }
    } while(true);
}

void H2ConnectionImpl::onClose(int err)
{
    KUMA_INFOXTRACE("onClose");
    cleanup();
}

void H2ConnectionImpl::onHttpData(const char* data, size_t len)
{
    KUMA_ERRXTRACE("onHttpData, len="<<len);
}

void H2ConnectionImpl::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOXTRACE("onHttpEvent, ev="<<ev);
    switch (ev) {
        case HTTP_HEADER_COMPLETE:
            if(101 != httpParser_.getStatusCode() ||
               !is_equal(httpParser_.getHeaderValue("Connection"), "Upgrade")) {
                KUMA_ERRXTRACE("onHttpEvent, not HTTP2 upgrade response");
            }
            break;
            
        case HTTP_COMPLETE:
            if(httpParser_.isRequest()) {
                handleRequest();
            } else {
                handleResponse();
            }
            break;
            
        case HTTP_ERROR:
            setState(State::ERROR);
            break;
    }
}

int H2ConnectionImpl::handleRequest()
{
    std::string upgrade = httpParser_.getHeaderValue("Upgrade");
    std::stringstream ss(upgrade);
    std::string item;
    bool hasHTTP2 = false;
    while (getline(ss, item, ';')) {
        trim_left(item);
        trim_right(item);
        if (item == "h2c" || item == "h2") {
            hasHTTP2 = true;
            break;
        }
    }
    if(!is_equal(httpParser_.getHeaderValue("Connection"), "Upgrade") ||
       !hasHTTP2) {
        setState(State::ERROR);
        KUMA_ERRXTRACE("handleRequest, not HTTP2 request");
        return KUMA_ERROR_INVALID_PROTO;
    }
    
    std::string str(buildResponse());
    send_buffer_.clear();
    send_offset_ = 0;
    send_buffer_.insert(send_buffer_.end(), str.begin(), str.end());
    int ret = tcp_.send(&send_buffer_[0], (uint32_t)send_buffer_.size());
    if(ret < 0) {
        cleanup();
        setState(State::CLOSED);
        return KUMA_ERROR_SOCKERR;
    } else {
        send_offset_ += ret;
        if(send_offset_ == send_buffer_.size()) {
            send_offset_ = 0;
            send_buffer_.clear();
            onStateOpen();
        }
    }
    
    return KUMA_ERROR_NOERR;
}

int H2ConnectionImpl::handleResponse()
{
    if(101 == httpParser_.getStatusCode() &&
       is_equal(httpParser_.getHeaderValue("Connection"), "Upgrade")) {
        onStateOpen();
        return KUMA_ERROR_NOERR;
    } else {
        setState(State::ERROR);
        KUMA_INFOXTRACE("handleResponse, invalid status code: "<<httpParser_.getStatusCode());
        return KUMA_ERROR_INVALID_PROTO;
    }
}

void H2ConnectionImpl::onStateOpen()
{
    KUMA_INFOXTRACE("onStateOpen");
    setState(State::OPEN);
    if (!isServer_ && cb_) {
        cb_(0);
    }
}
