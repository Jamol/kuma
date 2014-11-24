#ifndef __KM_REFCOUNT_H__
#define __KM_REFCOUNT_H__

#include "kmatomic.h"

namespace komm {;

class KM_RefCount{
public:
    KM_RefCount() { ref_cnt_ = 0; }
    virtual ~KM_RefCount() {}

    virtual void add_reference()
    {
        ++ref_cnt_;
    }
    virtual void release_reference()
    {
        long ref_cnt_tmp = 0;
        ref_cnt_tmp = --ref_cnt_;
        if(0 == ref_cnt_tmp) {
            on_ref_zero();
        }
    }
    virtual void on_ref_zero() { delete this; }

private:
    KM_Atomic ref_cnt_;
};

} // namespace komm
#endif
