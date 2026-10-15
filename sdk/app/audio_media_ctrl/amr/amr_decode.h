#ifndef _AMR__DECODE_MSI_H_
#define _AMR__DECODE_MSI_H_

#include "basic_include.h"
#include "audio_code_ctrl.h"

#ifdef PSRAM_HEAP
#define AMR_DECODE_MALLOC    os_malloc_psram
#define AMR_DECODE_ZALLOC    os_zalloc_psram
#define AMR_DECODE_FREE      os_free_psram
#else
#define AMR_DECODE_MALLOC    os_malloc
#define AMR_DECODE_ZALLOC    os_zalloc
#define AMR_DECODE_FREE      os_free
#endif

#define AMR_DEBUG(fmt, args...)     		//os_printf(fmt, ##args)
#define AMR_INFO      					    os_printf

struct msi *amr_decode_init(char *filename, uint8_t loop_mode, AUDEC_INIT *audec_init);

#endif