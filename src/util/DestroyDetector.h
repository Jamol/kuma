//
//  kmobject.h
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

class DestroyDetector
{
public:
    ~DestroyDetector() {
        if(destroy_flag_ptr_) {
            *destroy_flag_ptr_ = true;
        }
    }

protected:
    bool *destroy_flag_ptr_ = nullptr;
};

#define DESTROY_DETECTOR_SETUP() \
bool destroyed = false; \
KUMA_ASSERT(nullptr == destroy_flag_ptr_); \
destroy_flag_ptr_ = &destroyed;

#define DESTROY_DETECTOR_CHECK(ret) \
if(destroyed) { \
return ret; \
} \
destroy_flag_ptr_ = nullptr;

KUMA_NS_END

#endif /* __DestroyDetector_H__ */
