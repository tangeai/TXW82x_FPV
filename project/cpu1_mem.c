#include "sys_config.h"
#include "basic_include.h"
#include "osal/string.h"
struct cpu1_mem_info
{
    void *skb_heap;
    void *cpu1_heap;
    void *rxbuf_heap;
    uint32_t skb_heap_size;
    uint32_t cpu1_heap_size;
    uint32_t rxbuf_heap_size;
};
struct cpu1_mem_info *cpu1_mem_info_msg = NULL;

void *cpu1_skb_buf()
{
    if(!cpu1_mem_info_msg)
    {
        cpu1_mem_info_msg = os_malloc(sizeof(struct cpu1_mem_info));
    }

    cpu1_mem_info_msg->skb_heap = os_malloc_psram(CONFIG_CORE_SKB_POOL_SIZE);
    cpu1_mem_info_msg->skb_heap_size = CONFIG_CORE_SKB_POOL_SIZE;
    return cpu1_mem_info_msg->skb_heap;
}

void *cpu1_mem_heap()
{
    if(!cpu1_mem_info_msg)
    {
        cpu1_mem_info_msg = os_malloc(sizeof(struct cpu1_mem_info));
    }
    cpu1_mem_info_msg->cpu1_heap = os_malloc(CONFIG_CORE_HEAP_SIZE);
    cpu1_mem_info_msg->cpu1_heap_size = CONFIG_CORE_HEAP_SIZE;
    
    return cpu1_mem_info_msg->cpu1_heap;
}

void *cpu1_RXBUF_heap()
{
    if(!cpu1_mem_info_msg)
    {
        cpu1_mem_info_msg = os_malloc(sizeof(struct cpu1_mem_info));
    }
    #ifdef TXW82X
    /* 如果设置的RXBUF大小小于等于14KB,直接使用ld中SRAM2-3的14KB空间 */
    if (CONFIG_CORE_RXBUF_SIZE <= (14*1024)) {
        cpu1_mem_info_msg->rxbuf_heap = (void*)(0x20068000);
        cpu1_mem_info_msg->rxbuf_heap_size = (14*1024);
    } else {
        cpu1_mem_info_msg->rxbuf_heap = os_malloc(CONFIG_CORE_RXBUF_SIZE);
        cpu1_mem_info_msg->rxbuf_heap_size = CONFIG_CORE_RXBUF_SIZE;
    }
    #else
        cpu1_mem_info_msg->rxbuf_heap = os_malloc(CONFIG_CORE_RXBUF_SIZE);
        cpu1_mem_info_msg->rxbuf_heap_size = CONFIG_CORE_RXBUF_SIZE;
    #endif

    return cpu1_mem_info_msg->rxbuf_heap;
}

void *cpu1_skb_heap_get(uint32_t *size)
{
    if(!cpu1_mem_info_msg)
    {
        if(size)
        {
            *size = 0;
        }
        return NULL;
    }
    if(size)
    {
        *size = cpu1_mem_info_msg->skb_heap_size;
    }
    return cpu1_mem_info_msg->skb_heap;
}

void *cpu1_heap_get(uint32_t *size)
{
    if(!cpu1_mem_info_msg)
    {
        if(size)
        {
            *size = 0;
        }
        return NULL;
    }
    if(size)
    {
        *size = cpu1_mem_info_msg->cpu1_heap_size;
    }
    return cpu1_mem_info_msg->cpu1_heap;
}

void *cpu1_RXBUF_heap_get(uint32_t *size)
{
    if(!cpu1_mem_info_msg)
    {
        if(size)
        {
            *size = 0;
        }
        return NULL;
    }
    if(size)
    {
        *size = cpu1_mem_info_msg->rxbuf_heap_size;
    }
    return cpu1_mem_info_msg->rxbuf_heap;
}

void cpu1_info_free()
{
    if(cpu1_mem_info_msg)
    {
        os_free(cpu1_mem_info_msg);
        cpu1_mem_info_msg = NULL;
    }
}