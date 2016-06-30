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

#include "H2Frame.h"
#include "util/util.h"

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
bool FrameHeader::encode(uint8_t *buf, uint32_t len)
{
    if (!buf || len < H2_FRAME_HEADER_SIZE) {
        return false;
    }
    encode_u24(length_, buf);
    buf[3] = type_;
    buf[4] = flags_;
    encode_u32(streamId_, buf + 5);
    
    return true;
}

bool FrameHeader::decode(const uint8_t *buf, uint32_t len)
{
    if (!buf || len < H2_FRAME_HEADER_SIZE) {
        return false;
    }
    if (buf[5] & 0x80) { // check reserved bit
        
    }
    length_ = decode_u24(buf);
    type_ = buf[3];
    flags_ = buf[4];
    streamId_ = decode_u32(buf + 5) & 0x7FFFFFFF;
    return true;
}

//////////////////////////////////////////////////////////////////////////
void H2Frame::setFrameHeader(const FrameHeader &hdr)
{
    hdr_ = hdr;
}

//////////////////////////////////////////////////////////////////////////
H2Error DataFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    
    if (hdr.getStreamId() == 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    const uint8_t *ptr = payload;
    uint32_t len = hdr.getLength();
    uint8_t pad_len = 0;
    if (hdr.getFlags() & H2_FRAME_FLAG_PADDED) {
        pad_len = *ptr++;
        if (pad_len >= len) {
            return H2Error::PROTOCOL_ERROR;
        }
        len -= pad_len + 1;
    }
    data_ = ptr;
    size_ = len;
    return H2Error::NO_ERROR;
}

H2Error HeadersFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    
    if (hdr.getStreamId() == 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    const uint8_t *ptr = payload;
    uint32_t len = hdr.getLength();
    uint8_t pad_len = 0;
    if (hdr.getFlags() & H2_FRAME_FLAG_PADDED) {
        pad_len = *ptr++;
        if (pad_len >= len) {
            return H2Error::PROTOCOL_ERROR;
        }
        len -= pad_len + 1;
    }
    if (hdr.getFlags() & H2_FRAME_FLAG_PRIORITY) {
        if (len < 5) {
            return H2Error::FRAME_SIZE_ERROR;
        }
        depStreamId_ = decode_u32(ptr);
        exclusive_ = depStreamId_ & 0x80000000;
        depStreamId_ &= 0x7FFFFFFF;
        ptr += 4;
        len -= 4;
        weight_ = (uint16_t)(*ptr++) + 1;
        --len;
    }
    block_ = ptr;
    size_ = len;
    return H2Error::NO_ERROR;
}

H2Error PriorityFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getStreamId() == 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    const uint8_t *ptr = payload;
    uint32_t len = hdr.getLength();
    if (len != 5) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    depStreamId_ = decode_u32(ptr);
    exclusive_ = depStreamId_ & 0x80000000;
    depStreamId_ &= 0x7FFFFFFF;
    weight_ = (uint16_t)(*(ptr + 4)) + 1;
    return H2Error::NO_ERROR;
}

H2Error RSTStreamFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getStreamId() == 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    if (hdr.getLength() != 4) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    errCode_ = decode_u32(payload);
    return H2Error::NO_ERROR;
}

H2Error SettingsFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getStreamId() != 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    if (isAck() && hdr.getLength() != 0) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    if (hdr.getLength() % 6 != 0) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    const uint8_t *ptr = payload;
    uint32_t len = hdr.getLength();
    params_.clear();
    while (len > 0) {
        uint16_t id = decode_u16(ptr);
        uint32_t val = decode_u32(ptr + 2);
        params_.push_back(std::make_pair(id, val));
        ptr += 6;
        len -= 6;
    }
    return H2Error::NO_ERROR;
}

H2Error PushPromiseFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    
    if (hdr.getStreamId() == 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    const uint8_t *ptr = payload;
    uint32_t len = hdr.getLength();
    uint8_t pad_len = 0;
    if (hdr.getFlags() & H2_FRAME_FLAG_PADDED) {
        pad_len = *ptr++;
        if (pad_len >= len) {
            return H2Error::PROTOCOL_ERROR;
        }
        len -= pad_len + 1;
    }
    if (len < 4) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    promStreamId_ = decode_u32(ptr) & 0x7FFFFFFF;
    ptr += 4;
    len -= 4;
    return H2Error::NO_ERROR;
}

H2Error PingFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getStreamId() != 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    if (hdr.getLength() != H2_PING_FRAME_DATA_LENGTH) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    memcpy(data_, payload, hdr.getLength());
    return H2Error::NO_ERROR;
}

H2Error GoawayFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getStreamId() != 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    if (hdr.getLength() < 8) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    const uint8_t *ptr = payload;
    uint32_t len = hdr.getLength();
    lastStreamId_ = decode_u32(ptr) & 0x7FFFFFFF;
    ptr += 4;
    len -= 4;
    errCode_ = decode_u32(ptr);
    ptr += 4;
    len -= 4;
    return H2Error::NO_ERROR;
}

H2Error WindowUpdateFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getLength() != 4) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    windowSizeIncrement_ = decode_u32(payload) & 0x7FFFFFFF;
    return H2Error::NO_ERROR;
}

H2Error ContinuationFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getStreamId() == 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    const uint8_t *ptr = payload;
    uint32_t len = hdr.getLength();
    block_ = ptr;
    size_ = len;
    return H2Error::NO_ERROR;
}
