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

#include "FrameParser.h"
#include "util/kmtrace.h"

#include <algorithm>

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
FrameParser::FrameParser(FrameCallback *cb)
: cb_(cb)
{
    
}

FrameParser::~FrameParser()
{
    
}

FrameParser::ParseState FrameParser::parseInputData(const uint8_t *data, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        size_t used = 0;
        auto ret = parseOneFrame(data + offset, size - offset, used);
        if (ret == ParseState::SUCCESS) {
            offset += used;
        } else {
            return ret;
        }
    }
    return ParseState::SUCCESS;
}

FrameParser::ParseState FrameParser::parseOneFrame(const uint8_t *buf, size_t len, size_t &used)
{
    used = 0;
    if (ReadState::READ_HEADER == read_state_) {
        if (hdr_used_ + len < H2_FRAME_HEADER_SIZE) {
            memcpy(hdr_buf_ + hdr_used_, buf, len);
            hdr_used_ += (uint8_t)len;
            used = len;
            return ParseState::INCOMPLETE;
        }
        const uint8_t *p = buf;
        if (hdr_used_ > 0) {
            memcpy(hdr_buf_ + hdr_used_, buf, H2_FRAME_HEADER_SIZE - hdr_used_);
            p = hdr_buf_;
        }
        hdr_.decode(p, H2_FRAME_HEADER_SIZE);
        used += H2_FRAME_HEADER_SIZE - hdr_used_;
        len -= H2_FRAME_HEADER_SIZE - hdr_used_;
        buf += H2_FRAME_HEADER_SIZE - hdr_used_;
        hdr_used_ = 0;
        payload_.clear();
        payload_used_ = 0;
        if (hdr_.getLength() > max_frame_size_ && cb_) {
            bool stream_err = isStreamError(hdr_, H2Error::FRAME_SIZE_ERROR);
            cb_->onFrameError(hdr_, H2Error::FRAME_SIZE_ERROR, stream_err);
            return ParseState::FAILURE;
        }
        read_state_ = ReadState::READ_PAYLOAD;
    }
    if (ReadState::READ_PAYLOAD == read_state_) {
        const uint8_t *pl = buf;
        if (payload_.empty()) {
            if (len >= hdr_.getLength()) {
                used += hdr_.getLength();
            } else {
                payload_.resize(hdr_.getLength());
                memcpy(&payload_[0], buf, len);
                payload_used_ = len;
                used += len;
                return ParseState::INCOMPLETE;
            }
        } else {
            size_t copy_len = std::min<size_t>(len, hdr_.getLength() - payload_used_);
            memcpy(&payload_[payload_used_], buf, copy_len);
            payload_used_ += copy_len;
            used += copy_len;
            if (payload_used_ < hdr_.getLength()) {
                return ParseState::INCOMPLETE;
            }
            pl = &payload_[0];
        }
        
        auto parse_state = parseFrame(hdr_, pl);
        if (parse_state != ParseState::SUCCESS) {
            return parse_state;
        }
        read_state_ = ReadState::READ_HEADER;
        payload_.clear();
        payload_used_ = 0;
    }
    
    return ParseState::SUCCESS;
}

FrameParser::ParseState FrameParser::parseFrame(const FrameHeader &hdr, const uint8_t *payload)
{
    H2Frame *frame = nullptr;
    switch (hdr_.getType()) {
        case H2FrameType::DATA:
            frame = &data_frame_;
            break;
            
        case H2FrameType::HEADERS:
            frame = &hdr_frame_;
            break;
            
        case H2FrameType::PRIORITY:
            frame = &pri_frame_;
            break;
            
        case H2FrameType::RST_STREAM:
            frame = &rst_frame_;
            break;
            
        case H2FrameType::SETTINGS:
            frame = &settings_frame_;
            break;
            
        case H2FrameType::PUSH_PROMISE:
            frame = &push_frame_;
            break;
            
        case H2FrameType::PING:
            frame = &ping_frame_;
            break;
            
        case H2FrameType::GOAWAY:
            frame = &goaway_frame_;
            break;
            
        case H2FrameType::WINDOW_UPDATE:
            frame = &window_frame_;
            break;
            
        case H2FrameType::CONTINUATION:
            frame = &continuation_frame_;
            break;
            
        default:
            KUMA_WARNTRACE("FrameParser::handleFrame, invalid frame, type="<<frame->type());
            break;
    }
    
    if (frame && cb_) {
        H2Error err = frame->decode(hdr, payload);
        if (err == H2Error::NOERR) {
            DESTROY_DETECTOR_SETUP();
            auto parse_continue = cb_->onFrame(frame);
            DESTROY_DETECTOR_CHECK(ParseState::STOPPED);
            if (!parse_continue) {
                return ParseState::STOPPED;
            }
        } else {
            cb_->onFrameError(hdr, err, isStreamError(hdr, err));
            return ParseState::FAILURE;
        }
    }

    return ParseState::SUCCESS;
}

bool FrameParser::isStreamError(const FrameHeader &hdr, H2Error err)
{
    if (hdr.getStreamId() == 0) {
        return false;
    }
    
    switch (err) {
        case H2Error::FRAME_SIZE_ERROR:
            return (hdr.getType() != H2FrameType::HEADERS &&
                    hdr.getType() != H2FrameType::SETTINGS &&
                    hdr.getType() != H2FrameType::PUSH_PROMISE &&
                    hdr.getType() != H2FrameType::WINDOW_UPDATE);
            
        case H2Error::PROTOCOL_ERROR:
            return false;
            
        default:
            break;
    }
    
    return true;
}
