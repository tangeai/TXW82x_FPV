#ifndef AAC_PB_DIAG_H
#define AAC_PB_DIAG_H

#include "osal/string.h"

/* SD 卡回放临时诊断开关，设为 0 可关闭全部诊断代码。 */
#ifndef AAC_PB_DIAG
#define AAC_PB_DIAG 0
#endif

#if AAC_PB_DIAG
#define AAC_PB_DIAG_MAGIC 0x41414344u
#define AAC_PB_DIAG_DUMPS 3u

/* 由解复用帧持有，现有的 MSI_CMD_FREE_FB 流程负责释放 priv。 */
struct aac_pb_diag_frame {
    uint32_t magic;
    uint32_t sample;       /* 从 0 开始的 MP4 音频样本索引 */
    uint32_t offset;       /* AAC 裸数据在 MP4 文件中的偏移 */
    uint32_t len;          /* ADTS 头与 AAC 裸数据 */
    uint32_t full_hash;
    uint32_t raw_hash;
    uint8_t dsi[2];
};

static inline int aac_pb_diag_source(const char *name)
{
    return name && os_strncmp(name, "sd_pb_demux_", 12) == 0;
}

/* FNV-1a 哈希，按无符号 32 位整数回绕计算；不是 CRC。 */
static inline uint32_t aac_pb_diag_hash(const uint8_t *data, uint32_t len)
{
    uint32_t hash = 2166136261u;
    uint32_t i;
    for (i = 0; i < len; ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}
#endif
#endif
