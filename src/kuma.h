#ifndef __KUMA_H__
#define __KUMA_H__

#include "kmdefs.h"

KUMA_NS_BEGIN

class IOHandler
{
public:
    virtual ~IOHandler() {}
    virtual long acquireReference() = 0;
    virtual long releaseReference() = 0;
    virtual int onEvent(unsigned int ev) = 0;
};

KUMA_NS_END

#endif
