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

#ifndef __H2Frame_H__
#define __H2Frame_H__

#include "kmdefs.h"
#include "h2defs.h"

#include <vector>

KUMA_NS_BEGIN

class FrameHeader
{
public:
    bool encode(uint8_t *buf, uint32_t len);
    bool decode(const uint8_t *buf, uint32_t len);
    
    uint32_t getLength() const { return length_; }
    uint8_t getType() const { return type_; }
    uint8_t getFlags() const { return flags_; }
    uint32_t getStreamId() const { return streamId_; }
    
protected:
    uint32_t length_ = 0;
    uint8_t type_;
    uint8_t flags_ = 0;
    uint32_t streamId_ = H2_MAX_STREAM_ID;
};

class H2Frame
{
public:
    virtual ~H2Frame() {}
    
    void setFrameHeader(const FrameHeader &hdr);

    virtual H2FrameType type() = 0;
    virtual H2Error decode(const FrameHeader &hdr, const uint8_t *payload) = 0;
    
    uint32_t getStreamId() { return hdr_.getStreamId(); }
    
protected:
    FrameHeader hdr_;
};

class DataFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::DATA; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    
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
    
    bool hasPriority() { return hdr_.getFlags() & H2_FRAME_FLAG_PRIORITY; }
    bool hasEndHeaders() { return hdr_.getFlags() & H2_FRAME_FLAG_END_HEADERS; }
    const uint8_t* getBlock() { return block_; }
    uint32_t getBlockSize() { return size_; }
    void setBlock(const uint8_t *block) { block_ = block; }
    void setBlockSize(uint32_t sz) { size_ = sz; }
    
private:
    uint32_t depStreamId_ = 0;
    uint16_t weight_ = 16;
    bool exclusive_ = false;
    const uint8_t *block_ = nullptr;
    uint32_t size_ = 0;
};

class PriorityFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::PRIORITY; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    
    uint32_t getDepStreamId() { return depStreamId_; }
    uint16_t getWeight() { return weight_; }
    bool isExclusive() { return exclusive_; }
    void setDepStreamId(uint32_t streamId) { depStreamId_ = streamId; }
    void setWeight(uint16_t w) { weight_ = w; }
    void setExclusive(bool e) { exclusive_ = e; }
    
private:
    uint32_t depStreamId_ = 0;
    uint16_t weight_ = 16;
    bool exclusive_ = false;
};

class RSTStreamFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::RST_STREAM; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    
    uint32_t getErrorCode() { return errCode_; }
    void setErrorCode(uint32_t errCode) { errCode_ = errCode; }
    
private:
    uint32_t errCode_ = 0;
};

class SettingsFrame : public H2Frame
{
public:
    using ParamVector = std::vector<std::pair<uint16_t, uint32_t>>;
public:
    H2FrameType type() { return H2FrameType::SETTINGS; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    
    bool isAck() { return hdr_.getFlags() & H2_FRAME_FLAG_ACK; }
    
private:
    ParamVector params_;
};

class PushPromiseFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::PUSH_PROMISE; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    
    uint32_t getPromisedStreamId() { return promStreamId_; }
    void setPromisedStreamId(uint32_t streamId) { promStreamId_ = streamId; }
    
private:
    uint32_t promStreamId_ = 0;
};

class PingFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::PING; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    
    bool isAck() { return hdr_.getFlags() & H2_FRAME_FLAG_ACK; }
    
private:
    uint8_t data_[H2_PING_FRAME_DATA_LENGTH];
};

class GoawayFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::GOAWAY; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    
    uint32_t getLastStreamId() { return lastStreamId_; }
    uint32_t getErrorCode() { return errCode_; }
    void setLastStreamId(uint32_t streamId) { lastStreamId_ = streamId; }
    void setErrorCode(uint32_t errCode) { errCode_ = errCode; }
    
private:
    uint32_t lastStreamId_ = 0;
    uint32_t errCode_ = 0;
};

class WindowUpdateFrame : public H2Frame
{
public:
    H2FrameType type() { return H2FrameType::WINDOW_UPDATE; }
    H2Error decode(const FrameHeader &hdr, const uint8_t *payload);
    
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
    
    bool hasEndHeaders() { return hdr_.getFlags() & H2_FRAME_FLAG_END_HEADERS; }
    const uint8_t* getBlock() { return block_; }
    uint32_t getBlockSize() { return size_; }
    void setBlock(const uint8_t *block) { block_ = block; }
    void setBlockSize(uint32_t sz) { size_ = sz; }
    
private:
    const uint8_t *block_ = nullptr;
    uint32_t size_ = 0;
};

KUMA_NS_END

#endif
