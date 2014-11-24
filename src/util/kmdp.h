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
#include "refcount.h"
#include <vector>

namespace komm {;
struct IOV {
#ifdef KUMA_OS_WIN
    unsigned long	iov_len;    /* Length. */
#endif
    char*			iov_base;  /* Base address. */
#if defined(KUMA_OS_LINUX) || defined(KUMA_OS_MACOS)
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
    class KM_Data_Block : public KM_RefCount
    {
    public:
        KM_Data_Block(unsigned char* buf, unsigned int len, unsigned int offset)
        {
            m_buffer = buf;
            m_length = len;
            m_offset = offset;
        }
        ~KM_Data_Block()
        {
            if(m_buffer)
            {
                delete[] m_buffer;
                m_buffer = NULL;
                m_length = 0;
                m_offset = 0;
            }
        }
        unsigned char* get_buffer() { return m_buffer+m_offset; }
        unsigned int get_length() { return m_length-m_offset; }
        void detach_buffer(unsigned char*& buf, unsigned int& len, unsigned int& offset)
        {
            buf = m_buffer;
            len = m_length;
            offset = m_offset;
            m_offset = 0;
            m_length = 0;
            m_buffer = NULL;
        }

    private:
        KM_Data_Block();
        KM_Data_Block &operator= (const KM_Data_Block &);
        KM_Data_Block (const KM_Data_Block &);

    private:
        unsigned char* m_buffer;
        unsigned int m_length;
        unsigned int m_offset;
    };
public:
    KM_Data_Packet(DP_FLAG flag=0)
    {
        m_flag = flag;
        m_begin_ptr = m_end_ptr = NULL;
        m_rd_ptr = m_wr_ptr = NULL;
        m_data_block = NULL;
        m_next = NULL;
    }
    KM_Data_Packet(unsigned char* buf, unsigned int len, unsigned int offset=0, DP_FLAG flag=DP_FLAG_LIFCYC_STACK|DP_FLAG_DONT_DELETE)
    {
        m_flag = flag;
        m_begin_ptr = m_end_ptr = NULL;
        m_rd_ptr = m_wr_ptr = NULL;
        m_data_block = NULL;
        m_next = NULL;
        if(m_flag&DP_FLAG_DONT_DELETE)
        {
            if(offset < len)
            {
                m_begin_ptr = buf;
                m_end_ptr = m_begin_ptr + len;
                m_rd_ptr = m_begin_ptr + offset;
                m_wr_ptr = m_begin_ptr + offset;
            }
        }
        else
        {// we own the buffer
            if(offset > len) offset = len;
            m_data_block = new KM_Data_Block(buf, len, offset);
            m_data_block->add_reference();
            m_begin_ptr = buf;
            m_end_ptr = m_begin_ptr + len;
            m_rd_ptr = m_begin_ptr + offset;
            m_wr_ptr = m_begin_ptr + offset;
        }
    }
    virtual ~KM_Data_Packet()
    {
        if(m_next)
        {
            m_next->release();
            m_next = NULL;
        }
        if(!(m_flag&DP_FLAG_DONT_DELETE) && m_data_block)
        {
            m_data_block->release_reference();
            m_data_block = NULL;
        }
        m_begin_ptr = m_end_ptr = NULL;
        m_rd_ptr = m_wr_ptr = NULL;
    }

    virtual bool alloc_buffer(unsigned int len)
    {
        if(0 == len) return false;
        unsigned char* buf = new unsigned char[len];
        if(NULL == buf)
            return false;
        m_flag &= ~DP_FLAG_DONT_DELETE;
        m_data_block = new KM_Data_Block(buf, len, 0);
        m_data_block->add_reference();
        m_begin_ptr = buf;
        m_end_ptr = m_begin_ptr + len;
        m_rd_ptr = m_begin_ptr;
        m_wr_ptr = m_begin_ptr;
        return true;
    }
    virtual bool attach_buffer(unsigned char* buf, unsigned int len, unsigned int offset=0)
    {
        if(offset >= len)
            return false;
        if(!(m_flag&DP_FLAG_DONT_DELETE) && m_data_block)
            m_data_block->release_reference();
        m_flag &= ~DP_FLAG_DONT_DELETE;
        m_data_block = new KM_Data_Block(buf, len, offset);
        m_data_block->add_reference();
        m_begin_ptr = buf;
        m_end_ptr = m_begin_ptr + len;
        m_rd_ptr = m_begin_ptr + offset;
        m_wr_ptr = m_end_ptr;
        return true;
    }

    // don't call detach_buffer if KM_Data_Packet is duplicated
    virtual void detach_buffer(unsigned char*& buf, unsigned int& len, unsigned int& offset)
    {
        buf = NULL;
        len = 0;
        offset = 0;
        if(!(m_flag&DP_FLAG_DONT_DELETE) && m_data_block)
        {
            m_data_block->detach_buffer(buf, len, offset);
            m_data_block->release_reference();
            offset = (unsigned int)(m_rd_ptr - buf);
        }
        buf = m_begin_ptr;
        offset = (unsigned int)(m_rd_ptr - m_begin_ptr);
        len = (unsigned int)(m_end_ptr - m_begin_ptr);
        m_data_block = NULL;
        m_begin_ptr = m_end_ptr = NULL;
        m_rd_ptr = m_wr_ptr = NULL;
    }

    unsigned int space()
    {
        if(m_wr_ptr > m_end_ptr) return 0;
        return (unsigned int)(m_end_ptr - m_wr_ptr);
    }
    unsigned int length()
    {
        if(m_rd_ptr > m_wr_ptr) return 0;
        return (unsigned int)(m_wr_ptr - m_rd_ptr);
    }
    unsigned int read(unsigned char* buf, unsigned int len)
    {
        unsigned int ret = length();
        if(0 == ret) return 0;
        ret = ret>len?len:ret;
        if(buf)
            memcpy(buf, m_rd_ptr, ret);
        m_rd_ptr += ret;
        return ret;
    }
    unsigned int write(const unsigned char* buf, unsigned int len)
    {
        unsigned int ret = space();
        if(0 == ret) return 0;
        ret = ret>len?len:ret;
        memcpy(m_wr_ptr, buf, ret);
        m_wr_ptr += ret;
        return ret;
    }
    unsigned char* rd_ptr() { return m_rd_ptr; }
    unsigned char* wr_ptr() { return m_wr_ptr; }
    KM_Data_Packet* next() { return m_next; }

    void adv_rd_ptr(unsigned int len)
    {
        if(len > length())
        {
            if(m_next) m_next->adv_rd_ptr(len-length());
            m_rd_ptr = m_wr_ptr;
        }
        else
            m_rd_ptr += len;
    }
    void adv_wr_ptr(unsigned int len)
    {
        if(space() == 0)
            return ;
        if(len > space())
            m_wr_ptr = m_end_ptr;
        else
            m_wr_ptr += len;
    }

    unsigned int total_length()
    {
        if(m_next)
            return length()+m_next->total_length();
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
        while(tail->m_next)
            tail = tail->m_next;

        tail->m_next = dp;
    }

    virtual KM_Data_Packet* duplicate()
    {
        KM_Data_Packet* dup = duplicate_self();
        if(m_next)
            dup->m_next = m_next->duplicate();
        return dup;
    }

    virtual KM_Data_Packet* subpacket(unsigned int offset, unsigned int len)
    {
        if(offset < length())
        {
            unsigned int left_len = 0;
            KM_Data_Packet* dup = NULL;
            if(m_flag&DP_FLAG_DONT_DELETE)
            {
                DP_FLAG flag = m_flag;
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
                dup->m_rd_ptr += offset;
                if(offset+len <= length())
                {
                    dup->m_wr_ptr = dup->m_rd_ptr+len;
                }
                else
                {
                    left_len = offset+len-length();
                }
            }
            if(m_next && left_len>0)
                dup->m_next = m_next->subpacket(0, left_len);
            return dup;
        }
        else if(m_next)
            return m_next->subpacket(offset-length(), len);
        else
            return NULL;
    }
    virtual void reclaim(){
        if(length() > 0)
            return ;
        KM_Data_Packet* dp = m_next;
        while(dp && dp->length() == 0)
        {
            KM_Data_Packet* tmp = dp->m_next;
            dp->m_next = NULL;
            dp->release();
            dp = tmp;
        }
        if(!(m_flag&DP_FLAG_DONT_DELETE) && m_data_block)
        {
            m_data_block->release_reference();
            m_data_block = NULL;
        }
        m_begin_ptr = m_end_ptr = NULL;
        m_rd_ptr = m_wr_ptr = NULL;
        m_next = dp;
        /*
        KM_Data_Packet* dp = this;
        while(dp && dp->length() == 0)
        {
        if(!(dp->m_flag&DP_FLAG_DONT_DELETE) && dp->m_data_block)
        {
        dp->m_data_block->release_reference();
        dp->m_data_block = NULL;
        }
        dp->m_begin_ptr = dp->m_end_ptr = NULL;
        dp->m_rd_ptr = dp->m_wr_ptr = NULL;
        dp = dp->m_next;
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
        if(m_next)
        {
            m_next->release();
            m_next = NULL;
        }
        if(!(m_flag&DP_FLAG_DONT_DELETE) && m_data_block)
        {
            m_data_block->release_reference();
            m_data_block = NULL;
        }
        m_begin_ptr = m_end_ptr = NULL;
        m_rd_ptr = m_wr_ptr = NULL;
        if(!(m_flag&DP_FLAG_LIFCYC_STACK))
            delete this;
    }

private:
    KM_Data_Packet &operator= (const KM_Data_Packet &);
    KM_Data_Packet (const KM_Data_Packet &);

    KM_Data_Block* data_block() { return m_data_block; }
    void data_block(KM_Data_Block* db) {
        if(m_data_block)
            m_data_block->release_reference();
        m_data_block = db;
        if(m_data_block)
            m_data_block->add_reference();
    }
    virtual KM_Data_Packet* duplicate_self(){
        KM_Data_Packet* dup = NULL;
        if(m_flag&DP_FLAG_DONT_DELETE)
        {
            DP_FLAG flag = m_flag;
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
            DP_FLAG flag = m_flag;
            flag &= ~DP_FLAG_LIFCYC_STACK;
            dup = new KM_Data_Packet(flag);
            dup->m_data_block = m_data_block;
            if(dup->m_data_block)
                dup->m_data_block->add_reference();
            dup->m_begin_ptr = m_begin_ptr;
            dup->m_end_ptr = m_end_ptr;
            dup->m_rd_ptr = m_rd_ptr;
            dup->m_wr_ptr = m_wr_ptr;
        }
        return dup;
    }

private:
    DP_FLAG	m_flag;
    unsigned char* m_begin_ptr;
    unsigned char* m_end_ptr;
    unsigned char* m_rd_ptr;
    unsigned char* m_wr_ptr;
    KM_Data_Block* m_data_block;

    KM_Data_Packet* m_next;
};

}

#endif
