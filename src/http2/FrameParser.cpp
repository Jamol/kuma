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

#include "FrameParser.h"

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
FrameParser::FrameParser(FrameCallback *cb)
: cb_(cb)
{
    
}

FrameParser::~FrameParser()
{
    if (destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

FrameParserState FrameParser::parseInputData(const uint8_t *data, uint32_t size)
{
    const uint8_t *ptr = data;
    uint32_t sz = size;
    while (sz > 0) {
        if (ReadState::READ_HEADER == read_state_) {
            if (hdr_used_ + sz < H2_FRAME_HEADER_SIZE) {
                memcpy(hdr_buf_ + hdr_used_, ptr, sz);
                hdr_used_ += sz;
                return FrameParserState::INCOMPLETE;
            }
            const uint8_t *p = ptr;
            if (hdr_used_ > 0) {
                memcpy(hdr_buf_ + hdr_used_, ptr, H2_FRAME_HEADER_SIZE - hdr_used_);
                p = hdr_buf_;
            }
            hdr_.decode(p, H2_FRAME_HEADER_SIZE);
            sz -= H2_FRAME_HEADER_SIZE - hdr_used_;
            ptr += H2_FRAME_HEADER_SIZE - hdr_used_;
            hdr_used_ = 0;
            
            payload_.clear();
            payload_used_ = 0;
            read_state_ = ReadState::READ_PAYLOAD;
        }
        if (ReadState::READ_PAYLOAD == read_state_) {
            if (payload_.empty()) {
                if (sz >= hdr_.getLength()) {
                    if (!handleFrame(hdr_, ptr)) {
                        return FrameParserState::FAILURE;
                    }
                    sz -= hdr_.getLength();
                    ptr += hdr_.getLength();
                    read_state_ = ReadState::READ_HEADER;
                } else {
                    payload_.resize(hdr_.getLength());
                    memcpy(&payload_[0], ptr, sz);
                    payload_used_ = sz;
                    return FrameParserState::INCOMPLETE;
                }
            } else {
                uint32_t copy_len = std::min(sz, hdr_.getLength() - payload_used_);
                memcpy(&payload_[payload_used_], ptr, copy_len);
                payload_used_ += copy_len;
                if (payload_used_ < hdr_.getLength()) {
                    return FrameParserState::INCOMPLETE;
                }
                sz -= copy_len;
                ptr += copy_len;
                read_state_ = ReadState::READ_HEADER;
                if (!handleFrame(hdr_, &payload_[0])) {
                    return FrameParserState::FAILURE;
                }
                payload_.clear();
                payload_used_ = 0;
            }
        }
    }
    return FrameParserState::SUCCESS;
}

bool FrameParser::handleFrame(const FrameHeader &hdr, const uint8_t *payload)
{
    H2Frame *frame = nullptr;
    switch (hdr_.getType()) {
        case H2FrameType::DATA:
            frame = &dataFrame_;
            break;
            
        case H2FrameType::HEADERS:
            frame = &hdrFrame_;
            break;
            
        case H2FrameType::PRIORITY:
            frame = &priFrame_;
            break;
            
        case H2FrameType::RST_STREAM:
            frame = &rstFrame_;
            break;
            
        case H2FrameType::SETTINGS:
            frame = &settingsFrame_;
            break;
            
        case H2FrameType::PUSH_PROMISE:
            frame = &pushFrame_;
            break;
            
        case H2FrameType::PING:
            frame = &pingFrame_;
            break;
            
        case H2FrameType::GOAWAY:
            frame = &goawayFrame_;
            break;
            
        case H2FrameType::WINDOW_UPDATE:
            frame = &windowFrame_;
            break;
            
        case H2FrameType::CONTINUATION:
            frame = &continuationFrame_;
            break;
    }
    
    if (frame && cb_) {
        bool destroyed = false;
        destroy_flag_ptr_ = &destroyed;
        
        H2Error err = frame->decode(hdr, payload);
        if (err == H2Error::NO_ERROR) {
            cb_->onFrame(frame);
        } else {
            cb_->onFrameError(hdr, err);
        }
      
        if (destroyed) {
            return false;
        }
        destroy_flag_ptr_ = nullptr;
    }

    return true;
}
