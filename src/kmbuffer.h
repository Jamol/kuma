/* Copyright (c) 2014-2017, Fengping Bao <jamol@live.com>
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

#ifndef __KMBuffer_H__
#define __KMBuffer_H__
#include "kmconf.h"
#include "kmdefs.h"
#include <vector>
#include <atomic>

namespace {
    class _SharedBase
    {
    public:
        virtual ~_SharedBase() {}
        virtual char* data() = 0;
        virtual size_t size() const = 0;
        virtual long increment() = 0;
        virtual long decrement() = 0;
    };
    
    class _SharedBasePtr final
    {
        _SharedBase* base_ptr_ = nullptr;
    public:
        _SharedBasePtr() = default;
        _SharedBasePtr(const _SharedBasePtr &other)
        {
            *this = other.base_ptr_;
        }
        _SharedBasePtr(_SharedBasePtr &&other) : base_ptr_(other.base_ptr_)
        {
            other.base_ptr_ = nullptr;
        }
        _SharedBasePtr(_SharedBase *ptr)
        {
            *this = ptr;
        }
        
        ~_SharedBasePtr()
        {
            reset();
        }
        
        _SharedBasePtr& operator=(const _SharedBasePtr &other)
        {
            if (this != &other) {
                *this = other.base_ptr_;
            }
            return *this;
        }
        _SharedBasePtr& operator=(_SharedBasePtr &&other)
        {
            if (this != &other) {
                base_ptr_ = other.base_ptr_;
                other.base_ptr_ = nullptr;
            }
            return *this;
        }
        _SharedBasePtr& operator=(_SharedBase *ptr)
        {
            if (base_ptr_) {
                base_ptr_->decrement();
            }
            base_ptr_ = ptr;
            if (base_ptr_) {
                base_ptr_->increment();
            }
            return *this;
        }
        operator bool () const
        {
            return base_ptr_;
        }
        
        void reset()
        {
            *this = nullptr;
        }
        void reset(_SharedBase *ptr)
        {
            *this = ptr;
        }
    };
    
    template<typename MyDeleter, typename DataDeleter>
    class _SharedData final : public _SharedBase
    {
    public:
        _SharedData(char *d, size_t s, size_t o, size_t alloc_size, MyDeleter md, DataDeleter dd)
        : data_(d), size_(s), offset_(o), alloc_size_(alloc_size)
        , deleter_(std::move(md)), data_deleter_(std::move(dd))
        {
            if (offset_ > size_) {
                offset_ = size_;
            }
        }
        ~_SharedData()
        {
            if (data_) {
                data_deleter_(data_, size_);
                data_ = nullptr;
            }
        }
        
        char* data() override
        {
            if (data_) {
                return data_ + offset_;
            } else {
                return nullptr;
            }
        }
        
        size_t size() const override
        {
            return size_ - offset_;
        }
        
        long increment() override
        {
            return ++ref_count_;
        }
        
        long decrement() override
        {
            long tmp = --ref_count_;
            if (tmp == 0){
                this->~_SharedData();
                deleter_(this, alloc_size_);
            }
            return tmp;
        }
        
    private:
        char* data_ = nullptr;
        size_t size_ = 0;
        size_t offset_ = 0;
        
        std::atomic_long ref_count_{0};
        
        size_t alloc_size_ = 0;
        MyDeleter deleter_;
        DataDeleter data_deleter_;
    };
}

KUMA_NS_BEGIN

using IOVEC = std::vector<iovec>;
//////////////////////////////////////////////////////////////////////////
// class KMBuffer
class KMBuffer
{
public:
    enum {
        KMB_FLAG_NONE            = 0,
        KMB_FLAG_LIFCYC_STACK    = 0x01,
    };
    using KMB_FLAG = uint32_t;

public:
    KMBuffer(KMB_FLAG flags = KMB_FLAG_NONE) : flags_(flags) {};

    KMBuffer(KMBuffer &&other)
    : flags_(other.flags_), begin_ptr_(other.begin_ptr_), end_ptr_(other.end_ptr_)
    , rd_ptr_(other.rd_ptr_), wr_ptr_(other.wr_ptr_), shared_data_(std::move(other.shared_data_))
    {
        other.flags_ = KMB_FLAG_NONE;
        other.begin_ptr_ = nullptr;
        other.end_ptr_ = nullptr;
        other.rd_ptr_ = nullptr;
        other.wr_ptr_ = nullptr;
        
        if (other.prev_ != &other) {
            next_ = other.next_;
            next_->prev_ = this;
            other.next_ = &other;
            
            prev_ = other.prev_;
            prev_->next_ = this;
            other.prev_ = &other;
        }
    }
    
    template<typename Allocator>
    KMBuffer(size_t len, Allocator &a)
    {
        allocBuffer(len, a);
    }
    
    KMBuffer(size_t len)
    {
        allocBuffer(len);
    }
    
    template<typename DataDeleter> // DataDeleter = void(void*, size_t)
    KMBuffer(void *data, size_t len, size_t offset, DataDeleter &dd)
    {
        std::allocator<char> a;
        auto deleter = [a](void *ptr, size_t size) {
            std::allocator<char> a1 = a;
            a1.deallocate((char*)ptr, size);
        };
        using _MySharedData = _SharedData<decltype(deleter), DataDeleter>;
        size_t shared_size = sizeof(_MySharedData);
        size_t alloc_size = shared_size;
        auto buf = a.allocate(alloc_size);
        auto *sd = new (buf) _MySharedData(static_cast<char*>(data), len, offset, alloc_size, deleter, dd);
        shared_data_ = sd;
        
        begin_ptr_ = static_cast<char*>(data);
        end_ptr_ = begin_ptr_ + len;
        rd_ptr_ = wr_ptr_ = begin_ptr_ + offset;
    }
    
    KMBuffer(void *data, size_t len, KMB_FLAG flags=KMB_FLAG_LIFCYC_STACK)
    : flags_(flags)
    {// we are not the owner of data
        begin_ptr_ = static_cast<char*>(data);
        end_ptr_ = begin_ptr_ + len;
        rd_ptr_ = wr_ptr_ = begin_ptr_;
    }
    
    virtual ~KMBuffer()
    {
        while (next_ != this) {
            next_->releaseSelf();
        }
        shared_data_.reset();
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
        prev_ = next_ = this;
    }
    
    template<typename Allocator>
    bool allocBuffer(size_t len, Allocator &a)
    {
        shared_data_.reset();
        static auto null_deleter = [](void*, size_t){};
        auto deleter = [a](void *ptr, size_t size) {
            Allocator a1 = a;
            a1.deallocate((typename Allocator::pointer)ptr, size);
        };
        using _MySharedData = _SharedData<decltype(deleter), decltype(null_deleter)>;
        size_t shared_size = sizeof(_MySharedData);
        size_t alloc_size = len + shared_size;
        auto buf = a.allocate(alloc_size);
        auto data = buf + shared_size;
        auto *sd = new (buf) _MySharedData(data, len, 0, alloc_size, deleter, null_deleter);
        shared_data_ = sd;
        
        begin_ptr_ = static_cast<char*>(data);
        end_ptr_ = begin_ptr_ + len;
        rd_ptr_ = wr_ptr_ = begin_ptr_;
        
        return true;
    }
    
    bool allocBuffer(size_t len)
    {
        std::allocator<char> a;
        return allocBuffer(len, a);
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
    
    size_t read(void *buf, size_t len)
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
    
    size_t write(const void *buf, size_t len)
    {
        size_t ret = space();
        if(0 == ret) return 0;
        ret = ret > len ? len:ret;
        memcpy(wr_ptr_, buf, ret);
        wr_ptr_ += ret;
        return ret;
    }
    
    char* readPtr() const { return rd_ptr_; }
    char* writePtr() const { return wr_ptr_; }

    bool isChained() const { return next_ != this; }

    void bytesRead(size_t len)
    {
        auto *kmb = this;
        do {
            if (len <= length()) {
                rd_ptr_ += len;
                break;
            } else {
                rd_ptr_ = wr_ptr_;
            }
            kmb = kmb->next_;
        } while (kmb != this);
    }
    
    void bytesWritten(size_t len)
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
        auto *kmb = this;
        do {
            total_len += kmb->length();
            kmb = kmb->next_;
        } while (kmb != this);
        
        return total_len;
    }
    
    size_t readChained(void *buf, size_t len)
    {
        auto *kmb = this;
        auto *ptr = static_cast<char*>(buf);
        size_t total_read = 0;
        do {
            total_read += kmb->read(ptr ? (ptr+total_read) : nullptr, len-total_read);
            if(len == total_read) {
                break;
            }
            kmb = kmb->next_;
        } while (kmb != this);
        
        return total_read;
    }
    
    void append(KMBuffer *kmb)
    {
        auto my_tail = prev_;
        auto kmb_tail = kmb->prev_;
        my_tail->next_ = kmb;
        kmb->prev_ = my_tail;
        kmb_tail->next_ = this;
        prev_ = kmb_tail;
    }

    KMBuffer* clone() const
    {
        auto *dup = cloneSelf();
        auto *kmb = next_;
        while (kmb != this) {
            dup->append(kmb->cloneSelf());
            kmb = kmb->next_;
        }
        return dup;
    }

    KMBuffer* subbuffer(size_t offset, size_t len) const
    {
        if (len == 0) {
            return nullptr;
        }
        KMBuffer *dup = nullptr;
        auto *kmb = this;
        do {
            auto kmb_len = kmb->length();
            if(offset < kmb_len) {
                size_t copy_len = offset+len <= kmb_len ? len : kmb_len - offset;
                KMBuffer *dd = nullptr;
                if(!shared_data_) {
                    KMB_FLAG flags = flags_;
                    flags &= ~KMB_FLAG_LIFCYC_STACK;
                    dd = new KMBuffer(flags);
                    std::allocator<char> a;
                    dd->allocBuffer(copy_len, a);
                    dd->write(kmb->readPtr() + offset, copy_len);
                } else {
                    dd = kmb->cloneSelf();
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
                offset -= kmb_len;
            }
            kmb = kmb->next_;
        } while (kmb != this && len > 0);
        
        return dup;
    }
    
    void reclaim(){
        if(length() > 0) {
            return ;
        }
        while(next_ != this && next_->length() == 0) {
            next_->releaseSelf();
        }
        shared_data_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
    }
    
    int fillIov(IOVEC& iovs) const {
        int cnt = 0;
        auto *kmb = this;
        do {
            if (kmb->length() > 0) {
                iovec v;
                v.iov_base = (char*)kmb->readPtr();
                v.iov_len = kmb->length();
                iovs.push_back(v);
                ++cnt;
            }
            kmb = kmb->next_;
        } while (kmb != this);

        return cnt;
    }
    
    void release()
    {
        while (next_ != this) {
            next_->releaseSelf();
        }
        releaseSelf();
    }

private:
    KMBuffer &operator= (const KMBuffer &) = delete;
    KMBuffer (const KMBuffer &) = delete;

    KMBuffer* cloneSelf() const {
        KMBuffer* dup = nullptr;
        KMB_FLAG flags = flags_;
        flags &= ~KMB_FLAG_LIFCYC_STACK;
        if (!shared_data_) {
            dup = new KMBuffer(flags);
            if (length() > 0) {
                std::allocator<char> a;
                if(dup->allocBuffer(length(), a)) {
                    dup->write(readPtr(), length());
                }
            }
        } else {
            dup = new KMBuffer(flags);
            dup->shared_data_ = shared_data_;
            dup->begin_ptr_ = begin_ptr_;
            dup->end_ptr_ = end_ptr_;
            dup->rd_ptr_ = rd_ptr_;
            dup->wr_ptr_ = wr_ptr_;
        }

        return dup;
    }
    virtual void releaseSelf() {
        next_->prev_ = prev_;
        prev_->next_ = next_;
        prev_ = next_ = this;
        
        shared_data_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
        if(!(flags_ & KMB_FLAG_LIFCYC_STACK)) {
            delete this;
        }
    }

private:
    KMB_FLAG flags_{ KMB_FLAG_NONE };
    char* begin_ptr_{ nullptr };
    char* end_ptr_{ nullptr };
    char* rd_ptr_{ nullptr };
    char* wr_ptr_{ nullptr };
    _SharedBasePtr shared_data_;

    KMBuffer* prev_{ this };
    KMBuffer* next_{ this };
};

KUMA_NS_END

#endif
