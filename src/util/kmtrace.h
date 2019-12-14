//
//  kmtrace.h
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 11/12/14.
//  Copyright (c) 2014-2019. All rights reserved.
//

#pragma once

#include "kmconf.h"

#include <sstream>
#include <assert.h>

namespace kuma {

#define KUMA_TRACE_TAG  "KUMA"

#define KUMA_TRACE(l, x) \
    do{ \
        if (l <= kuma::getTraceLevel()) {\
            std::stringstream ss; \
            ss<<x; \
            kuma::traceWrite(l, ss.str()); \
        }\
    }while(0)
    
#define KUMA_XTRACE(l, x) \
    do{ \
        if (l <= kuma::getTraceLevel()) {\
            std::stringstream ss; \
            ss<<getObjKey()<<":: "<<x; \
            kuma::traceWrite(l, ss.str()); \
        }\
    }while(0)
    
#define KUMA_INFOXTRACE(x)  KUMA_XTRACE(kuma::TRACE_LEVEL_INFO, x)
#define KUMA_WARNXTRACE(x)  KUMA_XTRACE(kuma::TRACE_LEVEL_WARN, x)
#define KUMA_ERRXTRACE(x)   KUMA_XTRACE(kuma::TRACE_LEVEL_ERROR, x)
#define KUMA_DBGXTRACE(x)   KUMA_XTRACE(kuma::TRACE_LEVEL_DEBUG, x)

#define KUMA_INFOTRACE(x)   KUMA_TRACE(kuma::TRACE_LEVEL_INFO, x)
#define KUMA_WARNTRACE(x)   KUMA_TRACE(kuma::TRACE_LEVEL_WARN, x)
#define KUMA_ERRTRACE(x)    KUMA_TRACE(kuma::TRACE_LEVEL_ERROR, x)
#define KUMA_DBGTRACE(x)    KUMA_TRACE(kuma::TRACE_LEVEL_DEBUG, x)

#define KUMA_INFOTRACE_THIS(x)   KUMA_TRACE(kuma::TRACE_LEVEL_INFO, x<<", this="<<this)
#define KUMA_WARNTRACE_THIS(x)   KUMA_TRACE(kuma::TRACE_LEVEL_WARN, x<<", this="<<this)
#define KUMA_ERRTRACE_THIS(x)    KUMA_TRACE(kuma::TRACE_LEVEL_ERROR, x<<", this="<<this)
#define KUMA_DBGTRACE_THIS(x)    KUMA_TRACE(kuma::TRACE_LEVEL_DEBUG, x<<", this="<<this)

#define KUMA_ASSERT(x) assert(x)

const int TRACE_LEVEL_ERROR  = 1;
const int TRACE_LEVEL_WARN   = 2;
const int TRACE_LEVEL_INFO   = 3;
const int TRACE_LEVEL_DEBUG  = 4;
const int TRACE_LEVEL_VERBOS = 5;
const int TRACE_LEVEL_MAX = TRACE_LEVEL_VERBOS;

void traceWrite(int level, const std::string &msg);
void traceWrite(int level, std::string &&msg);

// msg is null-terminated and msg_len doesn't include '\0'
using TraceFunc = void(*)(int level, const char* msg, size_t msg_len);
void setTraceFunc(TraceFunc func);
void setTraceLevel(int level);
int getTraceLevel();

} // namespace kuma
