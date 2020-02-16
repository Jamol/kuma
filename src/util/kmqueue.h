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

#pragma once

#include "kmdefs.h"
#include <memory>
#include <atomic>

KUMA_NS_BEGIN

const size_t kPaddingSize = 128;
///
// enqueue on a thread and dequene on another thread
///
template <class E>
class KMQueue
{
public:
    KMQueue()
    {
        head_ = new TLNode();
        tail_ = head_;
    }
    ~KMQueue()
    {
        TLNode *node = nullptr;
        while(head_) {
            node = head_;
            head_ = head_->next_.load(std::memory_order_relaxed);
            delete node;
        }
    }

    template <class... Args>
    void enqueue(Args&&... args)
    {
        TLNode *node = new TLNode(std::forward<Args>(args)...);
        tail_->next_.store(node, std::memory_order_release);
        tail_ = node;
        ++count_;
    }

    bool dequeue(E &element)
    {
        auto *node = head_->next_.load(std::memory_order_acquire);
        if (node == nullptr) {
            return false;
        }
        --count_;
        element = std::move(node->element_);
        delete head_;
        head_ = node;
        return true;
    }
    
    E& front() {
        auto *node = head_->next_.load(std::memory_order_acquire);
        if (node == nullptr) {
            static E E_empty = E();
            return E_empty;
        }
        return node->element_;
    }
    
    void pop_front() {
        auto *node = head_->next_.load(std::memory_order_acquire);
        if (node != nullptr) {
            --count_;
            delete head_;
            head_ = node;
        }
    }
    
    bool empty()
    {
        return size() == 0;
    }
    
    size_t size()
    {
        return count_.load(std::memory_order_relaxed);
    }
    
protected:
    class TLNode
    {
    public:
        template<class... Args>
        TLNode(Args&&... args) : element_{ std::forward<Args>(args)... } {}

        E element_;
        std::atomic<TLNode*> next_{ nullptr };
    };

    TLNode* head_{ nullptr };
    char __pad0__[kPaddingSize - sizeof(TLNode*)];
    TLNode* tail_{ nullptr };
    char __pad1__[kPaddingSize - sizeof(TLNode*)];
    std::atomic<size_t> count_{ 0 };
};


// double linked list
template <class E>
class DLQueue final
{
public:
    class DLNode
    {
    public:
        using Ptr = std::shared_ptr<DLNode>;
        
        template<class... Args>
        DLNode(Args&&... args) : element_{ std::forward<Args>(args)... } {}
        E& element() { return element_; }
        
    private:
        friend class DLQueue;
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
    
    template <class... Args>
    NodePtr enqueue(Args&&... args)
    {
        auto node = std::make_shared<DLNode>(std::forward<Args>(args)...);
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
    char __pad__[kPaddingSize - sizeof(NodePtr)];
    NodePtr tail_;
};
    
KUMA_NS_END
