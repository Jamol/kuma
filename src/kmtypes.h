#ifndef __KMTYPES_H__
#define __KMTYPES_H__

typedef char                int8;
typedef unsigned char       uint8;
typedef short               int16;
typedef unsigned short      uint16;
typedef signed int          int32;
typedef unsigned int        uint32;

typedef uint8               boolean;

#ifdef WIN32
typedef __int64             int64;
typedef unsigned __int64    uint64;
#else
typedef long long           int64;
typedef unsigned long long  uint64;
#endif

#if !defined(TRUE)
#define TRUE ((boolean)1)
#endif

#if !defined(FALSE)
#define FALSE ((boolean)0)
#endif

#endif
