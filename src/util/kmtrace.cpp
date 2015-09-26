//
//  trace.cpp
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 11/12/14.
//  Copyright (c) 2014. All rights reserved.
//

#include "kmtrace.h"
#include "util.h"

#include <stdio.h>
#include <stdarg.h>
#include <thread>
#include <string>
#include <iostream>
#include <sstream>

#ifdef KUMA_OS_WIN
# include <Windows.h>
#endif

KUMA_NS_BEGIN

#ifdef KUMA_OS_WIN
#define VSNPRINTF(d, dl, fmt, ...)    _vsnprintf_s(d, dl, _TRUNCATE, fmt, ##__VA_ARGS__)
#else
#define VSNPRINTF   vsnprintf
#endif

std::function<void(int, const char*)> trace_func;
void setTraceFunc(std::function<void(int, const char*)> func)
{
    trace_func = func;
}

void TracePrint(int level, const char* szMessage, ...)
{
    va_list VAList;
    char szMsgBuf[2048] = {0};
    va_start(VAList, szMessage);
    VSNPRINTF(szMsgBuf, sizeof(szMsgBuf)-1, szMessage, VAList);
    
    //std::thread::id tid = std::this_thread::get_id();
    std::stringstream ss;
    //ss << tid << ":" << getCurrentThreadId();
    ss << getCurrentThreadId();
    std::string stid = ss.str();
    
    std::string str_log;
    switch(level)
    {
        case KUMA_TRACE_LEVEL_INFO:
            str_log = "INFO: ";
            //printf("INFO: [%s] %s\n", stid.c_str(), szMsgBuf);
            break;
        case KUMA_TRACE_LEVEL_WARN:
            str_log = "WARN: ";
            //printf("WARN: [%s] %s\n", stid.c_str(), szMsgBuf);
            break;
        case KUMA_TRACE_LEVEL_ERROR:
            str_log = "ERROR: ";
            //printf("ERROR: [%s] %s\n", stid.c_str(), szMsgBuf);
            break;
        case KUMA_TRACE_LEVEL_DEBUG:
            str_log = "DEBUG: ";
            //printf("DEBUG: [%s] %s\n", stid.c_str(), szMsgBuf);
            break;
        default:
            str_log = "INFO: ";
            //printf("INFO: [%s] %s\n", stid.c_str(), szMsgBuf);
            break;
    }
    str_log += "[" + stid + "] " + szMsgBuf;
    /*ss.str("");
    ss.clear();
    ss << str_log << "[" << stid << "] " << szMsgBuf;
    str_log = ss.str();*/
    if (trace_func) {
        trace_func(level, str_log.c_str());
    } else {
        printf("%s\n", str_log.c_str());
    }
}

KUMA_NS_END
