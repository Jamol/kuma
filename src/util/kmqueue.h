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

#ifndef __KMQUEUE_H__
#define __KMQUEUE_H__

#include "kmdefs.h"
#include <type_traits>

KUMA_NS_BEGIN

template <class E>
class KM_Queue
{
public:
    KM_Queue()
    {
        head_ = new TLNode(E());
        tail_ = head_;
    }
    ~KM_Queue()
    {
        TLNode* node = nullptr;
        while(head_) {
            node = head_;
            head_ = head_->next_;
            delete node;
        }
    }

    void enqueue(const E &element)
    {
        TLNode* node = new TLNode(element);
        tail_->next_ = node;
        tail_ = node;
    }
    
    void enqueue(const E &&element)
    {
        TLNode* node = new TLNode(std::forward(element));
        tail_->next_ = node;
        tail_ = node;
    }

    bool dequeue(E &element)
    {
        if(empty()) {
            return false;
        }
        TLNode* node_to_delete = head_;
        element = std::move(head_->next_->element_);
        head_ = head_->next_;
        delete node_to_delete;
        return true;
    }
    
    bool empty()
    {
        return nullptr == head_->next_;
    }

private:
    class TLNode
    {
    public:
        TLNode(const E &e) : element_(e), next_(nullptr) {}

        E element_;
        TLNode* next_;
    };

    TLNode* head_;
    TLNode* tail_;
};

template <class E, class LockType>
class KM_QueueMT
{
public:
    KM_QueueMT()
    {
        head_ = new TLNode(E());
        tail_ = head_;
        en_count_ = 0;
        de_count_ = 0;
    }
    ~KM_QueueMT()
    {
        TLNode* node = nullptr;
        while(head_)
        {
            node = head_;
            head_ = head_->next_;
            delete node;
        }
    }

    void enqueue(const E &element)
    {
        TLNode* node = new TLNode(element);
        lockerT_.lock();
        tail_->next_ = node;
        tail_ = node;
        ++en_count_;
        lockerT_.unlock();
    }
    
    void enqueue(E &&element)
    {
        TLNode* node = new TLNode(std::forward<E>(element));
        lockerT_.lock();
        tail_->next_ = node;
        tail_ = node;
        ++en_count_;
        lockerT_.unlock();
    }

    bool dequeue(E &element)
    {
        if(empty()) {
            return false;
        }
        TLNode* node_to_delete = nullptr;
        lockerH_.lock();
        if(empty()) {
            lockerH_.unlock();
            return false;
        }
        node_to_delete = head_;
        element = std::move(head_->next_->element_);
        head_ = head_->next_;
        ++de_count_;
        lockerH_.unlock();
        delete node_to_delete;
        return true;
    }
    
    bool empty()
    {
        return nullptr == head_->next_;
    }
    
    unsigned int size()
    {
        return en_count_ - de_count_;
    }

private:
    class TLNode
    {
    public:
        TLNode(const E &e) : element_(e)
        {
            next_ = nullptr;
        }

        E element_;
        TLNode* next_;
    };

    TLNode* head_;
    TLNode* tail_;
    LockType lockerH_;
    LockType lockerT_;
    unsigned int en_count_;
    unsigned int de_count_;
};
    
KUMA_NS_END

#endif
