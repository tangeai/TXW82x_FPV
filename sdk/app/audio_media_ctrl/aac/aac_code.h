#ifndef _AAC_CODE_H_
#define _AAC_CODE_H_

#include "basic_include.h"
#include "audio_code_ctrl.h"

#ifdef PSRAM_HEAP
#define AAC_CODE_MALLOC    os_malloc_psram
#define AAC_CODE_ZALLOC    os_zalloc_psram
#define AAC_CODE_CALLOC    os_calloc_psram
#define AAC_CODE_FREE      os_free_psram
#else
#define AAC_CODE_MALLOC    os_malloc
#define AAC_CODE_ZALLOC    os_zalloc
#define AAC_CODE_CALLOC    os_calloc
#define AAC_CODE_FREE      os_free
#endif

#define AAC_DEBUG(fmt, args...)     		//os_printf(fmt, ##args)
#define AAC_INFO      					    os_printf

struct msi *aac_encode_init(char *filename, uint32_t samplerate, uint32_t channels, uint8_t direct_to_record, AUENC_INIT *auenc_init);
struct msi *aac_decode_init(char *filename, uint8_t loop_mode, AUDEC_INIT *audec_init);

#endif