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
#include <memory>

KUMA_NS_BEGIN

///
// enqueue on a thread and dequene on another thread
///
template <class E>
class KMQueue
{
public:
    KMQueue()
    {
        head_ = new TLNode(E());
        tail_ = head_;
    }
    ~KMQueue()
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
        ++en_count_;
    }
    
    void enqueue(E &&element)
    {
        TLNode* node = new TLNode(std::move(element));
        tail_->next_ = node;
        tail_ = node;
        ++en_count_;
    }

    bool dequeue(E &element)
    {
        if(empty()) {
            return false;
        }
        TLNode* node_to_delete = head_;
        ++de_count_;
        element = std::move(head_->next_->element_);
        head_ = head_->next_;
        delete node_to_delete;
        return true;
    }
    
    E& front() {
        if(empty()) {
            static E E_empty = E();
            return E_empty;
        }
        return head_->next_->element_;
    }
    
    void pop_front() {
        if(!empty()) {
            TLNode* node_to_delete = head_;
            ++de_count_;
            head_ = head_->next_;
            delete node_to_delete;
        }
    }
    
    bool empty()
    {// correct on producer side
        return nullptr == head_->next_;
    }
    
    uint32_t size()
    {// it is not precise
        return en_count_ - de_count_;
    }
    
protected:
    class TLNode
    {
    public:
        TLNode(const E &e) : element_(e), next_(nullptr) {}
        TLNode(E &&e) : element_(std::move(e)), next_(nullptr) {}

        E element_;
        TLNode* next_;
    };

    TLNode* head_;
    TLNode* tail_;
    uint32_t en_count_ = 0;
    uint32_t de_count_ = 0;
};

template <class E, class LockType>
class KMQueueMT
{
public:
    KMQueueMT()
    {
        head_ = new TLNode(E());
        tail_ = head_;
    }
    ~KMQueueMT()
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
        TLNode* node = new TLNode(std::move(element));
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
    
    uint32_t size()
    {// it is not precise
        return en_count_ - de_count_;
    }

protected:
    class TLNode
    {
    public:
        TLNode(const E &e) : element_(e), next_(nullptr) {}
        TLNode(E &&e) : element_(std::move(e)), next_(nullptr) {}

        E element_;
        TLNode* next_;
    };

    TLNode* head_;
    TLNode* tail_;
    LockType lockerH_;
    LockType lockerT_;
    uint32_t en_count_ = 0;
    uint32_t de_count_ = 0;
};

// double linked list
template <class E>
class DLQueue final
{
public:
    class DLNode
    {
    public:
        DLNode(const E &e) : element_(e) {}
        DLNode(E &&e) : element_(std::forward<E>(e)) {}
        template<class... Args>
        DLNode(Args&&... args) : element_(std::forward<Args>(args)...) {}
        
        using Ptr = std::shared_ptr<DLNode>;
        E element_;
        Ptr prev_;
        Ptr next_;
    };
    using NodePtr = typename DLNode::Ptr;
    
public:
    ~DLQueue()
    {
        while(head_) {
            head_ = head_->next_;
        }
    }
    
    NodePtr enqueue(const E &element)
    {
        auto node = std::make_shared<DLNode>(element);
        return enqueue(node);
    }
    
    NodePtr enqueue(E &&element)
    {
        auto node = std::make_shared<DLNode>(std::forward<E>(element));
        return enqueue(node);
    }
    
    NodePtr enqueue(NodePtr &node)
    {
        if (empty()) {
            head_ = node;
        } else {
            tail_->next_ = node;
            node->prev_ = tail_;
        }
        tail_ = node;
        return node;
    }
    
    bool dequeue(E &element)
    {
        if(empty()) {
            return false;
        }
        element = std::move(head_->element_);
        pop_front();
        return true;
    }
    
    E& front()
    {
        if(empty()) {
            static E E_empty = E();
            return E_empty;
        }
        return head_->element_;
    }
    
    NodePtr& front_node()
    {
        return head_;
    }
    
    void pop_front()
    {
        if(!empty()) {
            if (head_->next_) {
                head_ = head_->next_;
                head_->prev_->next_.reset();
                head_->prev_.reset();
            } else {
                head_.reset();
                tail_.reset();
            }
        }
    }
    
    bool remove(const NodePtr &node)
    {
        if (!node) {
            return false;
        }
        if (node->next_) {
            node->next_->prev_ = node->prev_;
        } else if (tail_ == node) {
            tail_ = node->prev_;
        }
        if (node->prev_) {
            node->prev_->next_ = node->next_;
        } else if (head_ == node) {
            head_ = node->next_;
        }
        node->next_.reset();
        node->prev_.reset();
        return true;
    }
    
    bool empty()
    {
        return !head_;
    }
    
    void swap(DLQueue &other)
    {
        head_.swap(other.head_);
        tail_.swap(other.tail_);
    }
    
protected:
    NodePtr head_;
    NodePtr tail_;
};
    
KUMA_NS_END

#endif
