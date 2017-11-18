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
#include "kmdefs.h"
#include <memory>
#include <vector>
#include <atomic>

#ifndef KUMA_OS_WIN
#include <sys/uio.h> // for struct iovec
#include <string.h> // for memcpy
#endif

namespace {
    class _SharedBase
    {
    public:
        virtual ~_SharedBase() {}
        virtual void* data() = 0;
        virtual const void* data() const = 0;
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
            if (ptr) {
                ptr->increment();
            }
            if (base_ptr_) {
                base_ptr_->decrement();
            }
            base_ptr_ = ptr;
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
    
    template<typename Deleter, typename DataDeleter>
    class _SharedData final : public _SharedBase
    {
    public:
        _SharedData(void *data, size_t size, size_t alloc_size, Deleter md, DataDeleter dd)
        : data_(data), size_(size), alloc_size_(alloc_size)
        , deleter_(std::move(md)), data_deleter_(std::move(dd))
        {
            
        }
        ~_SharedData()
        {
            if (data_) {
                data_deleter_(data_, size_);
                data_ = nullptr;
            }
        }
        
        void* data() override
        {
            return data_;
        }
        
        const void* data() const override
        {
            return data_;
        }
        
        size_t size() const override
        {
            return size_;
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
        void* data_ = nullptr;
        size_t size_ = 0;
        
        std::atomic_long ref_count_{0};
        
        size_t alloc_size_ = 0;
        Deleter deleter_;
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
    class Iterator;
    enum class StorageType
    {
        AUTO,   // KMBuffer is auto storage, don't call delete when destroy
        OTHER
    };
    KMBuffer(StorageType type = StorageType::OTHER) : storage_type_(type) {}

    KMBuffer(const KMBuffer &other)
    {
        *this = other;
    }
    
    KMBuffer(KMBuffer &&other)
    {
        *this = std::move(other);
    }
    
    template<typename Allocator>
    KMBuffer(size_t size, Allocator &a)
    {
        allocBuffer(size, a);
    }
    
    KMBuffer(size_t size)
    {
        allocBuffer(size);
    }
    
    template<typename DataDeleter> // DataDeleter = void(void*, size_t)
    KMBuffer(void *data, size_t capacity, size_t size, size_t offset, DataDeleter &dd)
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
        auto *sd = new (buf) _MySharedData(data, capacity, alloc_size, deleter, dd);
        shared_data_ = sd;
        
        begin_ptr_ = static_cast<char*>(data);
        end_ptr_ = begin_ptr_ + capacity;
        rd_ptr_ = begin_ptr_ + offset;
        wr_ptr_ = rd_ptr_ + size;
        if (rd_ptr_ > end_ptr_) {
            rd_ptr_ = end_ptr_;
        }
        if (wr_ptr_ > end_ptr_) {
            wr_ptr_ = end_ptr_;
        }
    }
    
    /**
     * data is not owned by this KMBuffer, caller should make sure data is valid
     * before this KMBuffer destroyed
     */
    KMBuffer(void *data, size_t capacity, size_t size=0, StorageType type = StorageType::AUTO)
    : storage_type_(type)
    {
        if (size > capacity) {
            size = capacity;
        }
        begin_ptr_ = static_cast<char*>(data);
        end_ptr_ = begin_ptr_ + capacity;
        rd_ptr_ = begin_ptr_;
        wr_ptr_ = rd_ptr_ + size;
    }
    
    KMBuffer(const void *data, size_t capacity, size_t size=0, StorageType type = StorageType::AUTO)
    : KMBuffer(const_cast<void*>(data), capacity, size, type)
    {
    }
    
    ~KMBuffer()
    {
        reset();
    }
    
    template<typename Allocator>
    bool allocBuffer(size_t size, Allocator &a)
    {
        shared_data_.reset();
        static auto null_deleter = [](void*, size_t){};
        auto deleter = [a](void *ptr, size_t size) {
            Allocator a1 = a;
            a1.deallocate((typename Allocator::pointer)ptr, size);
        };
        using _MySharedData = _SharedData<decltype(deleter), decltype(null_deleter)>;
        size_t shared_size = sizeof(_MySharedData);
        size_t alloc_size = size + shared_size;
        auto buf = a.allocate(alloc_size);
        auto data = buf + shared_size;
        auto *sd = new (buf) _MySharedData(data, size, alloc_size, deleter, null_deleter);
        shared_data_ = sd;
        
        begin_ptr_ = static_cast<char*>(data);
        end_ptr_ = begin_ptr_ + size;
        rd_ptr_ = wr_ptr_ = begin_ptr_;
        
        return true;
    }
    
    bool allocBuffer(size_t size)
    {
        std::allocator<char> a;
        return allocBuffer(size, a);
    }
    
    KMBuffer& operator= (const KMBuffer &other)
    {
        if (this != &other) {
            reset();
            other.cloneSelf(*this);
            auto *kmb = other.next_;
            while (kmb != &other) {
                append(kmb->cloneSelf());
                kmb = kmb->next_;
            }
        }
        return *this;
    }
    
    KMBuffer& operator= (KMBuffer &&other)
    {
        if (this != &other) {
            reset();
            begin_ptr_ = other.begin_ptr_;
            end_ptr_ = other.end_ptr_;
            rd_ptr_ = other.rd_ptr_;
            wr_ptr_ = other.wr_ptr_;
            shared_data_ = std::move(other.shared_data_);
            
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
        return *this;
    }

    size_t space() const
    {
        if(wr_ptr_ > end_ptr_) return 0;
        return end_ptr_ - wr_ptr_;
    }
    
    size_t length() const
    {
        return size();
    }
    
    size_t size() const
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
    
    size_t read(void *buf, size_t len) const
    {
        size_t ret = length();
        if(0 == ret) return 0;
        ret = ret>len?len:ret;
        if(buf) {
            memcpy(buf, rd_ptr_, ret);
        }
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
            if (len <= kmb->length()) {
                kmb->rd_ptr_ += len;
                break;
            } else {
                len -= kmb->length();
                kmb->rd_ptr_ = kmb->wr_ptr_;
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

    size_t chainLength() const
    {
        size_t chain_size = 0;
        auto *kmb = this;
        do {
            chain_size += kmb->length();
            kmb = kmb->next_;
        } while (kmb != this);
        
        return chain_size;
    }
    
    bool empty() const
    {
        auto *kmb = this;
        do {
            if (kmb->length() > 0) {
                return false;
            }
            kmb = kmb->next_;
        } while (kmb != this);
        return true;
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
    
    size_t readChained(void *buf, size_t len) const
    {
        auto const *kmb = this;
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
    
    /**
     * Append kmb to the end of this KMBuffer chain
     * kmb will be owned by this chain
     */
    void append(KMBuffer *kmb)
    {
        if (kmb) {
            auto my_tail = prev_;
            auto kmb_tail = kmb->prev_;
            my_tail->next_ = kmb;
            kmb->prev_ = my_tail;
            kmb_tail->next_ = this;
            prev_ = kmb_tail;
            kmb->is_chain_head_ = false;
        }
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
                    dd = new KMBuffer();
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
    
    void reclaim()
    {
        if(length() > 0) {
            return ;
        }
        while(next_ != this && next_->length() == 0) {
            next_->destroySelf();
        }
        shared_data_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
    }
    
    int fillIov(IOVEC& iovs) const
    {
        int cnt = 0;
        auto *kmb = this;
        do {
            if (kmb->length() > 0) {
                iovec v;
                v.iov_base = (char*)kmb->readPtr();
                v.iov_len = static_cast<decltype(v.iov_len)>(kmb->length());
                iovs.emplace_back(v);
                ++cnt;
            }
            kmb = kmb->next_;
        } while (kmb != this);

        return cnt;
    }
    
    void unlink()
    {
        if (is_chain_head_ && next_ != this) {
            next_->is_chain_head_ = true;
        }
        next_->prev_ = prev_;
        prev_->next_ = next_;
        prev_ = next_ = this;
        is_chain_head_ = true;
    }
    
    void reset()
    {
        if (is_chain_head_) {
            // destroy whole buffer chain
            while (next_ != this) {
                next_->destroySelf();
            }
        }
        resetSelf();
    }
    
    void destroy()
    {
        reset();
        if(storage_type_ != StorageType::AUTO) {
            delete this;
        }
    }

private:
    KMBuffer* cloneSelf() const
    {
        KMBuffer* kmb = new KMBuffer();
        cloneSelf(*kmb);
        return kmb;
    }
    
    void cloneSelf(KMBuffer &buf) const
    {
        if (!shared_data_) {
            if (length() > 0 && buf.allocBuffer(length())) {
                buf.write(readPtr(), length());
            }
        } else {
            buf.shared_data_ = shared_data_;
            buf.begin_ptr_ = begin_ptr_;
            buf.end_ptr_ = end_ptr_;
            buf.rd_ptr_ = rd_ptr_;
            buf.wr_ptr_ = wr_ptr_;
        }
    }
    
    /**
     * don't call resetSelf on chained head
     */
    void resetSelf()
    {
        // unlink form buffer chain
        next_->prev_ = prev_;
        prev_->next_ = next_;
        prev_ = next_ = this;
        
        shared_data_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
        is_chain_head_ = true;
    }
    
    /**
     * don't call destroySelf on chained head
     */
    void destroySelf()
    {
        resetSelf();
        if(storage_type_ != StorageType::AUTO) {
            delete this;
        }
    }

private:
    StorageType storage_type_{ StorageType::OTHER };
    char* begin_ptr_{ nullptr };
    char* end_ptr_{ nullptr };
    char* rd_ptr_{ nullptr };
    char* wr_ptr_{ nullptr };
    bool is_chain_head_{ true };
    _SharedBasePtr shared_data_;

    KMBuffer* prev_{ this };
    KMBuffer* next_{ this };
    
public:
    class Iterator : public std::iterator<std::forward_iterator_tag, KMBuffer>
    {
    public:
        Iterator(const KMBuffer* pos, const KMBuffer* end)
        : pos_(pos), end_(end)
        {
            
        }
        Iterator(const Iterator &other)
        : pos_(other.pos_), end_(other.end_)
        {
            
        }
        
        Iterator& operator++()
        {
            pos_ = pos_->next_;
            if (pos_ == end_) {
                pos_ = nullptr;
                end_ = nullptr;
            }
            return *this;
        }
        Iterator operator++(int)
        {
            Iterator ret(*this);
            ++(*this);
            return ret;
        }
        
        const KMBuffer& operator*() const
        {
            return *pos_;
        }
        const KMBuffer* operator->() const
        {
            return pos_;
        }
        
        friend bool operator==(const Iterator& it1, const Iterator& it2)
        {
            return it1.pos_ == it2.pos_ && it1.end_ == it2.end_;
        }
        friend bool operator!=(const Iterator& it1, const Iterator& it2)
        {
            return !(it1 == it2);
        }
        
    protected:
        const KMBuffer* pos_{nullptr};
        const KMBuffer* end_{nullptr};
    };
    
    Iterator begin() const
    {
        return Iterator(this, this);
    }
    Iterator end() const
    {
        return Iterator(nullptr, nullptr);
    }
    
    struct KMBufferDeleter
    {
        void operator() (KMBuffer *kmb)
        {
            if (kmb) {
                kmb->destroy();
            }
        }
    };
    using Ptr = std::unique_ptr<KMBuffer, KMBufferDeleter>;
};

KUMA_NS_END

#endif
