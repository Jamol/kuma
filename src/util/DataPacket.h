/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
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

#ifndef __DataPacket_H__
#define __DataPacket_H__
#include "kmconf.h"
#include "kmdefs.h"
#include <vector>

KUMA_NS_BEGIN

using IOVEC = std::vector<iovec>;

//////////////////////////////////////////////////////////////////////////
// class DataPacket
class DataPacket
{
public:
    enum {
        DP_FLAG_NONE            = 0,
        DP_FLAG_DONT_DELETE     = 0x01,
        DP_FLAG_LIFCYC_STACK    = 0x02,
    };
    using DP_FLAG = uint32_t;

private:
    class DataBlock
    {
    public:
        DataBlock(uint8_t *buf, size_t size, size_t offset)
        : buffer_(buf), size_(size), offset_(offset)
        {}
        uint8_t* get_buffer() { return buffer_.get() + offset_; }
        size_t get_length() { return size_ - offset_; }
        void detach_buffer(uint8_t *&buf, size_t &size, size_t &offset) {
            buf = buffer_.release();
            size = size_;
            offset = offset_;
            offset_ = 0;
            size_ = 0;
        }
        
        DataBlock() = delete;
        DataBlock &operator= (const DataBlock &) = delete;
        DataBlock (const DataBlock &) = delete;

    private:
        std::unique_ptr<uint8_t[]> buffer_;
        size_t size_;
        size_t offset_;
    };
    using DataBlockPtr = std::shared_ptr<DataBlock>;

public:
    DataPacket(DP_FLAG flags = DP_FLAG_NONE) : flags_(flags) {};

    DataPacket(DataPacket &&dp)
    : flags_(dp.flags_), begin_ptr_(dp.begin_ptr_), end_ptr_(dp.end_ptr_)
    , rd_ptr_(dp.rd_ptr_), wr_ptr_(dp.wr_ptr_), data_block_(std::move(dp.data_block_))
    {
        dp.flags_ = DP_FLAG_NONE;
        dp.begin_ptr_ = nullptr;
        dp.end_ptr_ = nullptr;
        dp.rd_ptr_ = nullptr;
        dp.wr_ptr_ = nullptr;
        
        if (dp.prev_ != &dp) {
            next_ = dp.next_;
            next_->prev_ = this;
            dp.next_ = &dp;
            
            prev_ = dp.prev_;
            prev_->next_ = this;
            dp.prev_ = &dp;
        }
    }
    
    DataPacket(uint8_t *buf, size_t len, size_t offset=0, DP_FLAG flags=DP_FLAG_LIFCYC_STACK|DP_FLAG_DONT_DELETE)
    : flags_(flags)
    {
        if(flags_ & DP_FLAG_DONT_DELETE) {
            if(offset < len) {
                begin_ptr_ = buf;
                end_ptr_ = begin_ptr_ + len;
                rd_ptr_ = begin_ptr_ + offset;
                wr_ptr_ = begin_ptr_ + offset;
            }
        } else {// we own the buffer
            if(offset > len) offset = len;
            data_block_ = std::make_shared<DataBlock>(buf, len, offset);
            begin_ptr_ = buf;
            end_ptr_ = begin_ptr_ + len;
            rd_ptr_ = begin_ptr_ + offset;
            wr_ptr_ = begin_ptr_ + offset;
        }
    }
    
    virtual ~DataPacket()
    {
        while (next_ != this) {
            next_->releaseSelf();
        }
        data_block_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
        prev_ = next_ = this;
    }

    virtual bool allocBuffer(size_t len)
    {
        if(0 == len) return false;
        uint8_t *buf = new uint8_t[len];
        if(!buf) {
            return false;
        }
        flags_ &= ~DP_FLAG_DONT_DELETE;
        data_block_ = std::make_shared<DataBlock>(buf, len, 0);
        begin_ptr_ = buf;
        end_ptr_ = begin_ptr_ + len;
        rd_ptr_ = begin_ptr_;
        wr_ptr_ = begin_ptr_;
        return true;
    }
    
    virtual bool attachBuffer(uint8_t *buf, size_t len, size_t offset=0)
    {
        if(offset >= len) {
            return false;
        }
        flags_ &= ~DP_FLAG_DONT_DELETE;
        data_block_ = std::make_shared<DataBlock>(buf, len, offset);
        begin_ptr_ = buf;
        end_ptr_ = begin_ptr_ + len;
        rd_ptr_ = begin_ptr_ + offset;
        wr_ptr_ = end_ptr_;
        return true;
    }

    // don't call detach_buffer if DataPacket is cloned
    virtual void detachBuffer(uint8_t *&buf, size_t &len, size_t &offset)
    {
        buf = nullptr;
        len = 0;
        offset = 0;
        if(!(flags_ & DP_FLAG_DONT_DELETE) && data_block_) {
            data_block_->detach_buffer(buf, len, offset);
            data_block_ = nullptr;
            offset = (unsigned int)(rd_ptr_ - buf);
            return ;
        }
        buf = begin_ptr_;
        offset = (unsigned int)(rd_ptr_ - begin_ptr_);
        len = (unsigned int)(end_ptr_ - begin_ptr_);
        data_block_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
    }

    size_t space() const
    {
        if(wr_ptr_ > end_ptr_) return 0;
        return end_ptr_ - wr_ptr_;
    }
    
    size_t length() const
    {
        if(rd_ptr_ > wr_ptr_) return 0;
        return wr_ptr_ - rd_ptr_;
    }
    
    size_t read(uint8_t *buf, size_t len)
    {
        size_t ret = length();
        if(0 == ret) return 0;
        ret = ret>len?len:ret;
        if(buf) {
            memcpy(buf, rd_ptr_, ret);
        }
        rd_ptr_ += ret;
        return ret;
    }
    
    size_t write(const uint8_t *buf, size_t len)
    {
        size_t ret = space();
        if(0 == ret) return 0;
        ret = ret > len ? len:ret;
        memcpy(wr_ptr_, buf, ret);
        wr_ptr_ += ret;
        return ret;
    }
    
    uint8_t* readPtr() const { return rd_ptr_; }
    uint8_t* writePtr() const { return wr_ptr_; }

    bool isChained() const { return next_ != this; }

    void advReadPtr(size_t len)
    {
        auto *dp = this;
        do {
            if (len <= length()) {
                rd_ptr_ += len;
                break;
            } else {
                rd_ptr_ = wr_ptr_;
            }
            dp = dp->next_;
        } while (dp != this);
    }
    
    void advWritePtr(size_t len)
    {
        if(space() == 0) {
            return ;
        }
        if(len > space()) {
            wr_ptr_ = end_ptr_;
        } else {
            wr_ptr_ += len;
        }
    }

    size_t totalLength() const
    {
        size_t total_len = 0;
        auto *dp = this;
        do {
            total_len += length();
            dp = dp->next_;
        } while (dp != this);
        
        return total_len;
    }
    
    size_t readChained(uint8_t *buf, size_t len)
    {
        auto *dp = this;
        size_t total_read = 0;
        do {
            total_read += dp->read(!buf ? nullptr : (buf+total_read), len-total_read);
            if(len == total_read) {
                break;
            }
            dp = dp->next_;
        } while (dp != this);
        
        return total_read;
    }
    
    void append(DataPacket* dp)
    {
        auto my_tail = prev_;
        auto dp_tail = dp->prev_;
        my_tail->next_ = dp;
        dp->prev_ = my_tail;
        dp_tail->next_ = this;
        prev_ = dp_tail;
    }

    virtual DataPacket* clone() const
    {
        auto *dup = cloneSelf();
        auto *dp = next_;
        while (dp != this) {
            dup->append(dp->cloneSelf());
            dp = dp->next_;
        }
        return dp;
    }

    virtual DataPacket* subpacket(size_t offset, size_t len) const
    {
        if (len == 0) {
            return nullptr;
        }
        DataPacket *dup = nullptr;
        auto dp = this;
        do {
            if(offset < length()) {
                size_t copy_len = offset+len <= length() ? len : length() - offset;
                DataPacket *dd = nullptr;
                if(flags_ & DP_FLAG_DONT_DELETE) {
                    DP_FLAG flags = flags_;
                    flags &= ~DP_FLAG_DONT_DELETE;
                    flags &= ~DP_FLAG_LIFCYC_STACK;
                    dd = new DataPacket(flags);
                    dd->allocBuffer(copy_len);
                    dd->write(readPtr() + offset, copy_len);
                } else {
                    dd = cloneSelf();
                    dd->rd_ptr_ += offset;
                    dd->wr_ptr_ = dd->rd_ptr_ + copy_len;
                }
                offset = 0;
                len -= copy_len;
                if (dup) {
                    dup->append(dd);
                } else {
                    dup = dd;
                }
            } else {
                offset -= length();
            }
            dp = dp->next_;
        } while (dp != this && len > 0);
        
        return dup;
    }
    
    virtual void reclaim(){
        if(length() > 0) {
            return ;
        }
        while(next_ != this && next_->length() == 0) {
            next_->releaseSelf();
        }
        data_block_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
    }
    
    int fillIov(IOVEC& iovs) const {
        int cnt = 0;
        auto *dp = this;
        do {
            if (dp->length() > 0) {
                iovec v;
                v.iov_base = (char*)dp->readPtr();
                v.iov_len = dp->length();
                iovs.push_back(v);
                ++cnt;
            }
            dp = dp->next_;
        } while (dp != this);

        return cnt;
    }
    
    virtual void release()
    {
        while (next_ != this) {
            next_->releaseSelf();
        }
        releaseSelf();
    }

private:
    DataPacket &operator= (const DataPacket &) = delete;
    DataPacket (const DataPacket &) = delete;

    DataBlockPtr data_block() { return data_block_; }
    void data_block(const DataBlockPtr &db) {
        data_block_ = db;
    }
    virtual DataPacket* cloneSelf() const {
        DataPacket* dup = nullptr;
        if(flags_ & DP_FLAG_DONT_DELETE) {
            DP_FLAG flags = flags_;
            flags &= ~DP_FLAG_DONT_DELETE;
            flags &= ~DP_FLAG_LIFCYC_STACK;
            dup = new DataPacket(flags);
            if(dup->allocBuffer(length())) {
                dup->write(readPtr(), length());
            }
        } else {
            DP_FLAG flags = flags_;
            flags &= ~DP_FLAG_LIFCYC_STACK;
            dup = new DataPacket(flags);
            dup->data_block_ = data_block_;
            dup->begin_ptr_ = begin_ptr_;
            dup->end_ptr_ = end_ptr_;
            dup->rd_ptr_ = rd_ptr_;
            dup->wr_ptr_ = wr_ptr_;
        }
        return dup;
    }
    void releaseSelf() {
        next_->prev_ = prev_;
        prev_->next_ = next_;
        prev_ = next_ = this;
        
        data_block_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
        if(!(flags_ & DP_FLAG_LIFCYC_STACK)) {
            delete this;
        }
    }

private:
    DP_FLAG	flags_{ DP_FLAG_NONE };
    uint8_t* begin_ptr_{ nullptr };
    uint8_t* end_ptr_{ nullptr };
    uint8_t* rd_ptr_{ nullptr };
    uint8_t* wr_ptr_{ nullptr };
    DataBlockPtr data_block_;

    DataPacket* prev_{ this };
    DataPacket* next_{ this };
};

KUMA_NS_END

#endif
