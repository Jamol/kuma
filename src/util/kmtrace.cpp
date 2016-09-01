//
//  trace.cpp
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 11/12/14.
//  Copyright (c) 2014. All rights reserved.
//

#include "kmtrace.h"
#include "util.h"
#include "kmapi.h"

#include <stdio.h>
#include <stdarg.h>
#include <thread>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

#ifdef KUMA_OS_WIN
# include <Windows.h>
#endif

KUMA_NS_BEGIN

#ifdef KUMA_OS_WIN
#define VSNPRINTF(d, dl, fmt, ...)    _vsnprintf_s(d, dl, _TRUNCATE, fmt, ##__VA_ARGS__)
#define LOCALTIME_R(timep, result) localtime_s(result, timep)
#else
#define VSNPRINTF   vsnprintf
#define LOCALTIME_R localtime_r
#endif

TraceFunc trace_func;
void setTraceFunc(TraceFunc func)
{
    trace_func = std::move(func);
}

void TracePrint(int level, const char* szMessage, ...)
{
    va_list VAList;
    char szMsgBuf[2048] = {0};
    va_start(VAList, szMessage);
    VSNPRINTF(szMsgBuf, sizeof(szMsgBuf)-1, szMessage, VAList);
    
    std::stringstream ss;
    
    /*auto now_p = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now_p.time_since_epoch());
    auto now_c = std::chrono::system_clock::to_time_t(now_p);
    struct tm tm_buf;
    LOCALTIME_R(&now_c, &tm_buf);
    ss << std::put_time(&tm_buf, "%F %T.");
    ss.width(3);
    ss.fill('0');
    ss << (now_ms.count()%1000) << " ";*/
    switch(level)
    {
        case KUMA_TRACE_LEVEL_INFO:
            ss << "INFO: ";
            break;
        case KUMA_TRACE_LEVEL_WARN:
            ss << "WARN: ";
            break;
        case KUMA_TRACE_LEVEL_ERROR:
            ss << "ERROR: ";
            break;
        case KUMA_TRACE_LEVEL_DEBUG:
            ss << "DEBUG: ";
            break;
        default:
            ss << "INFO: ";
            break;
    }
    ss << "[" << getCurrentThreadId() << "] " << szMsgBuf;
    if (trace_func) {
        trace_func(level, ss.str().c_str());
    } else {
#ifdef KUMA_OS_WIN
        OutputDebugString(ss.str().c_str());
#else
        ss << std::endl;
        // sometimes the outputs are garbled when two threads print log with cout simultaneously
        //std::cout << ss.str();
        printf("%s", ss.str().c_str());
#endif
    }
}

KUMA_NS_END
