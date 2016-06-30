/* Copyright (c) 2016, Fengping Bao <jamol@live.com>
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

#ifndef __FrameParser_H__
#define __FrameParser_H__

#include "kmdefs.h"

#include <vector>

#include "H2Frame.h"

KUMA_NS_BEGIN

enum class FrameParserState {
    SUCCESS,
    INCOMPLETE,
    FAILURE
};

class FrameCallback
{
public:
    virtual void onFrame(H2Frame *frame) = 0;
    virtual void onFrameError(const FrameHeader &hdr, H2Error err) = 0;
};

class FrameParser
{
public:
	FrameParser(FrameCallback *cb);
	~FrameParser();
    
    FrameParserState parseInputData(const uint8_t *buf, uint32_t len);
    
private:
    bool handleFrame(const FrameHeader &hdr, const uint8_t *payload);

private:
    enum class ReadState {
        READ_HEADER,
        READ_PAYLOAD,
    };
private:
    FrameCallback *cb_;
    uint8_t hdr_buf_[H2_FRAME_HEADER_SIZE];
    uint8_t hdr_used_ = 0;
    ReadState read_state_ = ReadState::READ_HEADER;
    FrameHeader hdr_;
    
    std::vector<uint8_t> payload_;
    uint32_t payload_used_ = 0;
    
    DataFrame dataFrame_;
    HeadersFrame hdrFrame_;
    PriorityFrame priFrame_;
    RSTStreamFrame rstFrame_;
    SettingsFrame settingsFrame_;
    PushPromiseFrame pushFrame_;
    PingFrame pingFrame_;
    GoawayFrame goawayFrame_;
    WindowUpdateFrame windowFrame_;
    ContinuationFrame continuationFrame_;
    
    bool *destroy_flag_ptr_ = nullptr;
};

KUMA_NS_END

#endif
