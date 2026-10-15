
/*******************************************
这个文件主要为了将h264的数据buf变成framebuff,
然后发送出去,仅仅做了接口去实现

******************************************* */
#include "basic_include.h"
#include "lib/multimedia/msi.h"
#include "stream_define.h"
#include "lib/video/h264/h264_drv.h"
#include "osal/work.h"
#include "hal/h264.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "user_work/user_work.h"

#ifndef SAVE_COUNT
#define SAVE_COUNT 4
#endif

#define MAX_BYTES 0

#if VIDEO_YUV_RANGE_TYPE

#undef MAX_BYTES
#define MAX_BITS  160
#define MAX_BYTES (MAX_BITS / 8)
#endif

// data申请空间函数
#define STREAM_MALLOC av_psram_malloc
#define STREAM_FREE   av_psram_free
#define STREAM_ZALLOC av_psram_zalloc

struct h264_buf_s
{
    uint8_t count;
};

static uint8_t get_nal_size(uint8_t *buf, uint32_t size, uint32_t *offset)
{
    uint32_t pos = 0;
    while ((size - pos) > 3)
    {
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)
        {
            *offset = pos;
            return 3;
        }

        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1)
        {
            *offset = pos;
            return 4;
        }

        pos++;
    }
    return 0;
}

// 只是获取pps和sps的nalsize
static uint8_t *get_sps_pps_nal_size(uint8_t *buf, uint32_t size, uint32_t *nal_size, uint8_t *head_size)
{
    uint32_t offset;
    uint8_t  nal_head_size = get_nal_size(buf, size, &offset);
    uint8_t  nal_type;
    uint8_t *ret_buf = NULL;
    // 找到头部,检查类型
    if (nal_head_size && offset + nal_head_size < size)
    {
        nal_type = buf[nal_head_size + offset] & 0x1f;

        // 找到sps和pps就返回长度和偏移(相对buf的偏移)
        if (nal_type == 7 || nal_type == 8)
        {
            // 查找下一个nal
            nal_head_size = get_nal_size(buf + offset + nal_head_size, size - (offset + nal_head_size), nal_size);
            // os_printf("nal_head_size:%d\tnal_size:%d\n",nal_head_size,*nal_size);
            if (nal_head_size)
            {
                // 偏移到nal的头部

                ret_buf    = buf + offset;
                // 返回nal的头size
                *head_size = nal_head_size;
            }
        }
    }

    return ret_buf;
}

int32_t h264_buf_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    int32_t ret = RET_OK;
    switch (cmd_id)
    {
        case MSI_CMD_POST_DESTROY:
        {
            STREAM_FREE(msi->priv);
        }
        break;
        case MSI_CMD_FREE_FB:
        {
            struct framebuff *fb = (struct framebuff *) param1;
            //os_printf("free fb:%X\n",fb);
            if (fb)
            {
                if (fb->priv)
                {
                    STREAM_FREE(fb->priv);
                }
                if (fb->data)
                {
                    STREAM_FREE(fb->data);
                }
            }
        }
        break;
    }
    return ret;
}
struct msi *h264_buf_msi(const char *msi_name)
{
    uint8_t     isnew = 0;
    struct msi *m     = msi_new(msi_name, 0, &isnew);
    if (m && isnew)
    {
        m->priv   = STREAM_ZALLOC(sizeof(struct h264_buf_s));
        m->action = h264_buf_action;
        m->enable = 1;
    }
    return m;
}

// 组成一个h264的framebuff,然后发送出去
int32_t h264_buf_put(struct msi *m, uint16_t w, uint16_t h, uint8_t type, uint8_t *buf, uint32_t h264_len, uint32_t time)
{
    struct h264_buf_s *h264_buf_priv = (struct h264_buf_s *) m->priv;
    uint8_t           *h264_buf      = (uint8_t *) STREAM_MALLOC(h264_len);
    ASSERT(h264_buf);
    hw_memcpy(h264_buf, buf, h264_len);
    sys_dcache_clean_range((uint32_t *) h264_buf, h264_len);
    struct framebuff *fb = fb_alloc(h264_buf, h264_len, 0, m);

    //os_printf("gen fb:%X\tm:%X\n",fb,m);
    fb->data             = h264_buf;
    fb->mtype            = F_H264;
    fb->stype            = FSTYPE_NET_H264;
    fb->time             = time;
    fb->srcID            = FRAMEBUFF_SOURCE_NET;
    // I帧
    if (type == 1)
    {
        // 先去读取sps和pps的长度
        uint8_t  nal_head_size;
        uint32_t nal_size;
        uint8_t *sps_pps_buf = fb->data;

        uint8_t *pps_buf = NULL;
        uint8_t *sps_buf = NULL;
        uint8_t  pps_len = 0, sps_len = 0;
        uint8_t  pps_sps_times = 0;
        uint8_t *next_nal_buf  = fb->data;

        // 读取sps和pps,仅仅读取两次,没有就退出
        while (sps_pps_buf && pps_sps_times < 2)
        {
            sps_pps_buf = get_sps_pps_nal_size(next_nal_buf, 64, &nal_size, &nal_head_size);
            if (sps_pps_buf && (sps_pps_buf[nal_head_size] & 0x1f) == 7)
            {
                sps_buf      = sps_pps_buf + nal_head_size;
                sps_len      = nal_size;
                next_nal_buf = sps_pps_buf + nal_size + nal_head_size;
            }
            else if (sps_pps_buf && (sps_pps_buf[nal_head_size] & 0x1f) == 8)
            {
                pps_buf      = sps_pps_buf + nal_head_size;
                pps_len      = nal_size;
                next_nal_buf = sps_pps_buf + nal_size + nal_head_size;
            }
            // 不匹配,就不再去获取pps或者sps了
            else
            {
                break;
            }
            // os_printf("sps_pps_buf[nal_head_size]& 0x1f):%d\n",sps_pps_buf[nal_head_size]& 0x1f);
            pps_sps_times++;
        }

        struct fb_h264_s *priv = (struct fb_h264_s *) STREAM_ZALLOC(sizeof(struct fb_h264_s));

        // 寻找一下nal头有多少字节
        uint32_t pos;
        uint8_t  h264_nal_size = get_nal_size(next_nal_buf, 16, &pos);

        priv->pps       = pps_buf;
        priv->pps_len   = pps_len;
        priv->sps       = sps_buf;
        priv->sps_len   = sps_len;
        fb->priv        = (void *) priv;
        priv->type      = 1;
        priv->count     = h264_buf_priv->count++;
        priv->start_len = next_nal_buf - fb->data + pos + h264_nal_size;
        priv->w         = w;
        priv->h         = h;
        //os_printf("priv->start_len:%d\ttype:%d\n",priv->start_len,fb->data[priv->start_len]&0x1f);
        //os_printf("pps:%d\tsps:%d\n",pps_buf[0]&0x1f,sps_buf[0]&0x1f);
       // dump_hex("h264:",fb->data, h264_len,1);
#if 0
       uint8_t path[32];
       os_sprintf(path,"0:%08d.h264",(uint32_t)os_jiffies());
       void *fp = osal_fopen(path,"wb");
       os_printf("fp:%X\tpath:%s\n",fp,path);
       if(fp)
       {
        osal_fwrite(fb->data,h264_len,1,fp);
        osal_fclose(fp);
       }
#endif
    }
    else
    {
        struct fb_h264_s *priv = (struct fb_h264_s *) STREAM_ZALLOC(sizeof(struct fb_h264_s));
        uint32_t          pos;
        uint8_t           h264_nal_size = get_nal_size(fb->data, 16, &pos);
        priv->type                      = 2;
        fb->priv                        = (void *) priv;
        priv->count                     = h264_buf_priv->count++;
        priv->start_len                 = pos + h264_nal_size;
        priv->w                         = w;
        priv->h                         = h;
    }
    msi_output_fb(m, fb);
    return 0;
}