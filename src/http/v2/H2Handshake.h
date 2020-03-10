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

#pragma once

#include "kmdefs.h"
#include "h2defs.h"
#include "http/HttpParserImpl.h"
#include "FrameParser.h"

KUMA_NS_BEGIN

class H2Handshake : public kev::DestroyDetector, public FrameCallback
{
public:
    using HandshakeSender = std::function<KMError(KMBuffer &)>;
    using HandshakeCallback = std::function<void(SettingsFrame *)>;
    using ErrorCallback = std::function<void(KMError err)>;
    
    H2Handshake();
    void setHost(std::string host) { host_ = std::move(host); }
    void setLocalWindowSize(uint32_t win_size) { local_window_size_ = win_size; }
    void setHttpParser(HttpParser::Impl&& parser);
    KMError start(bool is_server, bool is_ssl);
    size_t parseInputData(uint8_t *buf, size_t len);
    
    void setHandshakeSender(HandshakeSender sender) { sender_ = std::move(sender); }
    void setHandshakeCallback(HandshakeCallback cb) { handshake_cb_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { error_cb_ = std::move(cb); }
    
protected:
    bool onFrame(H2Frame *frame) override;
    void onFrameError(const FrameHeader &hdr, H2Error err, bool stream_err) override;
    
protected:
    std::string buildUpgradeRequest();
    std::string buildUpgradeResponse();
    KMBuffer buildPreface();
    KMError sendUpgradeRequest();
    KMError sendUpgradeResponse();
    KMError sendPreface();
    KMError handleUpgradeRequest();
    KMError handleUpgradeResponse();
    
    bool isServer() const { return is_server_; }
    
    KMError sendHandshakeData(KMBuffer &buf);
    void onHandshakeComplete(SettingsFrame *frame);
    void onHandshakeError(KMError err);
    
    void onHttpData(KMBuffer &buf);
    void onHttpEvent(HttpEvent ev);
    
    enum State {
        UPGRADING,
        HANDSHAKE,
        OPEN,
        IN_ERROR,
        CLOSED
    };
    void setState(State state) { state_ = state; }
    State getState() const { return state_; }
    
    enum class ReadState {
        READ_HEADER,
        READ_PAYLOAD,
    };
    
protected:
    State state_ { State::UPGRADING };
    uint32_t max_local_frame_size_ = 65536;
    uint32_t init_local_window_size_ { H2_LOCAL_STREAM_INITIAL_WINDOW_SIZE };
    uint32_t max_concurrent_streams_ = 128;
    uint32_t local_window_size_ = 0;
    bool enable_connect_protocol_ = false;
    
    bool is_server_ = false;
    bool is_ssl_ = false;
    std::string cmp_preface_; // server only
    std::string host_;
    
    HttpParser::Impl http_parser_;
    FrameParser frame_parser_;
    
    ReadState read_state_ = ReadState::READ_HEADER;
    uint8_t hdr_buf_[H2_FRAME_HEADER_SIZE];
    uint8_t hdr_used_ = 0;
    
    HandshakeSender sender_;
    HandshakeCallback handshake_cb_;
    ErrorCallback error_cb_;
};

KUMA_NS_END
