//
//  trace.cpp
//  kuma
//
//  Created by Fengping Bao <jamol@live.com> on 11/12/14.
//  Copyright (c) 2014-2019. All rights reserved.
//

#include "kmtrace.h"

#include <stdio.h>
#include <stdarg.h>
#include <thread>
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>

#ifdef KUMA_OS_WIN
# include <Windows.h>
#elif defined(KUMA_OS_LINUX)
# include <sys/types.h>
# include <unistd.h>
# if !defined(KUMA_OS_ANDROID)
#  include <sys/syscall.h>
# endif
#endif


#ifdef KUMA_OS_WIN
# define getCurrentThreadId() GetCurrentThreadId()
#elif defined(KUMA_OS_MAC)
# define getCurrentThreadId() pthread_mach_thread_np(pthread_self())
#elif defined(KUMA_OS_ANDROID)
# define getCurrentThreadId() gettid()
#elif defined(KUMA_OS_LINUX)
# define getCurrentThreadId() syscall(__NR_gettid)
#else
# define getCurrentThreadId() pthread_self()
#endif

#ifdef KUMA_OS_ANDROID
# include <android/log.h>
#endif

namespace {
std::string getDateTimeString(bool utc) {
    auto now = std::chrono::system_clock::now();
    auto msecs = std::chrono::duration_cast<std::chrono::milliseconds>
        (now.time_since_epoch()).count() % 1000;
    auto itt = std::chrono::system_clock::to_time_t(now);
    struct tm res;
#ifdef KUMA_OS_WIN
    utc ? gmtime_s(&res, &itt) : localtime_s(&res, &itt); // windows and C11
#else
    utc ? gmtime_r(&itt, &res) : localtime_r(&itt, &res);
#endif
    std::ostringstream ss;
    ss << std::put_time(&res, "%FT%T.") << std::setfill('0')
       << std::setw(3) << msecs;
    if (utc) {
        ss << 'Z';
    } else {
        ss << std::put_time(&res, "%z");
    }
    return ss.str();
}
} // namespace

namespace kuma {

static TraceFunc s_traceFunc = nullptr;
static int s_traceLevel = TRACE_LEVEL_INFO;

const char* kTraceStrings[] = {
    "NONE",
    "ERROR",
    "WARN",
    "INFO",
    "DEBUG",
    "VERBOS"
};

#ifdef KUMA_OS_ANDROID
const int kAndroidLogLevels[] = {
    ANDROID_LOG_INFO,
    ANDROID_LOG_ERROR,
    ANDROID_LOG_WARN,
    ANDROID_LOG_INFO,
    ANDROID_LOG_DEBUG,
    ANDROID_LOG_VERBOSE
};
#endif

#ifdef KUMA_OS_WIN
# define VSNPRINTF(d, dl, fmt, ...)    _vsnprintf_s(d, dl, _TRUNCATE, fmt, ##__VA_ARGS__)
#else
# define VSNPRINTF   vsnprintf
#endif
void tracePrint(int level, const char* szMessage, ...)
{
    va_list VAList;
    char szMsgBuf[2048] = {0};
    va_start(VAList, szMessage);
    VSNPRINTF(szMsgBuf, sizeof(szMsgBuf)-1, szMessage, VAList);
    
    if (level > TRACE_LEVEL_MAX) {
        level = TRACE_LEVEL_MAX;
    } else if (level < TRACE_LEVEL_ERROR) {
        level = TRACE_LEVEL_ERROR;
    }
    
#if defined(KUMA_OS_ANDROID)
    int android_level = kAndroidLogLevels[level];
    __android_log_print(android_level, KUMA_TRACE_TAG, "%s", szMsgBuf);
#else
    std::stringstream ss;
    ss << kTraceStrings[level];
    ss << " [" << getCurrentThreadId() << "] " << szMsgBuf << '\n';
# if defined(KUMA_OS_WIN)
    OutputDebugStringA(ss.str().c_str());
# else
    printf("%s, %s", getDateTimeString(false).c_str(), ss.str().c_str());
# endif
#endif
}

void traceWrite(int level, const std::string &msg)
{
    if (s_traceFunc) {
        s_traceFunc(level, msg.c_str(), msg.size());
    } else {
        if (level > TRACE_LEVEL_MAX) {
            level = TRACE_LEVEL_MAX;
        } else if (level < TRACE_LEVEL_ERROR) {
            level = TRACE_LEVEL_ERROR;
        }
#if defined(KUMA_OS_ANDROID)
        int android_level = kAndroidLogLevels[level];
        __android_log_print(android_level, KUMA_TRACE_TAG, "%s", msg.c_str());
#else
        std::stringstream ss;
        ss << kTraceStrings[level];
        ss << " [" << getCurrentThreadId() << "] " << msg << '\n';
# if defined(KUMA_OS_WIN)
        OutputDebugStringA(ss.str().c_str());
# else
        printf("%s %s", getDateTimeString(false).c_str(), ss.str().c_str());
# endif
#endif
    }
}

void traceWrite(int level, std::string &&msg)
{
    traceWrite(level, msg);
}

void setTraceFunc(TraceFunc func)
{
    s_traceFunc = std::move(func);
}

void setTraceLevel(int level)
{
    s_traceLevel = level;
}

int getTraceLevel()
{
    return s_traceLevel;
}

} // namespace kuma
