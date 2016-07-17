//
//  kmtrace.h
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 11/12/14.
//  Copyright (c) 2014. All rights reserved.
//

#ifndef __kuma__trace__
#define __kuma__trace__
#include "kmconf.h"
#include "kmdefs.h"
#include <sstream>
#include <assert.h>

KUMA_NS_BEGIN

#define KUMA_TRACE_LEVEL_ERROR  1
#define KUMA_TRACE_LEVEL_WARN   2
#define KUMA_TRACE_LEVEL_INFO   3
#define KUMA_TRACE_LEVEL_DEBUG  4

#define KUMA_TRACE(l, x) \
    do{ \
        std::stringstream ss; \
        ss<<x; \
        TracePrint(l, "%s", ss.str().c_str());\
    }while(0)
    
#define KUMA_XTRACE(l, x) \
    do{ \
        std::stringstream ss; \
        ss<<getObjKey()<<":: "<<x; \
        TracePrint(l, "%s", ss.str().c_str());\
    }while(0)
    
#define KUMA_INFOXTRACE(x)  KUMA_XTRACE(KUMA_TRACE_LEVEL_INFO, x)
#define KUMA_WARNXTRACE(x)  KUMA_XTRACE(KUMA_TRACE_LEVEL_WARN, x)
#define KUMA_ERRXTRACE(x)   KUMA_XTRACE(KUMA_TRACE_LEVEL_ERROR, x)
#define KUMA_DBGXTRACE(x)   KUMA_XTRACE(KUMA_TRACE_LEVEL_DEBUG, x)
#define KUMA_INFOTRACE(x)   KUMA_TRACE(KUMA_TRACE_LEVEL_INFO, x)
#define KUMA_WARNTRACE(x)   KUMA_TRACE(KUMA_TRACE_LEVEL_WARN, x)
#define KUMA_ERRTRACE(x)    KUMA_TRACE(KUMA_TRACE_LEVEL_ERROR, x)
#define KUMA_DBGTRACE(x)    KUMA_TRACE(KUMA_TRACE_LEVEL_DEBUG, x)

#define KUMA_ASSERT(x) assert(x)

void TracePrint(int level, const char* szMessage, ...);

KUMA_NS_END
        
#endif /* defined(__kuma__trace__) */
