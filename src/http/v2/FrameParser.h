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
#include "libkev/src/util/DestroyDetector.h"

KUMA_NS_BEGIN

class FrameCallback
{
public:
    /**
     * return false to stop the parsing
     */
    virtual bool onFrame(H2Frame *frame) = 0;
    
    virtual void onFrameError(const FrameHeader &hdr, H2Error err, bool stream_err) = 0;
    
    virtual ~FrameCallback() {}
};

class FrameParser : public kev::DestroyDetector
{
public:
    FrameParser(FrameCallback *cb);
    ~FrameParser();
    
    enum class ParseState {
        SUCCESS,
        INCOMPLETE,
        FAILURE,
        STOPPED
    };
    void setMaxFrameSize(uint32_t max_frame_size) { max_frame_size_ = max_frame_size; }
    ParseState parseInputData(const uint8_t *buf, size_t len);
    ParseState parseOneFrame(const uint8_t *buf, size_t len, size_t &used);
    
private:
    ParseState parseFrame(const FrameHeader &hdr, const uint8_t *payload);
    bool isStreamError(const FrameHeader &hdr, H2Error err);

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
    uint32_t max_frame_size_ = H2_DEFAULT_FRAME_SIZE;
    
    std::vector<uint8_t> payload_;
    size_t payload_used_ = 0;
    
    DataFrame data_frame_;
    HeadersFrame hdr_frame_;
    PriorityFrame pri_frame_;
    RSTStreamFrame rst_frame_;
    SettingsFrame settings_frame_;
    PushPromiseFrame push_frame_;
    PingFrame ping_frame_;
    GoawayFrame goaway_frame_;
    WindowUpdateFrame window_frame_;
    ContinuationFrame continuation_frame_;
};

KUMA_NS_END

#endif
