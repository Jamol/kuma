#ifndef __KMDEFS_H__
#define __KMDEFS_H__

#define KUMA_NS_BEGIN   namespace kuma {;
#define KUMA_NS_END     }

KUMA_NS_BEGIN

enum{
    KUMA_ERROR_NOERR    = 0,
    KUMA_ERROR_FAILED,
    KUMA_ERROR_INVALID_STATE,
    KUMA_ERROR_INVALID_PARAM,
    KUMA_ERROR_UNSUPPORT
};

#define KUMA_EV_READ    1
#define KUMA_EV_WRITE   (1 << 1)
#define KUMA_EV_ERROR   (1 << 2)
#define KUMA_EV_NETWORK (KUMA_EV_READ|KUMA_EV_WRITE|KUMA_EV_ERROR)

KUMA_NS_END

#endif
