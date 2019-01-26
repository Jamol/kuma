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

const std::string kSecWebSocketKey { "Sec-WebSocket-Key" };
const std::string kSecWebSocketAccept { "Sec-WebSocket-Accept" };
const std::string kSecWebSocketVersion { "Sec-WebSocket-Version" };
const std::string kSecWebSocketProtocol { "Sec-WebSocket-Protocol" };
const std::string kSecWebSocketExtensions { "Sec-WebSocket-Extensions" };

const std::string kWebSocketVersion { "13" };

static std::string generate_sec_accept_value(const std::string& sec_ws_key);

//////////////////////////////////////////////////////////////////////////
WSHandler::WSHandler()
{
    http_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
}

void WSHandler::cleanup()
{
    ctx_.reset();
}

void WSHandler::setHttpParser(HttpParser::Impl&& parser)
{
    http_parser_.reset();
    http_parser_ = std::move(parser);
    http_parser_.setDataCallback([this] (KMBuffer &buf) { onHttpData(buf); });
    http_parser_.setEventCallback([this] (HttpEvent ev) { onHttpEvent(ev); });
    if(http_parser_.paused()) {
        http_parser_.resume();
    }
}

std::string WSHandler::buildUpgradeRequest(const std::string& path,
                                           const std::string& query,
                                           const std::string& host,
                                           const std::string& origin,
                                           const std::string& subprotocol,
                                           const std::string& extensions)
{
    std::stringstream ss;
    ss << "GET ";
    ss << path;
    if(!query.empty()){
        ss << "?";
        ss << query;
    }
    ss << " HTTP/1.1\r\n";
    ss << "Host: " << host << "\r\n";
    ss << "Upgrade: websocket\r\n";
    ss << "Connection: Upgrade\r\n";
    if (!origin.empty()) {
        ss << "Origin: " << origin << "\r\n";
    }
    ss << kSecWebSocketKey << ": " << "dGhlIHNhbXBsZSBub25jZQ==" << "\r\n";
    if (!subprotocol.empty()) {
        ss << kSecWebSocketProtocol << ": " << subprotocol << "\r\n";
    }
    if (!extensions.empty()) {
        ss << kSecWebSocketExtensions << ": " << extensions << "\r\n";
    }
    ss << kSecWebSocketVersion << ": " << kWebSocketVersion << "\r\n";
    ss << "\r\n";
    return ss.str();
}

std::string WSHandler::buildUpgradeResponse(const std::string& subprotocol,
                                            const std::string& extensions)
{
    if (state_ == STATE_OPEN) {
        std::string sec_ws_key = http_parser_.getHeaderValue(kSecWebSocketKey);
        std::string client_protocols = http_parser_.getHeaderValue(kSecWebSocketProtocol);
        std::string client_extensions = http_parser_.getHeaderValue(kSecWebSocketExtensions);
        
        std::stringstream ss;
        ss << "HTTP/1.1 101 Switching Protocols\r\n";
        ss << "Upgrade: websocket\r\n";
        ss << "Connection: Upgrade\r\n";
        ss << kSecWebSocketAccept << ": " << generate_sec_accept_value(sec_ws_key) << "\r\n";
        if(!subprotocol.empty()) {
            ss << kSecWebSocketProtocol << ": " << subprotocol << "\r\n";
        }
        if (!extensions.empty()) {
            ss << kSecWebSocketExtensions << ": " << extensions << "\r\n";
        }
        ss << "\r\n";
        return ss.str();
    } else if (state_ == STATE_ERROR) {
        std::stringstream ss;
        ss << "HTTP/1.1 400 Bad Request\r\n";
        ss << kSecWebSocketVersion << ": " << kWebSocketVersion << "\r\n";
        ss << "\r\n";
        return ss.str();
    } else {
        return "";
    }
}

const std::string WSHandler::getSubprotocol()
{
    return http_parser_.getHeaderValue(kSecWebSocketProtocol);
}

const std::string WSHandler::getOrigin()
{
    return http_parser_.getHeaderValue("Origin");
}

void WSHandler::onHttpData(KMBuffer &buf)
{
    KUMA_ERRTRACE_THIS("WSHandler::onHttpData, len="<<buf.chainLength());
}

void WSHandler::onHttpEvent(HttpEvent ev)
{
    KUMA_INFOTRACE_THIS("WSHandler::onHttpEvent, ev="<<int(ev));
    switch (ev) {
        case HttpEvent::HEADER_COMPLETE:
            break;
            
        case HttpEvent::COMPLETE:
            if(http_parser_.isRequest()) {
                handleRequest();
            } else {
                handleResponse();
            }
            break;
            
        case HttpEvent::HTTP_ERROR:
            state_ = STATE_ERROR;
            break;
            
        default:
            break;
    }
}

void WSHandler::handleRequest()
{
    auto err = KMError::NOERR;
    do {
        if(!http_parser_.isUpgradeTo("WebSocket")) {
            KUMA_ERRTRACE_THIS("WSHandler::handleRequest, not WebSocket request");
            err = KMError::PROTO_ERROR;
            break;
        }
        auto const &sec_ws_ver = http_parser_.getHeaderValue(kSecWebSocketVersion);
        if (sec_ws_ver.empty() || !contains_token(sec_ws_ver, kWebSocketVersion, ',')) {
            KUMA_ERRTRACE_THIS("WSHandler::handleRequest, unsupported version number, ver="<<sec_ws_ver);
            err = KMError::PROTO_ERROR;
            break;
        }
        auto const &sec_ws_key = http_parser_.getHeaderValue(kSecWebSocketKey);
        if(sec_ws_key.empty()) {
            KUMA_ERRTRACE_THIS("WSHandler::handleRequest, no Sec-WebSocket-Key");
            err = KMError::PROTO_ERROR;
            break;
        }
    } while (0);
    
    state_ = err == KMError::NOERR ? STATE_OPEN : STATE_ERROR;
    if(handshake_cb_) handshake_cb_(err);
}

void WSHandler::handleResponse()
{
    auto err = KMError::NOERR;
    if(!http_parser_.isUpgradeTo("WebSocket")) {
        KUMA_ERRTRACE_THIS("WSHandler::handleResponse, invalid status code: "<<http_parser_.getStatusCode());
        err = KMError::PROTO_ERROR;
    }
    
    state_ = err == KMError::NOERR ? STATE_OPEN : STATE_ERROR;
    if(handshake_cb_) handshake_cb_(err);
}

WSHandler::WSError WSHandler::handleData(uint8_t* data, size_t len)
{
    if(state_ == STATE_OPEN) {
        return decodeFrame(data, len);
    }
    if(state_ == STATE_HANDSHAKE) {
        DESTROY_DETECTOR_SETUP();
        int bytes_used = http_parser_.parse((char*)data, len);
        DESTROY_DETECTOR_CHECK(WSError::DESTROYED);
        if(state_ == STATE_ERROR) {
            return WSError::HANDSHAKE;
        }
        if(bytes_used < (int)len && state_ == STATE_OPEN) {
            return decodeFrame(data + bytes_used, len - bytes_used);
        }
    } else {
        return WSError::INVALID_STATE;
    }
    return WSError::NOERR;
}

int WSHandler::encodeFrameHeader(WSOpcode opcode, bool fin, uint8_t (*mask_key)[WS_MASK_KEY_SIZE], size_t plen, uint8_t hdr_buf[14])
{
    uint8_t first_byte = fin ? 0x80 : 0x00;
    first_byte |= opcode;
    uint8_t second_byte = mask_key?0x80:0x00;
    uint8_t hdr_len = 2;
    
    if(plen <= 125)
    {//0 byte
        second_byte |= (uint8_t)plen;
    }
    else if(plen <= 0xFFFF)
    {//2 bytes
        hdr_len += 2;
        second_byte |= 126;
    }
    else
    {//8 bytes
        hdr_len += 8;
        second_byte |= 127;
    }
    
    hdr_buf[0] = first_byte;
    hdr_buf[1] = second_byte;
    if(126 == (second_byte & 0x7F))
    {
        hdr_buf[2] = (unsigned char)(plen >> 8);
        hdr_buf[3] = (unsigned char)plen;
    }
    else if(127 == (second_byte & 0x7F))
    {
        hdr_buf[2] = 0;
        hdr_buf[3] = 0;
        hdr_buf[4] = 0;
        hdr_buf[5] = 0;
        hdr_buf[6] = (unsigned char)(plen >> 24);
        hdr_buf[7] = (unsigned char)(plen >> 16);
        hdr_buf[8] = (unsigned char)(plen >> 8);
        hdr_buf[9] = (unsigned char)plen;
    }
    if (mask_key) {
        memcpy(hdr_buf + hdr_len, *mask_key, WS_MASK_KEY_SIZE);
        hdr_len += WS_MASK_KEY_SIZE;
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
            case DecodeState::HDR1:
            {
                b = data[pos++];
                ctx_.hdr.fin = b >> 7;
                ctx_.hdr.opcode = b & 0x0F;
                if(b & 0x70) { // reserved bits are not 0
                    ctx_.state = DecodeState::IN_ERROR;
                    return WSError::INVALID_FRAME;
                }
                if (!ctx_.hdr.fin && isControlFrame(ctx_.hdr.opcode)) {
                    // Control frames MUST NOT be fragmented
                    ctx_.state = DecodeState::IN_ERROR;
                    return WSError::PROTOCOL_ERROR;
                }
                // TODO: check interleaved fragments of different messages
                ctx_.state = DecodeState::HDR2;
                break;
            }
            case DecodeState::HDR2:
            {
                b = data[pos++];
                ctx_.hdr.mask = b >> 7;
                ctx_.hdr.plen = b & 0x7F;
                ctx_.hdr.xpl.xpl64 = 0;
                ctx_.pos = 0;
                ctx_.buf.clear();
                if (isControlFrame(ctx_.hdr.opcode) && ctx_.hdr.plen > 125) {
                    // the payload length of control frames MUST <= 125
                    ctx_.state = DecodeState::IN_ERROR;
                    return WSError::PROTOCOL_ERROR;
                }
                ctx_.state = DecodeState::HDREX;
                break;
            }
            case DecodeState::HDREX:
            {
                if(126 == ctx_.hdr.plen) {
                    uint32_t expect_len = 2;
                    if(len-pos+ctx_.pos >= expect_len) {
                        for (; ctx_.pos<expect_len; ++pos, ++ctx_.pos) {
                            ctx_.hdr.xpl.xpl16 |= data[pos] << ((expect_len-ctx_.pos-1) << 3);
                        }
                        ctx_.pos = 0;
                        if(ctx_.hdr.xpl.xpl16 < 126)
                        {// invalid ex payload length
                            ctx_.state = DecodeState::IN_ERROR;
                            return WSError::INVALID_LENGTH;
                        }
                        ctx_.hdr.length = ctx_.hdr.xpl.xpl16;
                        ctx_.state = DecodeState::MASKEY;
                    } else {
                        ctx_.hdr.xpl.xpl16 |= data[pos++]<<8;
                        ++ctx_.pos;
                        return WSError::NEED_MORE_DATA;
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
                            ctx_.state = DecodeState::IN_ERROR;
                            return WSError::INVALID_LENGTH;
                        }
                        ctx_.hdr.length = (uint32_t)ctx_.hdr.xpl.xpl64;
                        if(ctx_.hdr.length > WS_MAX_FRAME_DATA_LENGTH)
                        {// invalid ex payload length
                            ctx_.state = DecodeState::IN_ERROR;
                            return WSError::INVALID_LENGTH;
                        }
                        ctx_.state = DecodeState::MASKEY;
                    } else {
                        for (; pos<len; ++pos, ++ctx_.pos) {
                            ctx_.hdr.xpl.xpl64 |= data[pos] << ((expect_len-ctx_.pos-1) << 3);
                        }
                        return WSError::NEED_MORE_DATA;
                    }
                } else {
                    ctx_.hdr.length = ctx_.hdr.plen;
                    ctx_.state = DecodeState::MASKEY;
                }
                break;
            }
            case DecodeState::MASKEY:
            {
                if(ctx_.hdr.mask) {
                    if (WSMode::CLIENT == mode_) {
                        // server MUST NOT mask any frames
                        ctx_.state = DecodeState::IN_ERROR;
                        return WSError::PROTOCOL_ERROR;
                    }
                    uint32_t expect_len = 4;
                    if(len-pos+ctx_.pos >= expect_len)
                    {
                        memcpy(ctx_.hdr.maskey+ctx_.pos, data+pos, expect_len-ctx_.pos);
                        pos += expect_len-ctx_.pos;
                        ctx_.pos = 0;
                    } else {
                        memcpy(ctx_.hdr.maskey+ctx_.pos, data+pos, len-pos);
                        ctx_.pos += uint8_t(len-pos);
                        pos = len;
                        return WSError::NEED_MORE_DATA;
                    }
                } else if (WSMode::SERVER == mode_ && ctx_.hdr.length > 0) {
                    // client MUST mask all frames
                    ctx_.state = DecodeState::IN_ERROR;
                    return WSError::PROTOCOL_ERROR;
                }
                ctx_.buf.clear();
                ctx_.state = DecodeState::DATA;
                break;
            }
            case DecodeState::DATA:
            {
                if (len-pos+ctx_.buf.size() < ctx_.hdr.length) {
                    auto buf_size = ctx_.buf.size();
                    auto read_len = len - pos;
                    ctx_.buf.reserve(buf_size + read_len);
                    ctx_.buf.insert(ctx_.buf.end(), data + pos, data + len);
                    return WSError::NEED_MORE_DATA;
                }

                uint8_t* notify_data = nullptr;
                uint32_t notify_len = 0;
                if(ctx_.buf.empty()) {
                    notify_data = data + pos;
                    notify_len = ctx_.hdr.length;
                    pos += notify_len;
                } else {
                    auto buf_size = ctx_.buf.size();
                    auto read_len = ctx_.hdr.length - buf_size;
                    ctx_.buf.reserve(ctx_.hdr.length);
                    ctx_.buf.insert(ctx_.buf.end(), data + pos, data + pos + read_len);
                    notify_data = &ctx_.buf[0];
                    notify_len = ctx_.hdr.length;
                    pos += read_len;
                }
                handleDataMask(ctx_.hdr, notify_data, notify_len);
                auto err = handleFrame(ctx_.hdr, notify_data, notify_len);
                if (err != WSError::NOERR) {
                    return err;
                }
                if (WS_OPCODE_CLOSE == ctx_.hdr.opcode) {
                    ctx_.state = DecodeState::CLOSED;
                    return WSError::CLOSED;
                }
                ctx_.reset();
                break;
            }
            default:
                return WSError::INVALID_FRAME;
        }
    }
    return ctx_.state == DecodeState::HDR1 ? WSError::NOERR : WSError::NEED_MORE_DATA;
}

WSHandler::WSError WSHandler::handleFrame(const FrameHeader &hdr, void* payload, size_t len)
{
    DESTROY_DETECTOR_SETUP();
    KMBuffer buf(payload, len, len);
    if(frame_cb_) frame_cb_(hdr.opcode, hdr.fin, buf);
    DESTROY_DETECTOR_CHECK(WSError::DESTROYED);
    return WSError::NOERR;
}

void WSHandler::handleDataMask(const FrameHeader& hdr, uint8_t* data, size_t len)
{
    if(0 == hdr.mask) return ;
    handleDataMask(hdr.maskey, data, len);
}

void WSHandler::handleDataMask(const FrameHeader& hdr, KMBuffer &buf)
{
    if(0 == hdr.mask) return ;
    handleDataMask(hdr.maskey, buf);
}

void WSHandler::handleDataMask(const uint8_t mask_key[WS_MASK_KEY_SIZE], uint8_t* data, size_t len)
{
    if(nullptr == data || 0 == len) return ;
    
    for(size_t i=0; i < len; ++i) {
        data[i] = data[i] ^ mask_key[i%4];
    }
}

void WSHandler::handleDataMask(const uint8_t mask_key[WS_MASK_KEY_SIZE], KMBuffer &buf)
{
    size_t pos = 0;
    for (auto it = buf.begin(); it != buf.end(); ++it) {
        auto *data = static_cast<uint8_t*>(it->readPtr());
        auto len = it->length();
        for (size_t i = 0; i < len; ++i, ++pos) {
            data[i] = data[i] ^ mask_key[pos%4];
        }
    }
}

#define SHA1_DIGEST_SIZE    20
static std::string generate_sec_accept_value(const std::string& sec_ws_key)
{
    const std::string sec_accept_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
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
