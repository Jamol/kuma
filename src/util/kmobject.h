//
//  kmobject.h
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 7/17/16.
//  Copyright © 2016 kuma. All rights reserved.
//

#ifndef __KMObject_H__
#define __KMObject_H__

#include "kmdefs.h"
#include <string>
#include <atomic>
#include <sstream>

KUMA_NS_BEGIN

class KMObject
{
public:
    KMObject() {
        static std::atomic<long> s_objIdSeed{0};
        objId_ = ++s_objIdSeed;
    }
    
    const std::string& getObjKey() const {
        return objKey_;
    }
    
    long getObjId() const { return objId_; }
    
protected:
    std::string objKey_;
    long objId_ = 0;
};

#define KM_SetObjKey(x) \
do{ \
    std::stringstream ss; \
    ss<<x<<"_"<<objId_; \
    objKey_ = ss.str();\
}while(0)

KUMA_NS_END

#endif /* __KMObject_H__ */
