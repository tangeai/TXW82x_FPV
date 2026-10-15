#ifndef _PROMPT_TONE_H_
#define _PROMPT_TONE_H_

#include "basic_include.h"
#include "lib/multimedia/msi.h"
#include "lib/multimedia/framebuff.h"

#ifdef PSRAM_HEAP
#define PROMPTTONE_MALLOC os_malloc_psram
#define PROMPTTONE_ZALLOC os_zalloc_psram
#define PROMPTTONE_FREE   os_free_psram
#else
#define PROMPTTONE_MALLOC os_malloc
#define PROMPTTONE_ZALLOC os_zalloc
#define PROMPTTONE_FREE   os_free
#endif

typedef struct {
    const uint8_t *tone_buf;
    void *task_hdl;
    uint32_t tone_buf_size;
    uint32_t tone_buf_offset;
    struct fbpool tx_pool;
    struct msi *msi;
    struct msi *mp3_msi;
} PROMPT_TONE_STRUCT;

extern const uint8_t connect_mp3[];
extern const uint8_t disconnect_mp3[];

#endif