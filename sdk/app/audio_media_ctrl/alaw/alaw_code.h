#ifndef _ALAW_CODE_H_
#define _ALAW_CODE_H_

#include "basic_include.h"
#include "audio_code_ctrl.h"

#ifdef PSRAM_HEAP
#define ALAW_CODE_MALLOC    os_malloc_psram
#define ALAW_CODE_ZALLOC    os_zalloc_psram
#define ALAW_CODE_CALLOC    os_calloc_psram
#define ALAW_CODE_FREE      os_free_psram
#else
#define ALAW_CODE_MALLOC    os_malloc
#define ALAW_CODE_ZALLOC    os_zalloc
#define ALAW_CODE_CALLOC    os_calloc
#define ALAW_CODE_FREE      os_free
#endif

#define ALAW_DEBUG(fmt, args...)     		//os_printf(fmt, ##args)
#define ALAW_INFO      					    os_printf

struct msi *alaw_encode_init(uint32_t samplerate, uint32_t channels, AUENC_INIT *auenc_init);
struct msi *alaw_decode_init(AUDEC_INIT *audec_init);

#endif