//
//  kmbuffer.h
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 7/17/16.
//  Copyright © 2016 kuma. All rights reserved.
//

#ifndef __KMBuffer_H__
#define __KMBuffer_H__

#include "kmdefs.h"
#include <vector>
#include <algorithm>
#include <string>

KUMA_NS_BEGIN

class KMBuffer
{
public:
    KMBuffer() {
        
    }
    
    bool empty() const
    {
        return buf_.empty() || length_ <= offset_;
    }
    
    size_t size() const
    {
        return empty() ? 0 : length_ - offset_;
    }
    
    size_t space() const
    {
        return buf_.size() - length_;
    }
    
    uint8_t* ptr()
    {
        if (empty()) {
            return nullptr;
        }
        return &buf_[offset_];
    }
    
    uint8_t* wr_ptr()
    {
        if (buf_.empty()) {
            return nullptr;
        }
        return &buf_[length_];
    }
    
    size_t bytes_written(size_t sz)
    {
        size_t l = std::min(space(), sz);
        length_ += l;
        return l;
    }
    
    size_t bytes_read(size_t sz)
    {
        size_t l = std::min(this->size(), sz);
        offset_ += l;
        if (offset_ == length_) {
            offset_ = length_ = 0;
        }
        return l;
    }
    
    bool expand(size_t sz)
    {
        if (space() >= sz) {
            return true;
        }
        if (offset_ > 0) {
            memmove(&buf_[0], &buf_[offset_], this->size());
            length_ -= offset_;
            offset_ = 0;
        }
        if (space() < sz) {
            buf_.resize(length_ + sz);
        }
        return true;
    }
    
    size_t write(const std::string &str)
    {
        return write(str.c_str(), str.size());
    }
    
    size_t write(const void *data, size_t sz)
    {
        expand(sz);
        memcpy(&buf_[length_], data, sz);
        length_ += sz;
        return sz;
    }
    
    size_t read(void *data, size_t sz)
    {
        size_t l = std::min(this->size(), sz);
        if (data && l > 0) {
            memcpy(data, &buf_[offset_], l);
        }
        offset_ += l;
        if (offset_ == length_) {
            offset_ = length_ = 0;
        }
        return l;
    }
    
    void reset()
    {
        offset_ = length_ = 0;
    }
    
protected:
    std::vector<uint8_t> buf_;
    size_t length_ = 0;
    size_t offset_ = 0;
};

KUMA_NS_END

#endif /* __KMBuffer_H__ */
