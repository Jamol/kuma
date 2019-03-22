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


using namespace kuma;
using namespace kuma::ws;

//////////////////////////////////////////////////////////////////////////
WSHandler::WSHandler()
{
    
}

void WSHandler::cleanup()
{
    ctx_.reset();
}

WSError WSHandler::handleData(uint8_t* data, size_t len)
{
    return decodeFrame(data, len);
}

int WSHandler::encodeFrameHeader(FrameHeader hdr, uint8_t hdr_buf[WS_MAX_HEADER_SIZE])
{
    uint8_t first_byte = 0x00;
    if (hdr.fin) {
        first_byte |= 0x80;
    }
    if (hdr.rsv1) {
        first_byte |= 0x40;
    }
    if (hdr.rsv2) {
        first_byte |= 0x20;
    }
    if (hdr.rsv3) {
        first_byte |= 0x10;
    }
    first_byte |= (uint8_t)hdr.opcode;
    uint8_t second_byte = 0x00;
    if (hdr.mask) {
        second_byte |= 0x80;
    }
    uint8_t hdr_len = 2;
    
    if(hdr.length <= 125)
    {//0 byte
        second_byte |= (uint8_t)hdr.length;
    }
    else if(hdr.length <= 0xFFFF)
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
        hdr_buf[2] = (unsigned char)(hdr.length >> 8);
        hdr_buf[3] = (unsigned char)hdr.length;
    }
    else if(127 == (second_byte & 0x7F))
    {
        hdr_buf[2] = 0;
        hdr_buf[3] = 0;
        hdr_buf[4] = 0;
        hdr_buf[5] = 0;
        hdr_buf[6] = (unsigned char)(hdr.length >> 24);
        hdr_buf[7] = (unsigned char)(hdr.length >> 16);
        hdr_buf[8] = (unsigned char)(hdr.length >> 8);
        hdr_buf[9] = (unsigned char)hdr.length;
    }
    if (hdr.mask) {
        memcpy(hdr_buf + hdr_len, hdr.maskey, WS_MASK_KEY_SIZE);
        hdr_len += WS_MASK_KEY_SIZE;
    }
    return hdr_len;
}

WSError WSHandler::decodeFrame(uint8_t* data, size_t len)
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
                ctx_.hdr.rsv1 = (b >> 6) & 0x01;
                ctx_.hdr.rsv2 = (b >> 5) & 0x01;
                ctx_.hdr.rsv3 = (b >> 4) & 0x01;
                if (!ctx_.hdr.fin && isControlFrame(ctx_.hdr.opcode)) {
                    // Control frames MUST NOT be fragmented
                    ctx_.state = DecodeState::IN_ERROR;
                    return WSError::PROTOCOL_ERROR;
                }
                // TODO: check interleaved fragments of different messages
                ctx_.state = DecodeState::HDR2;
                
                FALLTHROUGH;
            }
            case DecodeState::HDR2:
            {
                if (pos < len) {
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
                } else {
                    return WSError::NEED_MORE_DATA;
                }
                
                FALLTHROUGH;
            }
            case DecodeState::HDREX:
            {
                if (126 == ctx_.hdr.plen) {
                    uint32_t expect_len = 2;
                    if (len-pos+ctx_.pos >= expect_len) {
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
                } else if (127 == ctx_.hdr.plen) {
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
                
                FALLTHROUGH;
            }
            case DecodeState::MASKEY:
            {
                if (ctx_.hdr.mask) {
                    if (WSMode::CLIENT == mode_) {
                        // server MUST NOT mask any frames
                        ctx_.state = DecodeState::IN_ERROR;
                        return WSError::PROTOCOL_ERROR;
                    }
                    uint32_t expect_len = 4;
                    if (len-pos+ctx_.pos >= expect_len)
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
                
                FALLTHROUGH;
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
                if (ctx_.buf.empty()) {
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
                if ((uint8_t)WSOpcode::CLOSE == ctx_.hdr.opcode) {
                    ctx_.state = DecodeState::CLOSED;
                    return WSError::CLOSED;
                }
                // reset context for next frame
                ctx_.reset();
                break;
            }
            default:
            {
                return WSError::INVALID_FRAME;
            }
        }
    }
    return ctx_.state == DecodeState::HDR1 ? WSError::NOERR : WSError::NEED_MORE_DATA;
}

WSError WSHandler::handleFrame(const FrameHeader &hdr, void* payload, size_t len)
{
    DESTROY_DETECTOR_SETUP();
    KMBuffer buf(payload, len, len);
    if(frame_cb_) frame_cb_(hdr, buf);
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
        data[i] = data[i] ^ mask_key[i%WS_MASK_KEY_SIZE];
    }
}

void WSHandler::handleDataMask(const uint8_t mask_key[WS_MASK_KEY_SIZE], KMBuffer &buf)
{
    size_t pos = 0;
    for (auto it = buf.begin(); it != buf.end(); ++it) {
        auto *data = static_cast<uint8_t*>(it->readPtr());
        auto len = it->length();
        for (size_t i = 0; i < len; ++i, ++pos) {
            data[i] = data[i] ^ mask_key[pos%WS_MASK_KEY_SIZE];
        }
    }
}

void WSHandler::reset()
{
    ctx_.reset();
}
