#ifndef _CONFIG_H_
#define _CONFIG_H_
#include <stdint.h>
#define PRIx32 "x"
#define PRIx16 "x"
#define PRIu32 "d"
#define PRIu16 "d"
#define ENABLE_QRCODE 1

#define PACKAGE "zbar"
#define VERSION "0.10"
#define ZBAR_VERSION_MAJOR 0
#define ZBAR_VERSION_MINOR 10

#define HAVE_ERRNO_H 1
//#define HAVE_INTTYPES_H 1
#define HAVE_STDLIB_H 1
#define HAVE_MEMSET 1

#if defined(_WIN32) || defined(_WIN64)
/* tcc/mingw: no winmm lib, provide timeGetTime() in portable/timerstub.c */
# include <windows.h>
DWORD WINAPI timeGetTime(void);
#else
/* POSIX */
# define HAVE_UNISTD_H 1
# define HAVE_SYS_TYPES_H 1
# define HAVE_SYS_STAT_H 1
# define HAVE_SYS_TIME_H 1
# define HAVE_FCNTL_H 1
# define HAVE_SYS_MMAN_H 1
#endif

#endif
