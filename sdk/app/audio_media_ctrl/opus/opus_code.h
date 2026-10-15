#ifndef _OPUS_CODE_H_
#define _OPUS_CODE_H_

#include "basic_include.h"
#include "audio_code_ctrl.h"

#ifdef PSRAM_HEAP
#define OPUS_CODE_MALLOC    os_malloc_psram
#define OPUS_CODE_ZALLOC    os_zalloc_psram
#define OPUS_CODE_CALLOC    os_calloc_psram
#define OPUS_CODE_FREE      os_free_psram
#else
#define OPUS_CODE_MALLOC    os_malloc
#define OPUS_CODE_ZALLOC    os_zalloc
#define OPUS_CODE_CALLOC    os_calloc
#define OPUS_CODE_FREE      os_free
#endif

#define OPUS_DEBUG(fmt, args...)     		//os_printf(fmt, ##args)
#define OPUS_INFO      					    os_printf

struct msi *opus_encode_init(uint32_t samplerate, uint32_t channels, AUENC_INIT *auenc_init);
struct msi *opus_decode_init(uint32_t samplerate, AUDEC_INIT *audec_init);

#endif