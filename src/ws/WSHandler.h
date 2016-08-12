/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
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

class WSHandler : public DestroyDetector
{
public:
    typedef enum{
        WS_OPCODE_TEXT      = 1,
        WS_OPCODE_BINARY    = 2,
        WS_OPCODE_CLOSE     = 8,
        WS_OPCODE_PING      = 9,
        WS_OPCODE_PONG      = 10
    }WSOpcode;
    typedef enum{
        WS_ERROR_NOERR,
        WS_ERROR_NEED_MORE_DATA,
        WS_ERROR_HANDSHAKE,
        WS_ERROR_INVALID_PARAM,
        WS_ERROR_INVALID_STATE,
        WS_ERROR_INVALID_FRAME,
        WS_ERROR_INVALID_LENGTH,
        WS_ERROR_CLOSED,
        WS_ERROR_DESTROYED
    }WSError;
    typedef std::function<void(uint8_t*, size_t)> DataCallback;
    typedef std::function<void(int)> HandshakeCallback;
    
    WSHandler();
    ~WSHandler() = default;
    
    void setHttpParser(HttpParserImpl&& parser);
    std::string buildUpgradeRequest(const std::string& path, const std::string& host,
                             const std::string& proto, const std::string& origin);
    std::string buildUpgradeResponse();
    
    WSError handleData(uint8_t* data, size_t len);
    int encodeFrameHeader(WSOpcode opcode, size_t frame_len, uint8_t frame_hdr[10]);
    uint8_t getOpcode() { return opcode_; }
    
    const std::string getProtocol();
    const std::string getOrigin();
    
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }
    void setHandshakeCallback(HandshakeCallback cb) { handshake_cb_ = std::move(cb); }
    
private:
    typedef enum {
        FRAME_DECODE_STATE_HDR1,
        FRAME_DECODE_STATE_HDR2,
        FRAME_DECODE_STATE_HDREX,
        FRAME_DECODE_STATE_MASKEY,
        FRAME_DECODE_STATE_DATA,
        FRAME_DECODE_STATE_CLOSED,
        FRAME_DECODE_STATE_ERROR,
    }DecodeState;
    
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
        uint8_t maskey[4];
        uint32_t length;
    }FrameHeader;
    typedef struct DecodeContext{
        DecodeContext()
        : hdr()
        , state(FRAME_DECODE_STATE_HDR1)
        , pos(0)
        {
            
        }
        void reset()
        {
            memset(&hdr, 0, sizeof(hdr));
            state = FRAME_DECODE_STATE_HDR1;
            buf.clear();
            pos = 0;
        }
        FrameHeader hdr;
        DecodeState state;
        std::vector<uint8_t> buf;
        uint8_t pos;
    }DecodeContext;
    void cleanup();
    
    static WSError handleDataMask(FrameHeader& hdr, uint8_t* data, size_t len);
    WSError decodeFrame(uint8_t* data, size_t len);
    
    void onHttpData(const char* data, size_t len);
    void onHttpEvent(HttpEvent ev);
    
    void handleRequest();
    void handleResponse();
    
private:
    typedef enum {
        STATE_HANDSHAKE,
        STATE_OPEN,
        STATE_ERROR,
        STATE_DESTROY
    }State;
    State                   state_{ STATE_HANDSHAKE };
    DecodeContext           ctx_;
    uint8_t                 opcode_{ WS_OPCODE_BINARY };
    
    HttpParserImpl          http_parser_;
    
    DataCallback            data_cb_;
    HandshakeCallback       handshake_cb_;
};

KUMA_NS_END

#endif
