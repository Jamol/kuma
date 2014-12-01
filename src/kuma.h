#ifndef __KUMA_H__
#define __KUMA_H__

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

class IOHandler
{
public:
    virtual ~IOHandler() {}
    
    virtual int acquireRef() = 0;
    virtual int releaseRef() = 0;
    virtual int onEvent(unsigned int ev) = 0;
};

KUMA_NS_END

#endif
