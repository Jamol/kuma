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

#ifndef __KM_DP_H__
#define __KM_DP_H__
#include "kmconf.h"
#include "kmdefs.h"
#include <vector>

KUMA_NS_BEGIN

struct IOV {
#ifdef KUMA_OS_WIN
    unsigned long	iov_len;    /* Length. */
#endif
    char*			iov_base;  /* Base address. */
#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MAC)
    unsigned long	iov_len;    /* Length. */
#endif
};
typedef std::vector<IOV>	IOVEC;

//////////////////////////////////////////////////////////////////////////
// class KM_Data_Packet
class KM_Data_Packet
{
public:
    enum{
        DP_FLAG_DONT_DELETE     = 0x01,
        DP_FLAG_LIFCYC_STACK    = 0x02,
    };
    typedef unsigned int DP_FLAG;

private:
    class Kdata_block_
    {
    public:
        Kdata_block_(unsigned char* buf, unsigned int size, unsigned int offset)
        : buffer_(buf), size_(size), offset_(offset)
        { }
        ~Kdata_block_()
        {
            if(buffer_)
            {
                delete[] buffer_;
                buffer_ = nullptr;
                size_ = 0;
                offset_ = 0;
            }
        }
        unsigned char* get_buffer() { return buffer_+offset_; }
        unsigned int get_length() { return size_-offset_; }
        void detach_buffer(unsigned char*& buf, unsigned int& size, unsigned int& offset)
        {
            buf = buffer_;
            size = size_;
            offset = offset_;
            offset_ = 0;
            size_ = 0;
            buffer_ = nullptr;
        }

    private:
        Kdata_block_();
        Kdata_block_ &operator= (const Kdata_block_ &);
        Kdata_block_ (const Kdata_block_ &);

    private:
        unsigned char* buffer_;
        unsigned int size_;
        unsigned int offset_;
    };
public:
    KM_Data_Packet(DP_FLAG flag=0)
    {
        flag_ = flag;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
        data_block_ = nullptr;
        next_ = nullptr;
    }
    KM_Data_Packet(unsigned char* buf, unsigned int len, unsigned int offset=0, DP_FLAG flag=DP_FLAG_LIFCYC_STACK|DP_FLAG_DONT_DELETE)
    {
        flag_ = flag;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
        data_block_ = nullptr;
        next_ = nullptr;
        if(flag_&DP_FLAG_DONT_DELETE)
        {
            if(offset < len)
            {
                begin_ptr_ = buf;
                end_ptr_ = begin_ptr_ + len;
                rd_ptr_ = begin_ptr_ + offset;
                wr_ptr_ = begin_ptr_ + offset;
            }
        }
        else
        {// we own the buffer
            if(offset > len) offset = len;
            data_block_ = std::make_shared<Kdata_block_>(buf, len, offset);
            begin_ptr_ = buf;
            end_ptr_ = begin_ptr_ + len;
            rd_ptr_ = begin_ptr_ + offset;
            wr_ptr_ = begin_ptr_ + offset;
        }
    }
    virtual ~KM_Data_Packet()
    {
        if(next_)
        {
            next_->release();
            next_ = nullptr;
        }
        data_block_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
    }

    virtual bool alloc_buffer(unsigned int len)
    {
        if(0 == len) return false;
        unsigned char* buf = new unsigned char[len];
        if(NULL == buf)
            return false;
        flag_ &= ~DP_FLAG_DONT_DELETE;
        data_block_ = std::make_shared<Kdata_block_>(buf, len, 0);
        begin_ptr_ = buf;
        end_ptr_ = begin_ptr_ + len;
        rd_ptr_ = begin_ptr_;
        wr_ptr_ = begin_ptr_;
        return true;
    }
    virtual bool attach_buffer(unsigned char* buf, unsigned int len, unsigned int offset=0)
    {
        if(offset >= len) {
            return false;
        }
        flag_ &= ~DP_FLAG_DONT_DELETE;
        data_block_ = std::make_shared<Kdata_block_>(buf, len, offset);
        begin_ptr_ = buf;
        end_ptr_ = begin_ptr_ + len;
        rd_ptr_ = begin_ptr_ + offset;
        wr_ptr_ = end_ptr_;
        return true;
    }

    // don't call detach_buffer if KM_Data_Packet is duplicated
    virtual void detach_buffer(unsigned char*& buf, unsigned int& len, unsigned int& offset)
    {
        buf = NULL;
        len = 0;
        offset = 0;
        if(!(flag_&DP_FLAG_DONT_DELETE) && data_block_)
        {
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

    unsigned int space()
    {
        if(wr_ptr_ > end_ptr_) return 0;
        return (unsigned int)(end_ptr_ - wr_ptr_);
    }
    unsigned int length()
    {
        if(rd_ptr_ > wr_ptr_) return 0;
        return (unsigned int)(wr_ptr_ - rd_ptr_);
    }
    unsigned int read(unsigned char* buf, unsigned int len)
    {
        unsigned int ret = length();
        if(0 == ret) return 0;
        ret = ret>len?len:ret;
        if(buf)
            memcpy(buf, rd_ptr_, ret);
        rd_ptr_ += ret;
        return ret;
    }
    unsigned int write(const unsigned char* buf, unsigned int len)
    {
        unsigned int ret = space();
        if(0 == ret) return 0;
        ret = ret>len?len:ret;
        memcpy(wr_ptr_, buf, ret);
        wr_ptr_ += ret;
        return ret;
    }
    unsigned char* rd_ptr() { return rd_ptr_; }
    unsigned char* wr_ptr() { return wr_ptr_; }
    KM_Data_Packet* next() { return next_; }

    void adv_rd_ptr(unsigned int len)
    {
        if(len > length())
        {
            if(next_) next_->adv_rd_ptr(len-length());
            rd_ptr_ = wr_ptr_;
        }
        else
            rd_ptr_ += len;
    }
    void adv_wr_ptr(unsigned int len)
    {
        if(space() == 0)
            return ;
        if(len > space())
            wr_ptr_ = end_ptr_;
        else
            wr_ptr_ += len;
    }

    unsigned int total_length()
    {
        if(next_)
            return length()+next_->total_length();
        else
            return length();
    }
    unsigned int chain_read(unsigned char* buf, unsigned int len)
    {
        KM_Data_Packet* dp = this;
        unsigned int total_read = 0;
        while(dp)
        {
            total_read += dp->read(NULL==buf?NULL:(buf+total_read), len-total_read);
            if(len == total_read)
                break;
            dp = dp->next();
        }
        return total_read;
    }
    void append(KM_Data_Packet* dp)
    {
        KM_Data_Packet* tail = this;
        while(tail->next_)
            tail = tail->next_;

        tail->next_ = dp;
    }

    virtual KM_Data_Packet* duplicate()
    {
        KM_Data_Packet* dup = duplicate_self();
        if(next_)
            dup->next_ = next_->duplicate();
        return dup;
    }

    virtual KM_Data_Packet* subpacket(unsigned int offset, unsigned int len)
    {
        if(offset < length())
        {
            unsigned int left_len = 0;
            KM_Data_Packet* dup = NULL;
            if(flag_&DP_FLAG_DONT_DELETE)
            {
                DP_FLAG flag = flag_;
                flag &= ~DP_FLAG_DONT_DELETE;
                flag &= ~DP_FLAG_LIFCYC_STACK;
                dup = new KM_Data_Packet(flag);
                if(offset+len <= length())
                {
                    dup->alloc_buffer(len);
                    dup->write(rd_ptr(), len);
                }
                else
                {
                    dup->alloc_buffer(length());
                    dup->write(rd_ptr(), length());
                    left_len = offset+len-length();
                }
            }
            else
            {
                dup = duplicate_self();
                dup->rd_ptr_ += offset;
                if(offset+len <= length())
                {
                    dup->wr_ptr_ = dup->rd_ptr_+len;
                }
                else
                {
                    left_len = offset+len-length();
                }
            }
            if(next_ && left_len>0)
                dup->next_ = next_->subpacket(0, left_len);
            return dup;
        }
        else if(next_)
            return next_->subpacket(offset-length(), len);
        else
            return nullptr;
    }
    virtual void reclaim(){
        if(length() > 0)
            return ;
        KM_Data_Packet* dp = next_;
        while(dp && dp->length() == 0)
        {
            KM_Data_Packet* tmp = dp->next_;
            dp->next_ = nullptr;
            dp->release();
            dp = tmp;
        }
        data_block_ = nullptr;
        begin_ptr_ = end_ptr_ = nullptr;
        rd_ptr_ = wr_ptr_ = nullptr;
        next_ = dp;
        /*
        KM_Data_Packet* dp = this;
        while(dp && dp->length() == 0)
        {
        if(!(dp->flag_&DP_FLAG_DONT_DELETE) && dp->data_block_)
        {
        dp->data_block_->release_reference();
        dp->data_block_ = NULL;
        }
        dp->begin_ptr_ = dp->end_ptr_ = NULL;
        dp->rd_ptr_ = dp->wr_ptr_ = NULL;
        dp = dp->next_;
        }*/
    }
    unsigned int get_iovec(IOVEC& iovs){
        KM_Data_Packet* dp = NULL;
        unsigned int cnt = 0;
        for (dp = this; NULL != dp; dp = dp->next())
        {
            if(dp->length() > 0)
            {
                IOV v;
                v.iov_base = (char*)dp->rd_ptr();
                v.iov_len = dp->length();
                iovs.push_back(v);
                ++cnt;
            }
        }

        return cnt;
    }
    virtual void release()
    {
        if(next_)
        {
            next_->release();
            next_ = NULL;
        }
        data_block_ = nullptr;
        begin_ptr_ = end_ptr_ = NULL;
        rd_ptr_ = wr_ptr_ = NULL;
        if(!(flag_&DP_FLAG_LIFCYC_STACK))
            delete this;
    }

private:
    KM_Data_Packet &operator= (const KM_Data_Packet &);
    KM_Data_Packet (const KM_Data_Packet &);

    std::shared_ptr<Kdata_block_>& data_block() { return data_block_; }
    void data_block(std::shared_ptr<Kdata_block_>& db) {
        data_block_ = db;
    }
    virtual KM_Data_Packet* duplicate_self(){
        KM_Data_Packet* dup = NULL;
        if(flag_&DP_FLAG_DONT_DELETE)
        {
            DP_FLAG flag = flag_;
            flag &= ~DP_FLAG_DONT_DELETE;
            flag &= ~DP_FLAG_LIFCYC_STACK;
            dup = new KM_Data_Packet(flag);
            if(dup->alloc_buffer(length()))
            {
                dup->write(rd_ptr(), length());
            }
        }
        else
        {
            DP_FLAG flag = flag_;
            flag &= ~DP_FLAG_LIFCYC_STACK;
            dup = new KM_Data_Packet(flag);
            dup->data_block_ = data_block_;
            dup->begin_ptr_ = begin_ptr_;
            dup->end_ptr_ = end_ptr_;
            dup->rd_ptr_ = rd_ptr_;
            dup->wr_ptr_ = wr_ptr_;
        }
        return dup;
    }

private:
    DP_FLAG	flag_;
    unsigned char* begin_ptr_;
    unsigned char* end_ptr_;
    unsigned char* rd_ptr_;
    unsigned char* wr_ptr_;
    std::shared_ptr<Kdata_block_> data_block_;

    KM_Data_Packet* next_;
};

KUMA_NS_END

#endif
