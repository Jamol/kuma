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

#include "kmdefs.h"
#include "http/HttpParserImpl.h"
#include "util/DestroyDetector.h"
#include <vector>

KUMA_NS_BEGIN

#define WS_MASK_KEY_SIZE    4
#define WS_MAX_HEADER_SIZE  14
class WSHandler : public DestroyDetector
{
public:
    typedef enum{
        WS_OPCODE_CONTINUE  = 0,
        WS_OPCODE_TEXT      = 1,
        WS_OPCODE_BINARY    = 2,
        WS_OPCODE_CLOSE     = 8,
        WS_OPCODE_PING      = 9,
        WS_OPCODE_PONG      = 10
    }WSOpcode;
    enum class WSError : int {
        NOERR,
        NEED_MORE_DATA,
        HANDSHAKE,
        INVALID_PARAM,
        INVALID_STATE,
        INVALID_FRAME,
        INVALID_LENGTH,
        PROTOCOL_ERROR,
        CLOSED,
        DESTROYED
    };
    enum class WSMode {
        CLIENT,
        SERVER
    };
    using FrameCallback = std::function<void(uint8_t/*opcode*/, bool/*fin*/, KMBuffer &buf)>;
    using HandshakeCallback = std::function<void(KMError)>;
    
    WSHandler();
    ~WSHandler() = default;
    
    void setMode(WSMode mode) { mode_ = mode; }
    WSMode getMode() { return mode_; }
    void setHttpParser(HttpParser::Impl&& parser);
    std::string buildUpgradeRequest(const std::string& path, const std::string& query, const std::string& host,
                             const std::string& proto, const std::string& origin);
    std::string buildUpgradeResponse();
    
    WSError handleData(uint8_t* data, size_t len);
    static int encodeFrameHeader(WSOpcode opcode, bool fin, uint8_t (*mask_key)[WS_MASK_KEY_SIZE], size_t plen, uint8_t hdr_buf[14]);
    
    const std::string getProtocol();
    const std::string getOrigin();
    
    void setFrameCallback(FrameCallback cb) { frame_cb_ = std::move(cb); }
    void setHandshakeCallback(HandshakeCallback cb) { handshake_cb_ = std::move(cb); }
    
    static void handleDataMask(const uint8_t mask_key[WS_MASK_KEY_SIZE], uint8_t* data, size_t len);
    static void handleDataMask(const uint8_t mask_key[WS_MASK_KEY_SIZE], KMBuffer &buf);
    static bool isControlFrame(uint8_t opcode) {
        return opcode == WS_OPCODE_PING || opcode == WS_OPCODE_PONG || opcode == WS_OPCODE_CLOSE;
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
    
    typedef struct FrameHeader {
        uint8_t fin:1;
        uint8_t rsv1:1;
        uint8_t rsv2:1;
        uint8_t rsv3:1;
        uint8_t opcode:4;
        uint8_t mask:1;
        uint8_t plen:7;
        union{
            uint16_t xpl16;
            uint64_t xpl64;
        }xpl;
        uint8_t maskey[WS_MASK_KEY_SIZE];
        uint32_t length = 0;
    }FrameHeader;
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
    }DecodeContext;
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
    }State;
    State                   state_{ STATE_HANDSHAKE };
    WSMode                  mode_ = WSMode::CLIENT;
    DecodeContext           ctx_;
    
    HttpParser::Impl        http_parser_;
    
    FrameCallback           frame_cb_;
    HandshakeCallback       handshake_cb_;
};

KUMA_NS_END

#endif
