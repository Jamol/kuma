#ifndef __KMCONF_H__
#define __KMCONF_H__

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__CYGWIN__)
#define KUMA_OS_WIN
#elif defined(linux) || defined(__linux) || defined(__linux__)
#define KUMA_OS_LINUX
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
#define KUMA_OS_MACOS
#elif defined(iOS)
#define KUMA_OS_IOS
#elif defined(ANDROID)
#define KUMA_OS_ANDROID
#else
#error "unsupported OS"
#endif

#ifdef KUMA_OS_WIN
# include <memory>
# ifdef _HAS_CPP0X
#  define KUMA_HAS_CXX0X
# else
# endif
#endif

#if defined(KUMA_OS_MACOS) || defined(KUMA_OS_IOS)
#  define KUMA_HAS_CXX0X
#endif

#endif
