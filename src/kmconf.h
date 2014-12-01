#ifndef __KMCONF_H__
#define __KMCONF_H__

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(_WIN64) || defined(__CYGWIN__)
# define KUMA_OS_WIN
#elif defined(__ANDROID__)
# define KUMA_OS_ANDROID
# define KUMA_OS_LINUX
#elif defined(linux) || defined(__linux) || defined(__linux__)
# define KUMA_OS_LINUX
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
# define KUMA_OS_MAC
# include <TargetConditionals.h>
# if TARGET_OS_IPHONE == 1
#  define KUMA_OS_IOS
# endif
#else
# error "unknown OS"
#endif

#ifdef KUMA_OS_WIN
# if defined(_WIN64)
#  define KUMA_ENV64
# else
#  define KUMA_ENV32
# endif
#endif

#if defined(__GNUC__) || defined(__clang__)
# if defined(__x86_64__) || defined(__ppc64__)
#  define KUMA_ENV64
# else
#  define KUMA_ENV32
# endif
#endif

#if __cplusplus > 199711L
# define KUMA_HAS_CXX0X
#endif

#endif
