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
static const std::string ClientConnectionPreface("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");
KUMA_NS_END

//////////////////////////////////////////////////////////////////////////
H2ConnectionImpl::H2ConnectionImpl(EventLoopImpl* loop)
: loop_(loop), frameParser_(this), tcp_(loop)
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
    cb_connect_ = std::move(cb);
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
    
    int ret = tcp_.attachFd(fd);
    if (ret == KUMA_ERROR_NOERR) {
        if (tcp_.SslEnabled()) {
            sendPreface();
        } else {
            setState(State::HANDSHAKE);
        }
    }
    return ret;
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
        return KUMA_ERROR_INVALID_PROTO;
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
    
    return handleUpgradeRequest();
}

int H2ConnectionImpl::close()
{
    cleanup();
    return KUMA_ERROR_NOERR;
}

int H2ConnectionImpl::sendH2Frame(H2StreamPtr &stream, H2Frame *frame)
{
    if (!send_buffer_.empty()) {
        return KUMA_ERROR_AGAIN;
    }
    
    if (frame->type() == H2FrameType::HEADERS) {
        HeadersFrame *headers = dynamic_cast<HeadersFrame*>(frame);
        return sendHeadersFrame(stream, headers);
    }
    
    size_t payloadSize = frame->calcPayloadSize();
    size_t frameSize = payloadSize + H2_FRAME_HEADER_SIZE;
    send_buffer_.resize(frameSize);
    int ret = frame->encode(&send_buffer_[0], send_buffer_.size());
    if (ret < 0) {
        return KUMA_ERROR_INVALID_PARAM;
    }
    send_offset_ = 0;
    onSend(0);
    return KUMA_ERROR_NOERR;
}

int H2ConnectionImpl::sendHeadersFrame(H2StreamPtr &stream, HeadersFrame *frame)
{
    h2_priority_t pri;
    frame->setPriority(pri);
    size_t len1 = H2_FRAME_HEADER_SIZE + (frame->hasPriority()?H2_PRIORITY_PAYLOAD_SIZE:0);
    auto &headers = frame->getHeaders();
    size_t hdrSize = frame->getHeadersSize();
    send_buffer_.resize(len1 + hdrSize * 1.5);
    int ret = hpEncoder_.encode(headers, &send_buffer_[0] + len1, send_buffer_.size() - len1);
    if (ret < 0) {
        return KUMA_ERROR_FAILED;
    }
    size_t bsize = ret;
    ret = frame->encode(&send_buffer_[0], len1, bsize);
    KUMA_ASSERT(ret == len1);
    size_t total_len = len1 + bsize;
    send_buffer_.resize(total_len);
    send_offset_ = 0;
    onSend(0);
    return KUMA_ERROR_NOERR;
}

H2StreamPtr H2ConnectionImpl::createStream() {
    H2StreamPtr stream(new H2Stream(nextStreamId_));
    nextStreamId_ += 2;
    addStream(stream);
    return stream;
}

void H2ConnectionImpl::handleDataFrame(DataFrame *frame)
{
    KUMA_INFOXTRACE("handleDataFrame, streamId="<<frame->getStreamId()<<", size="<<frame->size()<<", flags="<<int(frame->getFlags()));
    H2StreamPtr stream = getStream(frame->getStreamId());
}

void H2ConnectionImpl::handleHeadersFrame(HeadersFrame *frame)
{
    KUMA_INFOXTRACE("handleHeadersFrame, streamId="<<frame->getStreamId()<<", flags="<<int(frame->getFlags()));
    H2StreamPtr stream = getStream(frame->getStreamId());
    
    HeaderVector headers;
    if (hpDecoder_.decode(frame->getBlock(), frame->getBlockSize(), headers) < 0) {
        KUMA_ERRXTRACE("handleHeadersFrame, hpack decode failed");
        return;
    }
}

void H2ConnectionImpl::handlePriorityFrame(PriorityFrame *frame)
{
    KUMA_INFOXTRACE("handlePriorityFrame, streamId="<<frame->getStreamId()<<", dep="<<frame->getPriority().streamId<<", weight="<<frame->getPriority().weight);
}

void H2ConnectionImpl::handleRSTStreamFrame(RSTStreamFrame *frame)
{
    KUMA_INFOXTRACE("handleRSTStreamFrame, streamId="<<frame->getStreamId());
}

void H2ConnectionImpl::handleSettingsFrame(SettingsFrame *frame)
{
    KUMA_INFOXTRACE("handleSettingsFrame, count="<<frame->getParams().size()<<", flags="<<int(frame->getFlags()));
    if (frame->getStreamId() == 0) {
        if (getState() < State::SENDING_PREFACE) {
            sendSettingAck = true;
        } else {
            SettingsFrame settings;
        }
    }
}

void H2ConnectionImpl::handlePushFrame(PushPromiseFrame *frame)
{
    KUMA_INFOXTRACE("handlePushFrame, streamId="<<frame->getStreamId()<<", flags="<<int(frame->getFlags()));
}

void H2ConnectionImpl::handlePingFrame(PingFrame *frame)
{
    KUMA_INFOXTRACE("handlePingFrame, streamId="<<frame->getStreamId());
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
    KUMA_INFOXTRACE("handleContinuationFrame, streamId="<<frame->getStreamId()<<", flags="<<int(frame->getFlags()));
}

int H2ConnectionImpl::handleInputData(const uint8_t *buf, size_t len)
{
    if (getState() == State::HANDSHAKE) {
        httpParser_.parse((char*)buf, (uint32_t)len);
    } else if (getState() == State::OPEN) {
        bool destroyed = false;
        KUMA_ASSERT(nullptr == destroy_flag_ptr_);
        destroy_flag_ptr_ = &destroyed;
        auto parseState = frameParser_.parseInputData(buf, len);
        if(destroyed) {
            return KUMA_ERROR_DESTROYED;
        }
        destroy_flag_ptr_ = nullptr;
        if(getState() == State::ERROR || getState() == State::CLOSED) {
            return KUMA_ERROR_INVALID_STATE;
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

std::string H2ConnectionImpl::buildUpgradeRequest()
{
    ParamVector params;
    params.emplace_back(std::make_pair(INITIAL_WINDOW_SIZE, 2147483647));
    params.emplace_back(std::make_pair(MAX_FRAME_SIZE, 65536));
    uint8_t buf[2 * H2_SETTING_ITEM_SIZE];
    SettingsFrame settings;
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

std::string H2ConnectionImpl::buildUpgradeResponse()
{
    std::stringstream ss;
    ss << "HTTP/1.1 101 Switching Protocols\r\n";
    ss << "Connection: Upgrade\r\n";
    ss << "Upgrade: "<< httpParser_.getParamValue("Upgrade") <<"\r\n";
    ss << "\r\n";
    return ss.str();
}

void H2ConnectionImpl::sendUpgradeRequest()
{
    std::string str(buildUpgradeRequest());
    send_buffer_.clear();
    send_offset_ = 0;
    send_buffer_.insert(send_buffer_.end(), str.begin(), str.end());
    setState(State::HANDSHAKE);
    onSend(0);
}

void H2ConnectionImpl::sendUpgradeResponse()
{
    std::string str(buildUpgradeResponse());
    send_buffer_.clear();
    send_offset_ = 0;
    send_buffer_.insert(send_buffer_.end(), str.begin(), str.end());
    setState(State::HANDSHAKE);
    onSend(0);
}

void H2ConnectionImpl::sendPreface()
{
    setState(State::SENDING_PREFACE);
    ParamVector params;
    params.emplace_back(std::make_pair(INITIAL_WINDOW_SIZE, 2147483647));
    params.emplace_back(std::make_pair(MAX_FRAME_SIZE, 65536));
    size_t setting_size = H2_FRAME_HEADER_SIZE + params.size() * H2_SETTING_ITEM_SIZE;
    size_t encoded_len = 0;
    if (!isServer_) {
        size_t total_len = ClientConnectionPreface.size() + setting_size + H2_WINDOW_UPDATE_FRAME_SIZE;
        send_buffer_.resize(total_len);
        memcpy(&send_buffer_[0], ClientConnectionPreface.c_str(), ClientConnectionPreface.size());
        encoded_len += ClientConnectionPreface.size();
    } else {
        params.emplace_back(std::make_pair(MAX_CONCURRENT_STREAMS, 128));
        setting_size += H2_SETTING_ITEM_SIZE;
        send_buffer_.resize(setting_size);
    }
    SettingsFrame settings;
    settings.setStreamId(0);
    settings.setParams(std::move(params));
    settings.setAck(sendSettingAck);
    int ret = settings.encode(&send_buffer_[0] + encoded_len, send_buffer_.size() - encoded_len);
    if (ret < 0) {
        KUMA_ERRXTRACE("sendPreface, failed to encode setting frame");
        return;
    }
    encoded_len += ret;
    sendSettingAck = false;
    WindowUpdateFrame win_update;
    win_update.setStreamId(0);
    win_update.setWindowSizeIncrement(2147418112);
    win_update.encode(&send_buffer_[0] + encoded_len, send_buffer_.size() - encoded_len);
    send_offset_ = 0;
    onSend(0);
}

void H2ConnectionImpl::onConnect(int err)
{
    if(err != KUMA_ERROR_NOERR) {
        cleanup();
        setState(State::ERROR);
        if (cb_connect_) cb_connect_(err);
        return ;
    }
    if (tcp_.SslEnabled()) {
        sendPreface();
        return ;
    }
    nextStreamId_ += 2; // stream id 1 is for upgrade request
    sendUpgradeRequest();
}

void H2ConnectionImpl::onSend(int err)
{
    if(!send_buffer_.empty() && send_offset_ < send_buffer_.size()) {
        int ret = tcp_.send(&send_buffer_[0] + send_offset_, send_buffer_.size() - send_offset_);
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
                    // response is sent out
                    sendPreface();
                } else if (getState() == State::SENDING_PREFACE) {
                    onStateOpen();
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
        } else if (0 == ret) {
            break;
        } else { // ret < 0
            cleanup();
            setState(State::CLOSED);
            return;
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
                handleUpgradeRequest();
            } else {
                handleUpgradeResponse();
            }
            break;
            
        case HTTP_ERROR:
            setState(State::ERROR);
            break;
    }
}

int H2ConnectionImpl::handleUpgradeRequest()
{
    std::string upgrade = httpParser_.getHeaderValue("Upgrade");
    std::stringstream ss(upgrade);
    std::string item;
    bool hasHTTP2 = false;
    while (getline(ss, item, ';')) {
        trim_left(item);
        trim_right(item);
        if (item == "h2c") {
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
    sendUpgradeResponse();
    
    return KUMA_ERROR_NOERR;
}

int H2ConnectionImpl::handleUpgradeResponse()
{
    if(101 == httpParser_.getStatusCode() &&
       is_equal(httpParser_.getHeaderValue("Connection"), "Upgrade")) {
        sendPreface();
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
    if (!isServer_ && cb_connect_) {
        cb_connect_(0);
    }
}
