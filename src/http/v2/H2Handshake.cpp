/* Copyright © 2014-2019, Fengping Bao <jamol@live.com>
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

#include "H2Handshake.h"
#include "H2Frame.h"
#include "util/base64.h"

#include <algorithm>

using namespace kuma;

namespace {
    static const std::string ClientConnectionPreface("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");
}


H2Handshake::H2Handshake()
: frame_parser_(this)
{
    http_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    frame_parser_.setMaxFrameSize(max_local_frame_size_);
}

void H2Handshake::setHttpParser(HttpParser::Impl&& parser)
{
    http_parser_ = std::move(parser);
    http_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
}

KMError H2Handshake::start(bool is_server, bool is_ssl)
{
    is_server_ = is_server;
    is_ssl_ = is_ssl;
    if (!isServer()) {
        if (!is_ssl) {
            setState(State::UPGRADING);
            return sendUpgradeRequest();
        } else {
            setState(State::HANDSHAKE);
            return sendPreface();
        }
    } else {
        enable_connect_protocol_ = true;
        cmp_preface_ = ClientConnectionPreface;
        if (!is_ssl) {
            // waiting upgrading request
            setState(State::UPGRADING);
            if (http_parser_.paused()) {
                http_parser_.resume();
                if (http_parser_.headerComplete()) {
                    onHttpEvent(HttpEvent::HEADER_COMPLETE);
                }
                if (http_parser_.complete()) {
                    onHttpEvent(HttpEvent::COMPLETE);
                }
            }
            return KMError::NOERR;
        } else {
            // waiting settings frame
            setState(State::HANDSHAKE);
            return sendPreface();
        }
    }
}

size_t H2Handshake::parseInputData(uint8_t *buf, size_t len)
{
    size_t sz = len;
    if (getState() == State::UPGRADING) {
        DESTROY_DETECTOR_SETUP();
        int ret = http_parser_.parse((char*)buf, (uint32_t)sz);
        DESTROY_DETECTOR_CHECK(ret);
        if (getState() == State::IN_ERROR || getState() == State::CLOSED) {
            return ret;
        }
        if (static_cast<size_t>(ret) >= sz) {
            return ret;
        }
        // residual data, should be preface
        sz -= ret;
        buf += ret;
    }
    if (getState() == State::HANDSHAKE) {
        if (isServer() && !cmp_preface_.empty()) {
            size_t cmp_size = std::min<size_t>(cmp_preface_.size(), sz);
            sz -= cmp_size;
            if (memcmp(cmp_preface_.c_str(), buf, cmp_size) != 0) {
                KUMA_ERRTRACE("H2Handshake::parseInputData, invalid protocol");
                setState(State::CLOSED);
                return len - sz;
            }
            cmp_preface_ = cmp_preface_.substr(cmp_size);
            if (!cmp_preface_.empty()) {
                return len - sz; // need more data
            }
            buf += cmp_size;
        }
        // expect a SETTINGS frame
        size_t used = 0;
        frame_parser_.parseOneFrame(buf, sz, used);
        sz -= used;
        return len - sz;
    } else {
        KUMA_WARNTRACE("H2Handshake::parseInputData, invalid state, len="<<sz<<", state="<<getState());
        return 0;
    }
}

std::string H2Handshake::buildUpgradeRequest()
{
    ParamVector params;
    params.emplace_back(std::make_pair(INITIAL_WINDOW_SIZE, init_local_window_size_));
    params.emplace_back(std::make_pair(MAX_FRAME_SIZE, max_local_frame_size_));
    uint8_t buf[2 * H2_SETTING_ITEM_SIZE];
    SettingsFrame settings;
    settings.encodePayload(buf, sizeof(buf), params);
    
    uint8_t x64_encode_buf[sizeof(buf) * 3 / 2] = {0};
    auto x64_encode_len = x64_encode(buf, sizeof(buf), x64_encode_buf, sizeof(x64_encode_buf), false);
    std::string settings_str((char*)x64_encode_buf, x64_encode_len);
    
    std::stringstream ss;
    ss << "GET / HTTP/1.1\r\n";
    if (!host_.empty()) {
        ss << "Host: " << host_ << "\r\n";
    }
    ss << "Connection: Upgrade, HTTP2-Settings\r\n";
    ss << "Upgrade: h2c\r\n";
    ss << "HTTP2-Settings: " << settings_str << "\r\n";
    ss << "\r\n";
    return ss.str();
}

std::string H2Handshake::buildUpgradeResponse()
{
    std::stringstream ss;
    ss << "HTTP/1.1 101 Switching Protocols\r\n";
    ss << "Connection: Upgrade\r\n";
    ss << "Upgrade: "<< http_parser_.getHeaderValue("Upgrade") <<"\r\n";
    ss << "\r\n";
    return ss.str();
}

KMError H2Handshake::sendUpgradeRequest()
{
    std::string str(buildUpgradeRequest());
    KMBuffer buf(str.c_str(), str.size(), str.size());
    return sendHandshakeData(buf);
}

KMError H2Handshake::sendUpgradeResponse()
{
    std::string str(buildUpgradeResponse());
    KMBuffer buf1(str.c_str(), str.size(), str.size());
    KMBuffer buf2 = buildPreface();
    buf1.append(&buf2);
    auto ret = sendHandshakeData(buf1);
    buf2.unlink();
    return ret;
}

KMError H2Handshake::sendPreface()
{
    KMBuffer buf = buildPreface();
    return sendHandshakeData(buf);
}

KMBuffer H2Handshake::buildPreface()
{
    ParamVector params;
    params.emplace_back(std::make_pair(INITIAL_WINDOW_SIZE, init_local_window_size_));
    params.emplace_back(std::make_pair(MAX_FRAME_SIZE, max_local_frame_size_));
    size_t setting_size = H2_FRAME_HEADER_SIZE + params.size() * H2_SETTING_ITEM_SIZE;
    KMBuffer buf;
    if (!isServer()) {
        size_t total_len = ClientConnectionPreface.size() + setting_size + H2_WINDOW_UPDATE_FRAME_SIZE;
        buf.allocBuffer(total_len);
        buf.write(ClientConnectionPreface.c_str(), ClientConnectionPreface.size());
    } else {
        params.emplace_back(std::make_pair(MAX_CONCURRENT_STREAMS, max_concurrent_streams_));
        setting_size += H2_SETTING_ITEM_SIZE;
        params.emplace_back(std::make_pair(ENABLE_CONNECT_PROTOCOL, enable_connect_protocol_?1:0));
        setting_size += H2_SETTING_ITEM_SIZE;
        size_t total_len = setting_size + H2_WINDOW_UPDATE_FRAME_SIZE;
        buf.allocBuffer(total_len);
    }
    SettingsFrame settings;
    settings.setStreamId(0);
    settings.setParams(std::move(params));
    int ret = settings.encode((uint8_t*)buf.writePtr(), buf.space());
    if (ret < 0) {
        KUMA_ERRTRACE("H2Handshake::buildPreface, failed to encode setting frame");
        return KMBuffer();
    }
    buf.bytesWritten(ret);
    WindowUpdateFrame win_update;
    win_update.setStreamId(0);
    win_update.setWindowSizeIncrement(local_window_size_);
    ret = win_update.encode((uint8_t*)buf.writePtr(), buf.space());
    if (ret < 0) {
        KUMA_ERRTRACE("H2Handshake::buildPreface, failed to window update frame");
        return KMBuffer();
    }
    buf.bytesWritten(ret);
    return buf;
}

KMError H2Handshake::handleUpgradeRequest()
{
    if(!http_parser_.isUpgradeTo("h2c")) {
        setState(State::IN_ERROR);
        KUMA_ERRTRACE("H2Handshake::handleRequest, not HTTP2 request");
        onHandshakeError(KMError::INVALID_PROTO);
        return KMError::INVALID_PROTO;
    }
    setState(State::HANDSHAKE);
    sendUpgradeResponse();
    
    return KMError::NOERR;
}

KMError H2Handshake::handleUpgradeResponse()
{
    if(http_parser_.isUpgradeTo("h2c")) {
        setState(State::HANDSHAKE);
        auto buf = buildPreface();
        sendHandshakeData(buf);
        return KMError::NOERR;
    } else {
        setState(State::IN_ERROR);
        KUMA_INFOTRACE("H2Handshake::handleResponse, invalid status code: "<<http_parser_.getStatusCode());
        onHandshakeError(KMError::INVALID_PROTO);
        return KMError::INVALID_PROTO;
    }
}

KMError H2Handshake::sendHandshakeData(KMBuffer &buf)
{
    if (sender_) {
        return sender_(buf);
    } else {
        return KMError::FAILED;
    }
}

void H2Handshake::onHandshakeComplete(SettingsFrame *frame)
{
    if (handshake_cb_) handshake_cb_(frame);
}

void H2Handshake::onHandshakeError(KMError err)
{
    if (error_cb_) error_cb_(err);
}

void H2Handshake::onHttpData(KMBuffer &buf)
{
    KUMA_ERRTRACE("H2Handshake::onHttpData, len="<<buf.chainLength());
}

void H2Handshake::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOTRACE("H2Handshake::onHttpEvent, ev="<<int(ev));
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
            break;
            
        case HttpEvent::COMPLETE:
            if(http_parser_.isRequest()) {
                handleUpgradeRequest();
            } else {
                handleUpgradeResponse();
            }
            break;
            
        case HttpEvent::HTTP_ERROR:
            setState(State::IN_ERROR);
            onHandshakeError(KMError::PROTO_ERROR);
            break;
    }
}

bool H2Handshake::onFrame(H2Frame *frame)
{
    if (frame->type() != H2FrameType::SETTINGS) {
        onHandshakeError(KMError::PROTO_ERROR);
        return false;
    }
    auto *settingsFrame = dynamic_cast<SettingsFrame*>(frame);
    if (!settingsFrame) {
        onHandshakeError(KMError::FAILED);
        return false;
    }
    setState(State::OPEN);
    onHandshakeComplete(settingsFrame);
    return true;
}

void H2Handshake::onFrameError(const FrameHeader &hdr, H2Error err, bool stream_err)
{
    setState(State::IN_ERROR);
    onHandshakeError(KMError::PROTO_ERROR);
}
