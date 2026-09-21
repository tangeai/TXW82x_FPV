#ifndef _MP3_DECODE_MSI_H_
#define _MP3_DECODE_MSI_H_

#include "basic_include.h"
#include "audio_code_ctrl.h"

#ifdef PSRAM_HEAP
#define MP3_DECODE_MALLOC    os_malloc_psram
#define MP3_DECODE_ZALLOC    os_zalloc_psram
#define MP3_DECODE_CALLOC    os_calloc_psram
#define MP3_DECODE_FREE      os_free_psram
#else
#define MP3_DECODE_MALLOC    os_malloc
#define MP3_DECODE_ZALLOC    os_zalloc
#define MP3_DECODE_CALLOC    os_calloc
#define MP3_DECODE_FREE      os_free
#endif

#define MP3_DEBUG(fmt, args...)         //os_printf(fmt, ##args)
#define MP3_INFO          				os_printf

struct msi *mp3_decode_init(char *filename, uint8_t loop_mode, AUDEC_INIT *audec_init);

#endif