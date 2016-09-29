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

#ifndef __FrameParser_H__
#define __FrameParser_H__

#include "kmdefs.h"

#include <vector>

#include "H2Frame.h"
#include "util/DestroyDetector.h"

KUMA_NS_BEGIN

class FrameCallback
{
public:
    virtual void onFrame(H2Frame *frame) = 0;
    virtual void onFrameError(const FrameHeader &hdr, H2Error err, bool stream) = 0;
};

class FrameParser : public DestroyDetector
{
public:
    FrameParser(FrameCallback *cb);
    ~FrameParser();
    
    enum class ParseState {
        SUCCESS,
        INCOMPLETE,
        FAILURE
    };
    ParseState parseInputData(const uint8_t *buf, size_t len);
    
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
    size_t payload_used_ = 0;
    
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
};

KUMA_NS_END

#endif
