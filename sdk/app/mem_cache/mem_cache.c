#include "basic_include.h"
#include "mem_cache.h"
// 检查是否有足够空间的内存,如果没有就申请
// size:需要申请的空间大小
uint8_t *mem_cache_alloc(struct mem_info **mem_info, uint32_t max_mem_num, uint32_t size, m_malloc c_malloc)
{
    int8_t   free_mem_index   = -1;
    int8_t   malloc_mem_index = -1;
    uint32_t flags            = disable_irq();
    // 先去搜索是否有足够空间的内存,如果没有就申请
    for (int i = 0; i < max_mem_num; i++)
    {
        if (!mem_info[i])
        {
            if (free_mem_index == -1)
            {
                free_mem_index = i;
            }
        }
        else
        {
            if (!mem_info[i]->used && mem_info[i]->size >= size)
            {
                if (malloc_mem_index == -1)
                {
                    malloc_mem_index = i;
                }
                // 使用最优的内存块
                else
                {
                    // 内存块的size有区别,使用适配的内存块
                    if (mem_info[i]->size < mem_info[malloc_mem_index]->size)
                    {
                        malloc_mem_index = i;
                    }
                    // 内存块size不一致,则使用最近使用的内存块?
                    else if (mem_info[i]->size == mem_info[malloc_mem_index]->size)
                    {
                        // 时间越大,越接近最近使用
                        if (mem_info[i]->last_time > mem_info[malloc_mem_index]->last_time)
                        {
                            malloc_mem_index = i;
                        }
                    }
                }
            }
        }
    }

    if (malloc_mem_index != -1)
    {
        // 更新时间
        mem_info[malloc_mem_index]->used      = 1;
        mem_info[malloc_mem_index]->last_time = os_jiffies();
        free_mem_index                        = -1;
    }
    enable_irq(flags);
    // 没有找到,重新申请一下空间块
    // 暂时不要中断调用,第一:占用时间对于中断可能时间过长,第二:不支持多线程空间申请
    if (c_malloc && (free_mem_index != -1))
    {
        if (c_malloc)
        {
            mem_info[free_mem_index] = c_malloc(sizeof(struct mem_info) + size);
            MEM_CACHE_PRINTF("%s:%d\tfree_mem_index:%d\n", __FUNCTION__, __LINE__, free_mem_index);
        }

        if (mem_info[free_mem_index])
        {
            sys_dcache_invalid_range((uint32_t *) mem_info[free_mem_index], sizeof(struct mem_info) + size);
            mem_info[free_mem_index]->used      = 1;
            mem_info[free_mem_index]->size      = size;
            mem_info[free_mem_index]->addr      = (uint32_t) (mem_info[free_mem_index] + 1);
            mem_info[free_mem_index]->last_time = os_jiffies();
            malloc_mem_index                    = free_mem_index;
        }
    }
    if (malloc_mem_index != -1)
    {
        return (uint8_t *) mem_info[malloc_mem_index]->addr;
    }
    return NULL;
}

// 内存释放,不需要保护
int8_t mem_cache_free(uint8_t *addr)
{
    int8_t ret = -1;
    if (!addr)
    {
        return ret;
    }
    struct mem_info *mem_info = (struct mem_info *) addr;
    if (mem_info)
    {
        mem_info       = mem_info - 1;
        mem_info->used = 0;
        ret            = 0;
    }

    return ret;
}

// 所有空间释放,不需要保护,应用代码保证
void mem_cache_destroy(struct mem_info **mem_info, uint32_t max_mem_num, m_free c_free)
{
    for (int i = 0; i < max_mem_num; i++)
    {
        if (mem_info[i])
        {
            if (c_free)
            {
                MEM_CACHE_PRINTF("%s:%d\tfree_mem_index:%d\tlast time:%d\n", __FUNCTION__, __LINE__, i, mem_info[i]->last_time);
                c_free(mem_info[i]);
            }
        }
    }
}

// 这里为了考虑多线程,所以使用中断保护,然后轮询是否有需要删除,把需要删除的内存块,放到del_buf中,最后批量释放(防止卡中断太久)
void mem_cache_gc(struct mem_info **mem_info, uint32_t max_mem_num, uint32_t max_ttl, m_free c_free)
{
    uint32_t del_count = 0;
    void    *del_buf[10];
    uint32_t iter = 0;
    if (c_free)
    {
        while (1)
        {
            uint32_t flags = disable_irq();
            for (; iter < max_mem_num; iter++)
            {
                // 如果没有被使用,并且超时,则去释放
                if (mem_info[iter] && !mem_info[iter]->used && os_jiffies() - mem_info[iter]->last_time > max_ttl)
                {
                    MEM_CACHE_PRINTF("%s:%d\tfree_mem_index:%d\tlast_time:%d\n", __FUNCTION__, __LINE__, iter, mem_info[iter]->last_time);
                    del_buf[del_count] = mem_info[iter];
                    mem_info[iter]     = NULL;
                    del_count++;
                    if (del_count >= 10)
                    {
                        break;
                    }
                }
            }
            enable_irq(flags);

            if (del_count)
            {
                for (int i = 0; i < del_count; i++)
                {
                    c_free(del_buf[i]);
                }
                del_count = 0;
            }
            else
            {
                break;
            }
        }
    }
}

// 预先分配空间,尽量保持mem_cache有pre_count个空间块
int32_t mem_cache_pre_alloc(struct mem_info **mem_info, uint32_t max_mem_num, uint32_t size, m_malloc c_malloc, uint8_t pre_count)
{
    uint32_t free_count = 0;
    uint32_t flags      = disable_irq();
    // 先去搜索是否有足够空间的内存,如果没有就申请
    for (int i = 0; i < max_mem_num; i++)
    {
        if (mem_info[i] && !mem_info[i]->used)
        {
            free_count++;
            if (free_count >= pre_count)
            {
                break;
            }
        }
    }
    enable_irq(flags);

    // 空闲块已经达到预期,则退出
    if (free_count >= pre_count)
    {
        return free_count;
    }

    // 没有达到预期,则去申请内存,然后放到对应空闲位置
    uint32_t iter  = 0;
    uint8_t *m_buf = NULL;
    for (; iter < max_mem_num; iter++)
    {
        if (!m_buf)
        {
            m_buf = c_malloc(sizeof(struct mem_info) + size);
            if (m_buf)
            {
                sys_dcache_invalid_range((uint32_t *) m_buf, sizeof(struct mem_info) + size);
            }
        }
        if (!m_buf)
        {
            break;
        }
        flags = disable_irq();

        if (!mem_info[iter])
        {
            mem_info[iter] = (struct mem_info *) m_buf;
            m_buf          = NULL;
            free_count++;
            mem_info[iter]->used      = 0;
            mem_info[iter]->size      = size;
            mem_info[iter]->addr      = (uint32_t) (mem_info[iter] + 1);
            mem_info[iter]->last_time = os_jiffies();
        }
        enable_irq(flags);
        // 空闲块已经达到预期,则退出
        if (free_count >= pre_count)
        {
            return free_count;
        }
    }

    return free_count;
}