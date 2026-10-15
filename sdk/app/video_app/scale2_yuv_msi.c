#include "basic_include.h"
#include "dev.h"
#include "devid.h"
#include "hal/scale.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "lib/multimedia/msi.h"
#include "lib/scale/scale_common.h"
#include "lib/scale/scale_dev.h"
#include "stream_define.h"
#include "user_work/user_work.h"
#include "dev/scale/hgscale.h"
#include "mem_cache/mem_cache.h"
#define SCALE2_YUV_TTL 1000

#define STREAM_MALLOC       av_psram_malloc
#define STREAM_FREE         av_psram_free
#define STREAM_LIBC_FREE    av_free
#define STREAM_LIBC_ZALLOC  av_zalloc

#define SCALE2_YUV_MAX      8
#define SCALE2_YUV_OUT_SIZE(w, h) ((w) * (h) * 3 / 2)

#ifndef SCALE2_YUV_BUF_NUM
#define SCALE2_YUV_BUF_NUM  1
#endif

struct scale2_yuv_s
{
    struct os_work       work;
    struct msi          *msi;
    struct scale_device *scale_dev;
    struct mem_info    **mem_info;
    uint32_t             mem_info_size;
    struct framebuff    *in_fb;
    struct framebuff    *out_fb;
    uint16_t             filter_type;
    uint16_t             show_x;
    uint16_t             show_y;
    uint16_t             out_w;
    uint16_t             out_h;
    uint16_t             start_x;
    uint16_t             start_y;
    uint16_t             tailor_w;
    uint16_t             tailor_h;
    uint8_t              stream_type;
    uint8_t              loc_mode;
    uint8_t              stype;
    uint8_t              busy : 2, done : 2, err : 2, unlock : 2;
    uint8_t              del;
};

static void scale2_yuv_unlock(struct scale2_yuv_s *scale2_yuv)
{
    if (scale2_yuv->unlock)
    {
        scale_mutex_unlock(2, SCALE_LOCK_YUV_SCALE2);
        scale2_yuv->unlock = 0;
    }
}

static struct framebuff *scale2_yuv_alloc_output_fb(struct scale2_yuv_s *scale2_yuv, uint32_t size)
{
    uint8_t          *data;
    struct framebuff *fb;

    data = mem_cache_alloc(scale2_yuv->mem_info, scale2_yuv->mem_info_size, size, STREAM_MALLOC);
    if (!data)
    {
        return NULL;
    }
    fb   = fb_alloc(data, size, F_YUV << 8 | scale2_yuv->stype, scale2_yuv->msi);
    if (!fb)
    {
        mem_cache_free(data);
        return NULL;
    }

    fb->priv = (void *)STREAM_LIBC_ZALLOC(sizeof(struct yuv_arg_s));
    if (!fb->priv)
    {
        msi_delete_fb(NULL, fb);
        return NULL;
    }

    return fb;
}

static void scale2_yuv_cleanup(struct scale2_yuv_s *scale2_yuv, uint8_t output)
{
    struct framebuff *out_fb = scale2_yuv->out_fb;
    struct framebuff *in_fb  = scale2_yuv->in_fb;

    scale2_yuv->out_fb = NULL;
    scale2_yuv->in_fb  = NULL;
    scale2_yuv->busy   = 0;
    scale2_yuv->done   = 0;
    scale2_yuv->err    = 0;

    if (out_fb)
    {
        if (output)
        {
            msi_output_fb(scale2_yuv->msi, out_fb);
        }
        else
        {
            msi_delete_fb(NULL, out_fb);
        }
    }

    if (in_fb)
    {
        msi_delete_fb(NULL, in_fb);
    }
}

static void scale2_yuv_done(uint32 irq_flag, uint32 irq_data, uint32 param1)
{
    struct scale2_yuv_s *scale2_yuv = (struct scale2_yuv_s *)irq_data;

    scale2_yuv->done = 1;
    scale2_yuv->err  = 0;
    scale2_yuv_unlock(scale2_yuv);
}

static void scale2_yuv_ov(uint32 irq_flag, uint32 irq_data, uint32 param1)
{
    struct scale2_yuv_s *scale2_yuv = (struct scale2_yuv_s *)irq_data;

    scale2_yuv->done = 1;
    scale2_yuv->err  = 1;
    os_printf("scale2 yuv ov\r\n");
    scale2_yuv_unlock(scale2_yuv);
}

static int32_t scale2_yuv_start(struct scale2_yuv_s *scale2_yuv, struct framebuff *in_fb, struct framebuff *out_fb)
{
    struct yuv_arg_s *in_arg;
    struct yuv_arg_s *out_arg;
    uint8_t          *out_data;
    uint32_t          in_w;
    uint32_t          in_h;
    uint32_t          src_y;
    uint32_t          src_u;
    uint32_t          src_v;
    struct scale_cfg  scale_cfg;

    if (!scale2_yuv || !scale2_yuv->scale_dev || !in_fb || !out_fb || !in_fb->data || !in_fb->priv || !out_fb->data || !out_fb->priv)
    {
        return RET_ERR;
    }

    in_arg  = (struct yuv_arg_s *)in_fb->priv;
    out_arg = (struct yuv_arg_s *)out_fb->priv;
    in_w    = in_arg->out_w;
    in_h    = in_arg->out_h;

    if (!in_w || !in_h)
    {
        return RET_ERR;
    }

    out_data  = out_fb->data;
    src_y     = in_arg->y_off ? in_arg->y_off : (uint32_t)in_fb->data;
    src_u     = in_arg->u_off ? in_arg->u_off : (uint32_t)in_fb->data + in_arg->y_size + in_arg->uv_off;
    src_v     = in_arg->v_off ? in_arg->v_off : (uint32_t)in_fb->data + in_arg->y_size + in_arg->y_size / 4 + in_arg->uv_off;

    out_arg->type       = YUV_ARG_NORMAL;
    out_arg->y_size     = scale2_yuv->out_w * scale2_yuv->out_h;
    out_arg->y_off      = (uint32_t)out_data;
    out_arg->u_off      = (uint32_t)out_data + out_arg->y_size;
    out_arg->v_off      = (uint32_t)out_data + out_arg->y_size + out_arg->y_size / 4;
    out_arg->uv_off     = 0;
    out_arg->out_w      = scale2_yuv->out_w;
    out_arg->out_h      = scale2_yuv->out_h;
    out_arg->x          = scale2_yuv->show_x;
    out_arg->y          = scale2_yuv->show_y;
    out_arg->del        = &scale2_yuv->del;
    out_arg->dispcnt    = in_arg->dispcnt;
    out_arg->magic      = in_arg->magic;
    out_arg->video_only = 0;

    out_fb->mtype   = F_YUV;
    out_fb->stype   = scale2_yuv->stype;
    out_fb->time    = in_fb->time;
    out_fb->srcID   = in_fb->srcID;
    out_fb->datatag = in_fb->datatag;
    out_fb->len     = SCALE2_YUV_OUT_SIZE(scale2_yuv->out_w, scale2_yuv->out_h);

    scale_request_irq(scale2_yuv->scale_dev, FRAME_END, (scale_irq_hdl)&scale2_yuv_done, (uint32)scale2_yuv);
    scale_request_irq(scale2_yuv->scale_dev, INBUF_OV, (scale_irq_hdl)&scale2_yuv_ov, (uint32)scale2_yuv);

    memset(&scale_cfg, 0, sizeof(scale_cfg));
    scale_cfg.scale_dev   = scale2_yuv->scale_dev;
    scale_cfg.yinbuf      = src_y;
    scale_cfg.uinbuf      = src_u;
    scale_cfg.vinbuf      = src_v;
    scale_cfg.yuvoutbuf   = (uint32_t)out_data;
    scale_cfg.in_w        = in_w;
    scale_cfg.in_h        = in_h;
    scale_cfg.out_w       = scale2_yuv->out_w;
    scale_cfg.out_h       = scale2_yuv->out_h;
    scale_cfg.start_x     = scale2_yuv->start_x;
    scale_cfg.start_y     = scale2_yuv->start_y;
    scale_cfg.tailor_w    = scale2_yuv->tailor_w;
    scale_cfg.tailor_h    = scale2_yuv->tailor_h;
    scale_cfg.stream_type = FRAME_YUV420P;
    scale_cfg.loc_mode    = scale2_yuv->loc_mode ? scale2_yuv->loc_mode : SCALE_ALIGN_CENTER;

    scale2_config_for_msi(&scale_cfg);
    scale_set_new_frame(scale2_yuv->scale_dev, 1);
    return RET_OK;
}

static int32 scale2_yuv_work(struct os_work *work)
{
    struct scale2_yuv_s *scale2_yuv = (struct scale2_yuv_s *)work;
    struct framebuff    *in_fb;
    struct framebuff    *out_fb;
    int32_t              ret;

    if (scale2_yuv->done)
    {
        scale2_yuv_cleanup(scale2_yuv, scale2_yuv->err ? 0 : 1);
        goto scale2_yuv_work_end;
    }

    if (scale2_yuv->busy)
    {
        goto scale2_yuv_work_end;
    }

    if (!scale2_yuv->in_fb)
    {
        in_fb = msi_get_fb(scale2_yuv->msi, 0);
        if (!in_fb)
        {
            goto scale2_yuv_work_end;
        }
        scale2_yuv->in_fb = in_fb;
    }

    if (!msi_output_fb(scale2_yuv->msi, NULL))
    {
        msi_delete_fb(NULL, scale2_yuv->in_fb);
        scale2_yuv->in_fb = NULL;
        goto scale2_yuv_work_end;
    }

    ret = scale_mutex_lock(2, SCALE_LOCK_YUV_SCALE2, NULL);
    if (ret)
    {
        goto scale2_yuv_work_end;
    }
    scale2_yuv->unlock = 1;
    
    out_fb = scale2_yuv_alloc_output_fb(scale2_yuv, SCALE2_YUV_OUT_SIZE(scale2_yuv->out_w, scale2_yuv->out_h));
    if (!out_fb)
    {
        goto scale2_yuv_work_end;
    }

    if (!out_fb->data)
    {
        msi_delete_fb(NULL, out_fb);
        goto scale2_yuv_work_end;
    }

    in_fb              = scale2_yuv->in_fb;
    scale2_yuv->out_fb = out_fb;
    scale2_yuv->busy   = 1;
    scale2_yuv->done   = 0;
    scale2_yuv->err    = 0;

    if (scale2_yuv_start(scale2_yuv, in_fb, out_fb))
    {
        scale2_yuv_unlock(scale2_yuv);
        scale2_yuv_cleanup(scale2_yuv, 0);
    }

scale2_yuv_work_end:
    if (!scale2_yuv->busy)
    {
        scale2_yuv_unlock(scale2_yuv);
    }
    mem_cache_gc(scale2_yuv->mem_info, scale2_yuv->mem_info_size, SCALE2_YUV_TTL, STREAM_FREE);
    os_run_work_delay(work, 1);
    return 0;
}

static int32_t scale2_yuv_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    int32_t              ret        = RET_OK;
    struct scale2_yuv_s *scale2_yuv = (struct scale2_yuv_s *)msi->priv;

    switch (cmd_id)
    {
        case MSI_CMD_POST_DESTROY:
        {
            mem_cache_destroy(scale2_yuv->mem_info, scale2_yuv->mem_info_size, STREAM_FREE);
            STREAM_LIBC_FREE(scale2_yuv);
        }
        break;

        case MSI_CMD_PRE_DESTROY:
        {
            os_work_cancle2(&scale2_yuv->work, 1);
            scale_close(scale2_yuv->scale_dev);
            scale2_yuv_unlock(scale2_yuv);
            if (scale2_yuv->out_fb)
            {
                msi_delete_fb(NULL, scale2_yuv->out_fb);
                scale2_yuv->out_fb = NULL;
            }
            if (scale2_yuv->in_fb)
            {
                msi_delete_fb(NULL, scale2_yuv->in_fb);
                scale2_yuv->in_fb = NULL;
            }
        }
        break;

        case MSI_CMD_FREE_FB:
        {
            struct framebuff *fb = (struct framebuff *)param1;
            if (fb)
            {
                if (fb->priv)
                {
                    STREAM_LIBC_FREE(fb->priv);
                    fb->priv = NULL;
                }
                mem_cache_free(fb->data);
                fb->data = NULL;
            }
        }
        break;

        case MSI_CMD_TRANS_FB:
        {
            struct framebuff *fb = (struct framebuff *)param1;

            if (!fb || fb->mtype != F_YUV)
            {
                ret = RET_ERR;
            }
            else if (scale2_yuv->filter_type && fb->stype != scale2_yuv->filter_type)
            {
                ret = RET_ERR;
            }
        }
        break;

        case MSI_CMD_TRANS_FB_END:
        {
            os_run_work(&scale2_yuv->work);
        }
        break;
        
        case MSI_CMD_SCALE2:
        {
            uint32_t cmd_self = param1;
            uint32_t arg      = param2;
            switch(cmd_self)
            {
                case MSI_SCALE2_SET_LOC_MODE:
                {
                    scale2_yuv->loc_mode = arg & 0xff;
                }
                break;
                case MSI_SCALE2_SET_X_Y:
                {
                    scale2_yuv->loc_mode = SCALE_MANUAL;
                    scale2_yuv->start_x = (arg >> 16) & 0xffff;
                    scale2_yuv->start_y = arg & 0xffff;
                }
                break;
                case MSI_SCALE2_SET_W_H:
                {
                    scale2_yuv->tailor_w = (arg >> 16) & 0xffff;
                    scale2_yuv->tailor_h = arg & 0xffff;
                }
                break;
                case MSI_SCALE2_SET_STYPE:
                {
                    scale2_yuv->stype = arg & 0xff;
                }
                break;
            }
        }
        break;

        default:
            break;
    }

    return ret;
}

struct msi *scale2_yuv_msi_init(const char *name, uint16_t filter_type, uint16_t out_w, uint16_t out_h, uint16_t show_x, uint16_t show_y)
{
    uint8_t              isnew;
    struct msi          *msi;
    struct scale2_yuv_s *scale2_yuv;

    msi = msi_new(name, SCALE2_YUV_MAX, &isnew);
    if (!msi)
    {
        return NULL;
    }

    scale2_yuv = (struct scale2_yuv_s *)msi->priv;
    if (isnew)
    {
        scale2_yuv = (struct scale2_yuv_s *)STREAM_LIBC_ZALLOC(sizeof(struct scale2_yuv_s) + sizeof(struct mem_info *) * SCALE2_YUV_BUF_NUM);
        if (!scale2_yuv)
        {
            msi_destroy(msi);
            return NULL;
        }

        scale2_yuv->msi           = msi;
        scale2_yuv->scale_dev     = (struct scale_device *)dev_get(HG_SCALE2_DEVID);
        scale2_yuv->mem_info      = (struct mem_info **)(scale2_yuv + 1);
        scale2_yuv->mem_info_size = SCALE2_YUV_BUF_NUM;
        msi->priv                 = scale2_yuv;
        msi->action               = scale2_yuv_action;
        msi->enable               = 1;

        OS_WORK_INIT(&scale2_yuv->work, scale2_yuv_work, 0);
        os_run_work_delay(&scale2_yuv->work, 1);
    }

    scale2_yuv->filter_type = filter_type;
    scale2_yuv->show_x      = show_x;
    scale2_yuv->show_y      = show_y;
    scale2_yuv->out_w       = out_w;
    scale2_yuv->out_h       = out_h;
    msi->enable             = 1;

    return msi;
}
