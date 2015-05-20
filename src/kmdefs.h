#ifndef __KMDEFS_H__
#define __KMDEFS_H__

#include "kmconf.h"

#define KUMA_NS_BEGIN   namespace kuma {;
#define KUMA_NS_END     }

KUMA_NS_BEGIN

enum{
    KUMA_ERROR_NOERR    = 0,
    KUMA_ERROR_FAILED,
    KUMA_ERROR_INVALID_STATE,
    KUMA_ERROR_INVALID_PARAM,
    KUMA_ERROR_ALREADY_EXIST,
    KUMA_ERROR_POLLERR,
    KUMA_ERROR_SSL_FAILED,
    KUMA_ERROR_UNSUPPORT
};

KUMA_NS_END

#endif
