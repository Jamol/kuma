/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
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

#ifndef __WSHandler_H__
#define __WSHandler_H__

#include "wsdefs.h"
#include "http/HttpParserImpl.h"
#include "util/DestroyDetector.h"
#include <vector>

WS_NS_BEGIN

class WSHandler : public DestroyDetector
{
public:
    using FrameCallback = std::function<KMError(FrameHeader, KMBuffer &)>;
    using HandshakeCallback = std::function<void(KMError)>;
    using EnumerateCallback = HttpParser::Impl::EnumerateCallback;
    
    WSHandler();
    ~WSHandler() = default;
    
    void setMode(WSMode mode) { mode_ = mode; }
    WSMode getMode() { return mode_; }
    void setHttpParser(HttpParser::Impl&& parser);
    
    WSError handleData(uint8_t* data, size_t len);
    static int encodeFrameHeader(FrameHeader hdr, uint8_t hdr_buf[WS_MAX_HEADER_SIZE]);
    
    const std::string getOrigin() const;
    const std::string getSubprotocol() const;
    const std::string getExtensions() const;
    
    const std::string& getPath() const;
    const std::string& getQuery() const;
    const std::string& getParamValue(const std::string &name) const;
    const std::string& getHeaderValue(const std::string &name) const;
    void forEachHeader(const EnumerateCallback &cb) const;
    
    void setFrameCallback(FrameCallback cb) { frame_cb_ = std::move(cb); }
    void setHandshakeCallback(HandshakeCallback cb) { handshake_cb_ = std::move(cb); }
    
    void reset();
    
    static void handleDataMask(const uint8_t mask_key[WS_MASK_KEY_SIZE], uint8_t* data, size_t len);
    static void handleDataMask(const uint8_t mask_key[WS_MASK_KEY_SIZE], KMBuffer &buf);
    static bool isControlFrame(uint8_t opcode) {
        return opcode >= 8;
    }
    
private:
    enum class DecodeState {
        HDR1,
        HDR2,
        HDREX,
        MASKEY,
        DATA,
        CLOSED,
        IN_ERROR,
    };
    typedef struct DecodeContext{
        void reset()
        {
            memset(&hdr, 0, sizeof(hdr));
            state = DecodeState::HDR1;
            buf.clear();
            pos = 0;
        }
        FrameHeader hdr;
        DecodeState state{ DecodeState::HDR1 };
        std::vector<uint8_t> buf;
        uint8_t pos = 0;
    } DecodeContext;
    void cleanup();
    
    void handleDataMask(const FrameHeader& hdr, uint8_t* data, size_t len);
    void handleDataMask(const FrameHeader& hdr, KMBuffer &buf);
    WSError decodeFrame(uint8_t* data, size_t len);
    
    void onHttpData(KMBuffer &buf);
    void onHttpEvent(HttpEvent ev);
    
    void handleRequest();
    void handleResponse();
    WSError handleFrame(const FrameHeader &hdr, void* payload, size_t len);
    
private:
    typedef enum {
        STATE_HANDSHAKE,
        STATE_OPEN,
        STATE_ERROR,
        STATE_DESTROY
    } State;
    State                   state_{ STATE_HANDSHAKE };
    WSMode                  mode_ = WSMode::CLIENT;
    DecodeContext           ctx_;
    
    HttpParser::Impl        http_parser_;
    
    FrameCallback           frame_cb_;
    HandshakeCallback       handshake_cb_;
};

WS_NS_END

#endif
