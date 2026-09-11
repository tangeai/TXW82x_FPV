/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Adam Dunkels <adam@sics.se>
 *
 */
#ifndef __CC_H__
#define __CC_H__

#include <stdint.h>
#include <stddef.h> /* for size_t */
#include "typesdef.h"
#include "list.h"
#include "errno.h"
#include "dev.h"
#include "devid.h"
#include "osal/string.h"

/* ARM/LPC17xx is little endian only */
#if !defined(BYTE_ORDER) || (BYTE_ORDER != LITTLE_ENDIAN && BYTE_ORDER != BIG_ENDIAN)
#ifdef BYTE_ORDER
#undef BYTE_ORDER
#endif
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* Use LWIP error codes */
#define LWIP_PROVIDE_ERRNO

#if defined(__arm__) && defined(__ARMCC_VERSION)
/* Keil uVision4 tools */
#define PACK_STRUCT_BEGIN //__packed
#define PACK_STRUCT_STRUCT __packed
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(fld) fld
#define ALIGNED(n)  __align(n)
#elif defined (__IAR_SYSTEMS_ICC__)
/* IAR Embedded Workbench tools */
#define PACK_STRUCT_BEGIN __packed
#define PACK_STRUCT_STRUCT
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(fld) fld
#define IAR_STR(a) #a
#define ALIGNED(n) _Pragma(IAR_STR(data_alignment= ## n ##))
#else
/* GCC tools (CodeSourcery) */
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__ ((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(fld) fld
#define ALIGNED(n)  __attribute__((aligned (n)))
#endif

/* Provide Thumb-2 routines for GCC to improve performance */
#if defined(TOOLCHAIN_GCC) && defined(__thumb2__)
#define MEMCPY(dst,src,len)     hw_memcpy(dst,src,len)//thumb2_memcpy(dst,src,len)
#define LWIP_CHKSUM             thumb2_checksum
/* Set algorithm to 0 so that unused lwip_standard_chksum function
   doesn't generate compiler warning */
#define LWIP_CHKSUM_ALGORITHM   0

void *thumb2_memcpy(void *pDest, const void *pSource, size_t length);
uint16_t thumb2_checksum(const void *pData, int length);
#else
/* Used with IP headers only */
#define LWIP_CHKSUM_ALGORITHM   3   //
#endif


#ifdef LWIP_DEBUG

#include "stdio.h"

void assert_printf(char *msg, int line, char *file);

/* Plaform specific diagnostic output */
#define LWIP_PLATFORM_DIAG(vars) printf vars
#define LWIP_PLATFORM_ASSERT(flag) { assert_printf((flag), __LINE__, __FILE__); }
#elif LWIP_STATS_DISPLAY
/* 非 DEBUG 但开了 stats_display.
 *
 * 不能直接调 printf: stats.c 里调用是 LWIP_PLATFORM_DIAG(("recv: %u\n\t", ...)),
 * 每条以 "\n\t" 结尾, 项目 hgprintf 只在缓冲最末字符==\n 时才把 \n 转 \r\n
 * (string.c:323), 中间的 \n 被原样发到 UART, 表现为整段 dump 压成一行.
 *
 * 这里用 static inline wrapper, vsnprintf 到栈缓冲再逐字节 \n → \r\n,
 * 一次性吐到 UART. 只在编译 stats.c 时被 lwip 头链路引入, 不污染其他文件. */
#include <stdarg.h>
extern int vsnprintf(char *str, unsigned int size, const char *fmt, va_list ap);
extern int printf(const char *fmt, ...);
static inline void __lwip_diag_crlf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
    /* 原地无法插入, 改用串 putchar; 但 hgprintf 锁更重, 这里一次一行调 printf
     * 切片: 用 \n 拆开, 每段独立 printf("%.*s\r\n", ...) 让 hgprintf 末尾自带 \r\n. */
//    char *p = buf;
    char *line_start = buf;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') {
            buf[i] = 0;
            printf("%s\r\n", line_start);
            line_start = &buf[i + 1];
        }
    }
    if (line_start < &buf[n]) {
        /* 末尾不带 \n 的残余 (例如 stats.c 每条以 \n\t 结尾时残一个 \t):
         * 不加换行, 直接吐出去 — 让下一条续上 (但因下一条又会先吐\r\n,
         * 这个 \t 会落到下一行行首充当缩进). */
        printf("%s", line_start);
    }
}
#define LWIP_PLATFORM_DIAG(vars) __lwip_diag_crlf vars
#define LWIP_PLATFORM_ASSERT(flag) { ; }
#else
#define LWIP_PLATFORM_DIAG(msg) { ; }
#define LWIP_PLATFORM_ASSERT(flag) { ; }
#endif

//#include "cmsis.h"
#define LWIP_PLATFORM_HTONS(x)      __REV16(x)
#define LWIP_PLATFORM_HTONL(x)      __REV(x)

#endif /* __CC_H__ */
