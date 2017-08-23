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
    encode_u32(dst + 5, stream_id_);
    
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
    stream_id_ = decode_u32(src + 5) & 0x7FFFFFFF;
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

int H2Frame::encodeHeader(uint8_t *dst, size_t len)
{
    hdr_.setType(type());
    hdr_.setLength((uint32_t)calcPayloadSize());
    return hdr_.encode(dst, len);
}

H2Error H2Frame::decodePriority(const uint8_t *src, size_t len, h2_priority_t &pri)
{
    if (len < H2_PRIORITY_PAYLOAD_SIZE) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    pri.stream_id = decode_u32(src);
    pri.exclusive = !!(pri.stream_id & 0x80000000);
    pri.stream_id &= 0x7FFFFFFF;
    pri.weight = (uint16_t)(src[4]) + 1;
    return H2Error::NOERR;
}

int H2Frame::encodePriority(uint8_t *dst, size_t len, h2_priority_t pri)
{
    if (len < H2_PRIORITY_PAYLOAD_SIZE) {
        return -1;
    }
    pri.stream_id &= 0x7FFFFFFF;
    if (pri.exclusive) {
        pri.stream_id |= 0x80000000;
    }
    encode_u32(dst, pri.stream_id);
    dst[4] = (uint8_t)pri.weight;
    
    return H2_PRIORITY_PAYLOAD_SIZE;
}

//////////////////////////////////////////////////////////////////////////
int DataFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    if (end - ptr < size_) {
        return -1;
    }
    memcpy(ptr, data_, size_);
    ptr += size_;
    return int(ptr - dst);
}

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
    return H2Error::NOERR;
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
        if (err != H2Error::NOERR) {
            return err;
        }
        ptr += H2_PRIORITY_PAYLOAD_SIZE;
        len -= H2_PRIORITY_PAYLOAD_SIZE;
    }
    block_ = ptr;
    bsize_ = len;
    return H2Error::NOERR;
}

int HeadersFrame::encode(uint8_t *dst, size_t len, size_t bsize)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    bsize_ = bsize;
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    if (hasPriority()) {
        ret = encodePriority(ptr, end - ptr, pri_);
        if (ret < 0) {
            return ret;
        }
        ptr += ret;
    }
    return int(ptr - dst);
}

int HeadersFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    if (hasPriority()) {
        ret = encodePriority(ptr, end - ptr, pri_);
        if (ret < 0) {
            return ret;
        }
        ptr += ret;
    }
    
    if (end - ptr < bsize_) {
        return -1;
    }
    memcpy(ptr, block_, bsize_);
    ptr += bsize_;
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

int PriorityFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    ret = encodePriority(ptr, end - ptr, pri_);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    return int(ptr - dst);
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
    err_code_ = decode_u32(payload);
    return H2Error::NOERR;
}

int RSTStreamFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    encode_u32(ptr, err_code_);
    ptr += 4;
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

int SettingsFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    ret = encodePayload(ptr, end - ptr, params_);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
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
    return H2Error::NOERR;
}

int PushPromiseFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    if (len - ret < calcPayloadSize()) {
        return -1;
    }
    
    encode_u32(ptr, prom_stream_id_);
    ptr += 4;
    
    if (block_ && bsize_ > 0) {
        memcpy(ptr, block_, bsize_);
        ptr += bsize_;
    }
    return int(ptr - dst);
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
    prom_stream_id_ = decode_u32(ptr) & 0x7FFFFFFF;
    ptr += 4;
    len -= 4;
    if (len > 0) {
        block_ = ptr;
        bsize_ = len;
    }
    return H2Error::NOERR;
}

int PingFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    if (len - ret < H2_PING_PAYLOAD_SIZE) {
        return -1;
    }
    
    memcpy(ptr, data_, H2_PING_PAYLOAD_SIZE);
    ptr += H2_PING_PAYLOAD_SIZE;
    return int(ptr - dst);
}

H2Error PingFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getStreamId() != 0) {
        return H2Error::PROTOCOL_ERROR;
    }
    if (hdr.getLength() != H2_PING_PAYLOAD_SIZE) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    memcpy(data_, payload, hdr.getLength());
    return H2Error::NOERR;
}

void PingFrame::setData(const uint8_t *data, size_t len)
{
    if (len != H2_PING_PAYLOAD_SIZE) {
        return;
    }
    memcpy(data_, data, H2_PING_PAYLOAD_SIZE);
}

int GoawayFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    if (len - ret < calcPayloadSize()) {
        return -1;
    }
    
    encode_u32(ptr, last_stream_id_);
    ptr += 4;
    encode_u32(ptr, err_code_);
    ptr += 4;
    if (size_ > 0) {
        memcpy(ptr, data_, size_);
        ptr += size_;
    }
    return int(ptr - dst);
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
    last_stream_id_ = decode_u32(ptr) & 0x7FFFFFFF;
    ptr += 4;
    len -= 4;
    err_code_ = decode_u32(ptr);
    ptr += 4;
    len -= 4;
    if (len > 0) {
        data_ = ptr;
        size_ = len;
    }
    return H2Error::NOERR;
}

int WindowUpdateFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    if (len - ret < calcPayloadSize()) {
        return -1;
    }
    
    encode_u32(ptr, window_size_increment_);
    ptr += 4;
    return int(ptr - dst);
}

H2Error WindowUpdateFrame::decode(const FrameHeader &hdr, const uint8_t *payload)
{
    setFrameHeader(hdr);
    if (hdr.getLength() != 4) {
        return H2Error::FRAME_SIZE_ERROR;
    }
    window_size_increment_ = decode_u32(payload) & 0x7FFFFFFF;
    return H2Error::NOERR;
}

int ContinuationFrame::encode(uint8_t *dst, size_t len)
{
    uint8_t *ptr = dst;
    const uint8_t *end = dst + len;
    
    int ret = H2Frame::encodeHeader(ptr, end - ptr);
    if (ret < 0) {
        return ret;
    }
    ptr += ret;
    
    if (len - ret < calcPayloadSize()) {
        return -1;
    }
    
    if (block_ && bsize_ > 0) {
        memcpy(ptr, block_, bsize_);
        ptr += bsize_;
    }
    return int(ptr - dst);
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
    bsize_ = len;
    return H2Error::NOERR;
}

KUMA_NS_BEGIN
const std::string& H2FrameTypeToString(H2FrameType type)
{
#define CASE_FRAME_TYPE(TYPE) \
    case H2FrameType::TYPE: { \
    static const std::string str_frame_type(#TYPE); \
        return str_frame_type; \
    }

    static const std::string unknown_type = "Unknown";
    switch (type) {
        CASE_FRAME_TYPE(DATA);
        CASE_FRAME_TYPE(HEADERS);
        CASE_FRAME_TYPE(PRIORITY);
        CASE_FRAME_TYPE(RST_STREAM);
        CASE_FRAME_TYPE(SETTINGS);
        CASE_FRAME_TYPE(PUSH_PROMISE);
        CASE_FRAME_TYPE(PING);
        CASE_FRAME_TYPE(GOAWAY);
        CASE_FRAME_TYPE(WINDOW_UPDATE);
        CASE_FRAME_TYPE(CONTINUATION);
    default:
        return unknown_type;
    }

#undef CASE_FRAME_TYPE
}

KUMA_NS_END
