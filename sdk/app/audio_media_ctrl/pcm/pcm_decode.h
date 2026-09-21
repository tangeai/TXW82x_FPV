#ifndef _PCM_DECODE_MSI_H_
#define _PCM_DECODE_MSI_H_

#include "basic_include.h"
#include "audio_code_ctrl.h"

#ifdef PSRAM_HEAP
#define PCM_DECODE_MALLOC    os_malloc_psram
#define PCM_DECODE_ZALLOC    os_zalloc_psram
#define PCM_DECODE_CALLOC    os_calloc_psram
#define PCM_DECODE_FREE      os_free_psram
#else
#define PCM_DECODE_MALLOC    os_malloc
#define PCM_DECODE_ZALLOC    os_zalloc
#define PCM_DECODE_CALLOC    os_calloc
#define PCM_DECODE_FREE      os_free
#endif

#define PCM_DEBUG(fmt, args...)         //os_printf(fmt, ##args)
#define PCM_INFO          				os_printf

struct msi *pcm_decode_init(uint32_t samplerate, char *filename, uint8_t loop_mode, AUDEC_INIT *audec_init);

#endif