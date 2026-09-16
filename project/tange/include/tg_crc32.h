#ifndef __tg_crc32_h__
#define __tg_crc32_h__

#include "platforms.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t calc_crc32(uint32_t crc, const void *buf, int len);

#ifdef __cplusplus
}
#endif

#endif
