//
//  DestroyDetector.h
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 8/9/16.
//  Copyright © 2016 kuma. All rights reserved.
//

#ifndef __DestroyDetector_H__
#define __DestroyDetector_H__

#include "kmdefs.h"
#include "kmtrace.h"

KUMA_NS_BEGIN

class DestroyGuard
{
public:
    ~DestroyGuard()
    {
        prev_->next_ = next_;
        next_->prev_ = prev_;
        prev_ = next_ = this;
    }
    
    void setDestroyed()
    {
        destroyed_ = true;
    }
    
    bool isDestroyed() const
    {
        return destroyed_;
    }
    
protected:
    friend class DestroyDetector;
    
    bool destroyed_ = false;
    DestroyGuard* prev_ = this;
    DestroyGuard* next_ = this;
};

class DestroyDetector
{
public:
    ~DestroyDetector() {
        auto next = dd_root_.next_;
        for (; next != &dd_root_; next = next->next_) {
            next->setDestroyed();
        }
    }
    
    void appendDestroyGuard(DestroyGuard *dg)
    {
        auto *last = dd_root_.prev_->next_;
        last->next_ = dg;
        dg->prev_ = last;
        dg->next_ = &dd_root_;
        dd_root_.prev_ = dg;
    }

protected:
    DestroyGuard dd_root_;
};

#define DESTROY_DETECTOR_SETUP() \
DestroyGuard __dguard; \
appendDestroyGuard(&__dguard);

#define DESTROY_DETECTOR_CHECK(ret) \
if(__dguard.isDestroyed()) { \
return ret; \
}

#define DESTROY_DETECTOR_CHECK_VOID() DESTROY_DETECTOR_CHECK((void()))

KUMA_NS_END

#endif /* __DestroyDetector_H__ */
