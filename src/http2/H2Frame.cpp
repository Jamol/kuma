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

#include "H2Frame.h"
#include "util/util.h"

using namespace kuma;

//////////////////////////////////////////////////////////////////////////
int FrameHeader::encode(uint8_t *dst, size_t len)
{
    if (!dst || len < H2_FRAME_HEADER_SIZE) {
        return -1;
    }
    encode_u24(dst, length_);
    dst[3] = type_;
    dst[4] = flags_;
    encode_u32(dst + 5, streamId_);
    
    return H2_FRAME_HEADER_SIZE;
}

bool FrameHeader::decode(const uint8_t *src, size_t len)
{
    if (!src || len < H2_FRAME_HEADER_SIZE) {
        return false;
    }
    if (src[5] & 0x80) { // check reserved bit
        
    }
    length_ = decode_u24(src);
    type_ = src[3];
    flags_ = src[4];
    streamId_ = decode_u32(src + 5) & 0x7FFFFFFF;
    return true;
}

//////////////////////////////////////////////////////////////////////////
void H2Frame::setFrameHeader(const FrameHeader &hdr)
{
    hdr_ = hdr;
}

int H2Frame::encodeHeader(uint8_t *dst, size_t len, FrameHeader &hdr)
{
    return hdr.encode(dst, len);
}

H2Error H2Frame::decodePriority(const uint8_t *src, size_t len, h2_priority_t &pri)
{
    if (len < H2_PRIORITY_PAYLOAD_SIZE) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    pri.streamId = decode_u32(src);
    pri.exclusive = pri.streamId & 0x80000000;
    pri.streamId &= 0x7FFFFFFF;
    pri.weight = (uint16_t)(src[4]) + 1;
    return H2Error::NO_ERROR;
}

int H2Frame::encodePriority(uint8_t *dst, size_t len, h2_priority_t pri)
{
    if (len < H2_PRIORITY_PAYLOAD_SIZE) {
        return -1;
    }
    pri.streamId &= 0x7FFFFFFF;
    if (pri.exclusive) {
        pri.streamId |= 0x80000000;
    }
    encode_u32(dst, pri.streamId);
    dst[4] = pri.weight;
    
    return H2_PRIORITY_PAYLOAD_SIZE;
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
    size_t len = hdr.getLength();
    uint8_t pad_len = 0;
    if (hdr.getFlags() & H2_FRAME_FLAG_PADDED) {
        pad_len = *ptr++;
        if (pad_len >= len) {
            return H2Error::PROTOCOL_ERROR;
        }
        len -= pad_len + 1;
    }
    if (hdr.getFlags() & H2_FRAME_FLAG_PRIORITY) {
        H2Error err = decodePriority(ptr, len, pri_);
        if (err != H2Error::NO_ERROR) {
            return err;
        }
        ptr += H2_PRIORITY_PAYLOAD_SIZE;
        len -= H2_PRIORITY_PAYLOAD_SIZE;
    }
    block_ = ptr;
    size_ = len;
    return H2Error::NO_ERROR;
}

int HeadersFrame::encode(uint8_t *dst, size_t len, uint32_t streamId, const uint8_t *block, size_t bsize, h2_priority_t *pri, uint8_t flags)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = encode(ptr, end - ptr, streamId, bsize, pri, flags);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    if (end - ptr < bsize) {
        return -1;
    }
    memcpy(ptr, block, bsize);
    ptr += bsize;
    return int(ptr - dst);
}

int HeadersFrame::encode(uint8_t *dst, size_t len, uint32_t streamId, size_t bsize, h2_priority_t *pri, uint8_t flags)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    if (pri) {
        flags |= H2_FRAME_FLAG_PRIORITY;
    }
    flags |= H2_FRAME_FLAG_END_HEADERS;
    FrameHeader hdr;
    hdr.setType(type());
    hdr.setFlags(flags);
    hdr.setStreamId(streamId);
    hdr.setLength((pri?H2_PRIORITY_PAYLOAD_SIZE:0) + (uint32_t)bsize);
    int ret = encodeHeader(ptr, end - ptr, hdr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    if (pri) {
        ret = encodePriority(ptr, end - ptr, *pri);
        if (ret < 0) {
            return ret;
        }
        ptr += ret;
    }
    return int(ptr - dst);
}

H2Error PriorityFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getStreamId() == 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    const uint8_t *ptr = payload;
    size_t len = hdr.getLength();
    return decodePriority(ptr, len, pri_);
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

int SettingsFrame::encode(uint8_t *dst, size_t len, ParamVector &params, bool ack)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = encodePayload(ptr, end - ptr, params);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    return int(ptr - dst);
}

int SettingsFrame::encodePayload(uint8_t *dst, size_t len, ParamVector &params)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    for (auto &p : params) {
        if (ptr + 6 > end) {
            return -1;
        }
        encode_u16(ptr, p.first);
        ptr += 2;
        encode_u32(ptr, p.second);
        ptr += 4;
    }
    return int(ptr - dst);
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
