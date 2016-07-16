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

#ifndef __H2Frame_H__
#define __H2Frame_H__

#include "kmdefs.h"
#include "h2defs.h"

#include <vector>

KUMA_NS_BEGIN

class FrameHeader
{
public:
    int encode(uint8_t *buf, size_t len);
    bool decode(const uint8_t *buf, size_t len);
    
    void setLength(uint32_t length) { length_ = length; }
    uint32_t getLength() const { return length_; }
    void setType(uint8_t type) { type_ = type; }
    uint8_t getType() const { return type_; }
    void setFlags(uint8_t flags) { flags_ = flags; }
    uint8_t getFlags() const { return flags_; }
    void setStreamId(uint32_t streamId) { streamId_ = streamId; }
    uint32_t getStreamId() const { return streamId_; }
    
protected:
    uint32_t length_ = 0;
    uint8_t type_;
    uint8_t flags_ = 0;
    uint32_t streamId_ = H2_MAX_STREAM_ID;
};

struct h2_priority_t {
    uint32_t streamId = 0;
    uint16_t weight = 16;
    bool exclusive = false;
};

class H2Frame
{
public:
    virtual ~H2Frame() {}
    
    void setFrameHeader(const FrameHeader &hdr);

    virtual H2FrameType type() = 0;
    virtual size_t calcPayloadSize() = 0;
    size_t calcFrameSize() { return H2_FRAME_HEADER_SIZE + calcPayloadSize(); }
    virtual H2Error decode(const FrameHeader &hdr, const uint8_t *payload) = 0;
    virtual int encode(uint8_t *dst, size_t len) = 0;
    int encodeHeader(uint8_t *dst, size_t len, FrameHeader &hdr);
    
    void setStreamId(uint32_t streamId) { hdr_.setStreamId(streamId); }
    uint32_t getStreamId() { return hdr_.getStreamId(); }
    void setFlags(uint8_t flags) { hdr_.setFlags(flags); }
    uint8_t getFlags() { return hdr_.getFlags(); }
    void addFlags(uint8_t flags) { uint8_t f = hdr_.getFlags(); f |= flags; hdr_.setFlags(f); }
    void clearFlags(uint8_t flags) { uint8_t f = hdr_.getFlags(); f &= ~flags; hdr_.setFlags(f); }
    
protected:
    int encodeHeader(uint8_t *dst, size_t len);
    
public:
    static H2Error decodePriority(const uint8_t *src, size_t len, h2_priority_t &pri);
    static int encodePriority(uint8_t *dst, size_t len, h2_priority_t pri);
    
protected:
    FrameHeader hdr_;
};

class DataFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::DATA; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    int encode(uint8_t *dst, size_t len);
    
    size_t calcPayloadSize() { return size_; }
    
    const uint8_t* data() { return data_; }
    uint32_t size() { return size_; }
    
private:
    const uint8_t *data_ = nullptr;
    uint32_t size_ = 0;
};

class HeadersFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::HEADERS; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    int encode(uint8_t *dst, size_t len);
    int encode(uint8_t *dst, size_t len, size_t bsize);
    
    size_t calcPayloadSize() { return (hasPriority()?H2_PRIORITY_PAYLOAD_SIZE:0) + bsize_; }
    
    bool hasPriority() { return hdr_.getFlags() & H2_FRAME_FLAG_PRIORITY; }
    bool hasEndHeaders() { return hdr_.getFlags() & H2_FRAME_FLAG_END_HEADERS; }
    const uint8_t* getBlock() { return block_; }
    size_t getBlockSize() { return bsize_; }
    
    void setHeaders(HeaderVector h, size_t hsize) { headers_ = std::move(h); hsize_ = hsize; }
    void setBlock(const uint8_t *block, uint32_t bsize) { block_ = block; bsize_ = bsize; }
    void setPriority(h2_priority_t pri) { pri_ = pri; addFlags(H2_FRAME_FLAG_PRIORITY); }
    void setEndHeaders() { addFlags(H2_FRAME_FLAG_END_HEADERS); }
    
    HeaderVector& getHeaders() { return headers_; }
    size_t getHeadersSize() { return hsize_; }
    
private:
    h2_priority_t pri_;
    const uint8_t *block_ = nullptr;
    size_t bsize_ = 0;
    
    HeaderVector headers_;
    size_t hsize_ = 0;
};

class PriorityFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::PRIORITY; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    int encode(uint8_t *dst, size_t len);
    
    size_t calcPayloadSize() { return H2_PRIORITY_PAYLOAD_SIZE; }
    
    h2_priority_t getPriority() { return pri_; }
    void setPriority(h2_priority_t pri) { pri_ = pri; }
    
private:
    h2_priority_t pri_;
};

class RSTStreamFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::RST_STREAM; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    int encode(uint8_t *dst, size_t len);
    
    size_t calcPayloadSize() { return H2_RST_STREAM_PAYLOAD_SIZE; }
    
    uint32_t getErrorCode() { return errCode_; }
    void setErrorCode(uint32_t errCode) { errCode_ = errCode; }
    
private:
    uint32_t errCode_ = 0;
};

class SettingsFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::SETTINGS; }
    static int encodePayload(uint8_t *dst, size_t len, ParamVector &params);
    int encode(uint8_t *dst, size_t len);
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    
    size_t calcPayloadSize() { return params_.size() * H2_SETTING_ITEM_SIZE; }
    
    void setAck(bool ack) { ack?addFlags(H2_FRAME_FLAG_ACK):clearFlags(H2_FRAME_FLAG_ACK); }
    bool isAck() { return hdr_.getFlags() & H2_FRAME_FLAG_ACK; }
    void setParams(ParamVector params) { params_ = std::move(params); }
    const ParamVector& getParams() { return params_; }
    
private:
    ParamVector params_;
};

class PushPromiseFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::PUSH_PROMISE; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    int encode(uint8_t *dst, size_t len);
    
    size_t calcPayloadSize() { return 4 + bsize_; }
    
    uint32_t getPromisedStreamId() { return promStreamId_; }
    void setPromisedStreamId(uint32_t streamId) { promStreamId_ = streamId; }
    
private:
    uint32_t promStreamId_ = 0;
    const uint8_t *block_ = nullptr;
    size_t bsize_ = 0;
};

class PingFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::PING; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    int encode(uint8_t *dst, size_t len);
    
    size_t calcPayloadSize() { return H2_PING_PAYLOAD_SIZE; }
    
    bool isAck() { return hdr_.getFlags() & H2_FRAME_FLAG_ACK; }
    
private:
    uint8_t data_[H2_PING_PAYLOAD_SIZE];
};

class GoawayFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::GOAWAY; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    int encode(uint8_t *dst, size_t len);
    
    size_t calcPayloadSize() { return 8 + size_; }
    
    uint32_t getLastStreamId() { return lastStreamId_; }
    uint32_t getErrorCode() { return errCode_; }
    void setLastStreamId(uint32_t streamId) { lastStreamId_ = streamId; }
    void setErrorCode(uint32_t errCode) { errCode_ = errCode; }
    
private:
    uint32_t lastStreamId_ = 0;
    uint32_t errCode_ = 0;
    const uint8_t *data_ = nullptr;
    size_t size_ = 0;
};

class WindowUpdateFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::WINDOW_UPDATE; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    int encode(uint8_t *dst, size_t len);
    
    size_t calcPayloadSize() { return H2_WINDOW_UPDATE_PAYLOAD_SIZE; }
    
    uint32_t getWindowSizeIncrement() { return windowSizeIncrement_; }
    void setWindowSizeIncrement(uint32_t w) { windowSizeIncrement_ = w; }
    
private:
    uint32_t windowSizeIncrement_ = 0;
};

class ContinuationFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::CONTINUATION; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    int encode(uint8_t *dst, size_t len);
    
    size_t calcPayloadSize() { return bsize_;}
    
    bool hasEndHeaders() { return hdr_.getFlags() & H2_FRAME_FLAG_END_HEADERS; }
    const uint8_t* getBlock() { return block_; }
    uint32_t getBlockSize() { return bsize_; }
    void setBlock(const uint8_t *block) { block_ = block; }
    void setBlockSize(uint32_t sz) { bsize_ = sz; }
    
private:
    const uint8_t *block_ = nullptr;
    uint32_t bsize_ = 0;
};

KUMA_NS_END

#endif
