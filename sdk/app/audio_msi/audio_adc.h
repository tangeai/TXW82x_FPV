#ifndef _AUDIO_ADC_H_
#define _AUDIO_ADC_H_

#include "basic_include.h"
#include "dev/audio/ausys.h"

#ifdef PSRAM_HEAP
#define AUADC_MALLOC    os_malloc_psram
#define AUADC_ZALLOC    os_zalloc_psram
#define AUADC_FREE      os_free_psram
#else
#define AUADC_MALLOC    os_malloc
#define AUADC_ZALLOC    os_zalloc
#define AUADC_FREE      os_free
#endif

#define AUADC_DEBUG(fmt, args...)     		//os_printf(fmt, ##args)
#define AUADC_INFO      					os_printf

#define AUADC_QUEUE_NUM       3

#ifndef MAX_AUADC_TXBUF
#define MAX_AUADC_TXBUF       4
#endif
#ifndef AUADC_TIME_INTERVAL
#define AUADC_TIME_INTERVAL   20
#endif
#ifndef AUADC_TASK_PRIORITY
#define AUADC_TASK_PRIORITY   OS_TASK_PRIORITY_ABOVE_NORMAL
#endif
#ifndef AUDIO_PROCESS
#define AUDIO_PROCESS         0
#endif

#ifndef MAIN_MIC_ID
#define MAIN_MIC_ID           0
#endif

#define AUADC_OUTPUT_SIN      0

typedef enum {
    mic_auadc = 0,
    mic_aupdm = 1,
    mic_auiis0 = 2,
    mic_auiis1 = 3,
}mic_platform_id;

int32_t audio_adc_init(enum ausys_ad_platform platform, uint32_t sampleRate, uint32_t channels, uint32_t soft_gain, uint32_t auproc_enable);
int32_t audio_adc_deinit(uint32_t mic_id);
int32_t audio_adc_get_samplerate(uint32_t mic_id);
int32_t audio_adc_get_channels(uint32_t mic_id);
struct msi *get_auadc_msi(uint32_t mic_id);
int32_t auadc_msi_add_output(uint32_t mic_id, const char *msi_name);
int32_t auadc_msi_del_output(uint32_t mic_id, const char *msi_name);
int32_t audio_adc_set_soft_gain(uint32_t mic_id, uint32_t soft_gain);
uint32_t audio_adc_get_energy(uint32_t mic_id);
#endif