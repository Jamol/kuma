#ifndef __KM_REFCOUNT_H__
#define __KM_REFCOUNT_H__

#include "kmatomic.h"

KUMA_NS_BEGIN

class KM_RefCount{
public:
    KM_RefCount() { ref_cnt_ = 0; }
    virtual ~KM_RefCount() {}

    virtual long acquireReference()
    {
        return ++ref_cnt_;
    }
    virtual long releaseReference()
    {
        long ref_cnt_tmp = 0;
        ref_cnt_tmp = --ref_cnt_;
        if(0 == ref_cnt_tmp) {
            onDestroy();
        }
        return ref_cnt_tmp;
    }
    virtual void onDestroy() { delete this; }

private:
    KM_Atomic ref_cnt_;
};

KUMA_NS_END

#endif
