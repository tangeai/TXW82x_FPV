#ifndef __DECODE_MEM_H__
#define __DECODE_MEM_H__
#define MEM_CACHE_DEBUG 0
#define MEM_CACHE_PRINTF(fmt, ...)                                                                                                                                                                     \
    if (MEM_CACHE_DEBUG)                                                                                                                                                                               \
    {                                                                                                                                                                                                  \
        os_printf(fmt, ##__VA_ARGS__);                                                                                                                                                                 \
    }

// 统计mem空间使用的时间情况,超时 MAX_TTL 没有使用,则移除
// 优先使用最近使用的内存池空间
// 32对齐

struct mem_info
{
    uint32_t used : 1, rev : 31;
    uint32_t size;      // 内存块大小
    uint32_t addr;      // 内存块地址
    uint32_t last_time; // 记录上一次使用的时间,预防有异常的时候可以计算超时;
    uint32_t reserve[4];
};
typedef void *(m_malloc) (int size);
typedef void(m_free)(void *mem);
uint8_t *mem_cache_alloc(struct mem_info **mem_info, uint32_t max_mem_num, uint32_t size, m_malloc c_malloc);
int8_t   mem_cache_free(uint8_t *addr);
void     mem_cache_destroy(struct mem_info **mem_info, uint32_t max_mem_num, m_free c_free);
void     mem_cache_gc(struct mem_info **mem_info, uint32_t max_mem_num, uint32_t max_ttl, m_free c_free);
int32_t  mem_cache_pre_alloc(struct mem_info **mem_info, uint32_t max_mem_num, uint32_t size, m_malloc c_malloc, uint8_t pre_count);
#endif