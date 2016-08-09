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

#include "WSHandler.h"
#include "util/kmtrace.h"
#include "util/util.h"
#include "util/base64.h"

#include <sstream>
#ifdef KUMA_HAS_OPENSSL
#include <openssl/sha.h>
#else
#include "sha1/sha1.h"
#include "sha1/sha1.cpp"
#define SHA1 sha1::calc
#endif

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
WSHandler::WSHandler()
{
    http_parser_.setDataCallback([this] (const char* data, size_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
}

WSHandler::~WSHandler()
{
    
}

void WSHandler::cleanup()
{
    ctx_.reset();
}

void WSHandler::setHttpParser(HttpParserImpl&& parser)
{
    http_parser_.reset();
    http_parser_ = std::move(parser);
    http_parser_.setDataCallback([this] (const char* data, size_t len) { onHttpData(data, len); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    if(http_parser_.paused()) {
        http_parser_.resume();
    }
}

#define SHA1_DIGEST_SIZE	20
std::string generate_sec_accept_value(const std::string& sec_ws_key)
{
    static const std::string sec_accept_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    if(sec_ws_key.empty()) {
        return "";
    }
    std::string accept = sec_ws_key;
    accept += sec_accept_guid;
    
    uint8_t uShaRst2[SHA1_DIGEST_SIZE] = {0};
    SHA1((const uint8_t *)accept.c_str(), accept.size(), uShaRst2);
    
    uint8_t x64_encode_buf[32] = {0};
    uint32_t x64_encode_len = x64_encode(uShaRst2, SHA1_DIGEST_SIZE, x64_encode_buf, sizeof(x64_encode_buf), false);
    
    return std::string((char*)x64_encode_buf, x64_encode_len);
}

std::string WSHandler::buildUpgradeRequest(const std::string& path, const std::string& host,
                                    const std::string& proto, const std::string& origin)
{
    std::stringstream ss;
    ss << "GET ";
    ss << path;
    ss << " HTTP/1.1\r\n";
    ss << "Host: " << host << "\r\n";
    ss << "Upgrade: websocket\r\n";
    ss << "Connection: Upgrade\r\n";
    ss << "Origin: " << origin << "\r\n";
    ss << "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
    ss << "Sec-WebSocket-Protocol: " << proto << "\r\n";
    ss << "Sec-WebSocket-Version: 13\r\n";
    ss << "\r\n";
    return ss.str();
}

std::string WSHandler::buildUpgradeResponse()
{
    std::string sec_ws_key = http_parser_.getHeaderValue("Sec-WebSocket-Key");
    std::string protos = http_parser_.getHeaderValue("Sec-WebSocket-Protocol");
    
    std::stringstream ss;
    ss << "HTTP/1.1 101 Switching Protocols\r\n";
    ss << "Upgrade: websocket\r\n";
    ss << "Connection: Upgrade\r\n";
    ss << "Sec-WebSocket-Accept: " << generate_sec_accept_value(sec_ws_key) << "\r\n";
    if(!protos.empty()) {
        ss << "Sec-WebSocket-Protocol: " << protos << "\r\n";
    }
    ss << "\r\n";
    return ss.str();
}

const std::string WSHandler::getProtocol()
{
    return http_parser_.getHeaderValue("Sec-WebSocket-Protocol");
}

const std::string WSHandler::getOrigin()
{
    return http_parser_.getHeaderValue("Origin");
}

void WSHandler::onHttpData(const char* data, size_t len)
{
    KUMA_ERRTRACE("WSHandler::onHttpData, len="<<len);
}

void WSHandler::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOTRACE("WSHandler::onHttpEvent, ev="<<ev);
    switch (ev) {
        case HTTP_HEADER_COMPLETE:
            break;
            
        case HTTP_COMPLETE:
            if(http_parser_.isRequest()) {
                handleRequest();
            } else {
                handleResponse();
            }
            break;
            
        case HTTP_ERROR:
            state_ = STATE_ERROR;
            break;
            
        default:
            break;
    }
}

void WSHandler::handleRequest()
{
    if(!is_equal(http_parser_.getHeaderValue("Upgrade"), "WebSocket") ||
       !is_equal(http_parser_.getHeaderValue("Connection"), "Upgrade")) {
        state_ = STATE_ERROR;
        KUMA_INFOTRACE("WSHandler::handleRequest, not WebSocket request");
        if(handshake_cb_) handshake_cb_(KUMA_ERROR_INVALID_PROTO);
        return;
    }
    std::string sec_ws_key = http_parser_.getHeaderValue("Sec-WebSocket-Key");
    if(sec_ws_key.empty()) {
        state_ = STATE_ERROR;
        KUMA_INFOTRACE("WSHandler::handleRequest, no Sec-WebSocket-Key");
        if(handshake_cb_) handshake_cb_(KUMA_ERROR_INVALID_PROTO);
        return;
    }
    state_ = STATE_OPEN;
    if(handshake_cb_) handshake_cb_(0);
}

void WSHandler::handleResponse()
{
    if(101 == http_parser_.getStatusCode() &&
       is_equal(http_parser_.getHeaderValue("Upgrade"), "WebSocket") &&
       is_equal(http_parser_.getHeaderValue("Connection"), "Upgrade")) {
        state_ = STATE_OPEN;
        if(handshake_cb_) handshake_cb_(0);
    } else {
        state_ = STATE_ERROR;
        KUMA_INFOTRACE("WSHandler::handleResponse, invalid status code: "<<http_parser_.getStatusCode());
        if(handshake_cb_) handshake_cb_(-1);
    }
}

WSHandler::WSError WSHandler::handleData(uint8_t* data, size_t len)
{
    if(state_ == STATE_OPEN) {
        return decodeFrame(data, len);
    }
    if(state_ == STATE_HANDSHAKE) {
        DESTROY_DETECTOR_SETUP();
        int bytes_used = http_parser_.parse((char*)data, len);
        DESTROY_DETECTOR_CHECK(WS_ERROR_DESTROYED);
        if(state_ == STATE_ERROR) {
            return WS_ERROR_HANDSHAKE;
        }
        if(bytes_used < len && state_ == STATE_OPEN) {
            return decodeFrame(data + bytes_used, len - bytes_used);
        }
    } else {
        return WS_ERROR_INVALID_STATE;
    }
    return WS_ERROR_NOERR;
}

int WSHandler::encodeFrameHeader(WSOpcode opcode, size_t frame_len, uint8_t frame_hdr[10])
{
    uint8_t first_byte = 0x80 | opcode;
    uint8_t second_byte = 0x00;
    uint8_t hdr_len = 2;
    
    if(frame_len <= 125)
    {//0 byte
        second_byte = (uint8_t)frame_len;
    }
    else if(frame_len <= 0xFFFF)
    {//2 bytes
        hdr_len += 2;
        second_byte = 126;
    }
    else
    {//8 bytes
        hdr_len += 8;
        second_byte = 127;
    }
    
    frame_hdr[0] = first_byte;
    frame_hdr[1] = second_byte;
    if(126 == second_byte)
    {
        frame_hdr[2] = (unsigned char)(frame_len >> 8);
        frame_hdr[3] = (unsigned char)frame_len;
    }
    else if(127 == second_byte)
    {
        frame_hdr[2] = 0;
        frame_hdr[3] = 0;
        frame_hdr[4] = 0;
        frame_hdr[5] = 0;
        frame_hdr[6] = (unsigned char)(frame_len >> 24);
        frame_hdr[7] = (unsigned char)(frame_len >> 16);
        frame_hdr[8] = (unsigned char)(frame_len >> 8);
        frame_hdr[9] = (unsigned char)frame_len;
    }
    return hdr_len;
}

WSHandler::WSError WSHandler::decodeFrame(uint8_t* data, size_t len)
{
#define WS_MAX_FRAME_DATA_LENGTH	10*1024*1024
    
    size_t pos = 0;
    uint8_t b = 0;
    while(pos < len)
    {
        switch(ctx_.state)
        {
            case FRAME_DECODE_STATE_HDR1:
                b = data[pos++];
                ctx_.hdr.fin = b >> 7;
                ctx_.hdr.opcode = b & 0x0F;
                if(b & 0x70) { // reserved bits are not 0
                    ctx_.state = FRAME_DECODE_STATE_ERROR;
                    return WS_ERROR_INVALID_FRAME;
                }
                opcode_ = ctx_.hdr.opcode;
                ctx_.state = FRAME_DECODE_STATE_HDR2;
                break;
            case FRAME_DECODE_STATE_HDR2:
                b = data[pos++];
                ctx_.hdr.mask = b >> 7;
                ctx_.hdr.plen = b & 0x7F;
                ctx_.hdr.xpl.xpl64 = 0;
                ctx_.pos = 0;
                ctx_.buf.clear();
                ctx_.state = FRAME_DECODE_STATE_HDREX;
                break;
            case FRAME_DECODE_STATE_HDREX:
                if(126 == ctx_.hdr.plen) {
                    uint32_t expect_len = 2;
                    if(len-pos+ctx_.pos >= expect_len) {
                        for (; ctx_.pos<expect_len; ++pos, ++ctx_.pos) {
                            ctx_.hdr.xpl.xpl16 |= data[pos] << ((expect_len-ctx_.pos-1) << 3);
                        }
                        ctx_.pos = 0;
                        if(ctx_.hdr.xpl.xpl16 < 126)
                        {// invalid ex payload length
                            ctx_.state = FRAME_DECODE_STATE_ERROR;
                            return WS_ERROR_INVALID_LENGTH;
                        }
                        ctx_.hdr.length = ctx_.hdr.xpl.xpl16;
                        ctx_.state = FRAME_DECODE_STATE_MASKEY;
                    } else {
                        ctx_.hdr.xpl.xpl16 |= data[pos++]<<8;
                        ++ctx_.pos;
                        return WS_ERROR_NEED_MORE_DATA;
                    }
                } else if(127 == ctx_.hdr.plen) {
                    uint32_t expect_len = 8;
                    if(len-pos+ctx_.pos >= expect_len) {
                        for (; ctx_.pos<expect_len; ++pos, ++ctx_.pos) {
                            ctx_.hdr.xpl.xpl64 |= data[pos] << ((expect_len-ctx_.pos-1) << 3);
                        }
                        ctx_.pos = 0;
                        if((ctx_.hdr.xpl.xpl64>>63) != 0)
                        {// invalid ex payload length
                            ctx_.state = FRAME_DECODE_STATE_ERROR;
                            return WS_ERROR_INVALID_LENGTH;
                        }
                        ctx_.hdr.length = (uint32_t)ctx_.hdr.xpl.xpl64;
                        if(ctx_.hdr.length > WS_MAX_FRAME_DATA_LENGTH)
                        {// invalid ex payload length
                            ctx_.state = FRAME_DECODE_STATE_ERROR;
                            return WS_ERROR_INVALID_LENGTH;
                        }
                        ctx_.state = FRAME_DECODE_STATE_MASKEY;
                    } else {
                        for (; pos<len; ++pos, ++ctx_.pos) {
                            ctx_.hdr.xpl.xpl64 |= data[pos] << ((expect_len-ctx_.pos-1) << 3);
                        }
                        return WS_ERROR_NEED_MORE_DATA;
                    }
                } else {
                    ctx_.hdr.length = ctx_.hdr.plen;
                    ctx_.state = FRAME_DECODE_STATE_MASKEY;
                }
                break;
            case FRAME_DECODE_STATE_MASKEY:
                if(ctx_.hdr.mask) {
                    uint32_t expect_len = 4;
                    if(len-pos+ctx_.pos >= expect_len)
                    {
                        memcpy(ctx_.hdr.maskey+ctx_.pos, data+pos, expect_len-ctx_.pos);
                        pos += expect_len-ctx_.pos;
                        ctx_.pos = 0;
                    } else {
                        memcpy(ctx_.hdr.maskey+ctx_.pos, data+pos, len-pos);
                        ctx_.pos += len-pos;
                        pos = len;
                        return WS_ERROR_NEED_MORE_DATA;
                    }
                }
                ctx_.buf.clear();
                ctx_.state = FRAME_DECODE_STATE_DATA;
                if(WS_OPCODE_CLOSE == ctx_.hdr.opcode && 0 == ctx_.hdr.length) {
                    // connection closed
                    ctx_.state = FRAME_DECODE_STATE_CLOSED;
                    return WS_ERROR_CLOSED;
                }
                break;
            case FRAME_DECODE_STATE_DATA:
                if(WS_OPCODE_CLOSE == ctx_.hdr.opcode) {
                    // connection closed
                    ctx_.state = FRAME_DECODE_STATE_CLOSED;
                    return WS_ERROR_CLOSED;
                }
                if(ctx_.buf.empty() && len-pos >= ctx_.hdr.length) {
                    uint8_t* notify_data = data + pos;
                    uint32_t notify_len = ctx_.hdr.length;
                    pos += notify_len;
                    handleDataMask(ctx_.hdr, notify_data, notify_len);
                    DESTROY_DETECTOR_SETUP();
                    if(data_cb_) data_cb_(notify_data, notify_len);
                    DESTROY_DETECTOR_CHECK(WS_ERROR_DESTROYED);
                    ctx_.reset();
                } else if(len-pos+ctx_.buf.size() >= ctx_.hdr.length) {
                    auto buf_size = ctx_.buf.size();
                    auto read_len = ctx_.hdr.length - buf_size;
                    ctx_.buf.reserve(ctx_.hdr.length);
                    ctx_.buf.insert(ctx_.buf.end(), data + pos, data + pos + read_len);
                    uint8_t* notify_data = &ctx_.buf[0];
                    uint32_t notify_len = ctx_.hdr.length;
                    pos += read_len;
                    handleDataMask(ctx_.hdr, notify_data, notify_len);
                    DESTROY_DETECTOR_SETUP();
                    if(data_cb_) data_cb_(notify_data, notify_len);
                    DESTROY_DETECTOR_CHECK(WS_ERROR_DESTROYED);
                    ctx_.reset();
                } else {
                    auto buf_size = ctx_.buf.size();
                    auto read_len = len - pos;
                    ctx_.buf.reserve(buf_size + read_len);
                    ctx_.buf.insert(ctx_.buf.end(), data + pos, data + len);
                    return WS_ERROR_NEED_MORE_DATA;
                }
                break;
            default:
                return WS_ERROR_INVALID_FRAME;
        }
    }
    return ctx_.state == FRAME_DECODE_STATE_HDR1 ? WS_ERROR_NOERR : WS_ERROR_NEED_MORE_DATA;
}

WSHandler::WSError WSHandler::handleDataMask(FrameHeader& hdr, uint8_t* data, size_t len)
{
    if(0 == hdr.mask) return WS_ERROR_NOERR;
    if(nullptr == data || 0 == len) return WS_ERROR_INVALID_FRAME;
    
    for(size_t i=0; i < len; ++i) {
        data[i] = data[i] ^ hdr.maskey[i%4];
    }
    
    return WS_ERROR_NOERR;
}
