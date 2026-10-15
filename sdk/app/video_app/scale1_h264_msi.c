#include "basic_include.h"
#include "lib/multimedia/msi.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "stream_define.h"
#include "hal/scale.h"
#include "dev/scale/hgscale.h"
#include "lib/scale/scale_common.h"
#include "lib/video/dvp/jpeg/jpg.h"
#include "lib/video/vpp/vpp_dev.h"
#include "hal/timer_device.h"
#include "video_msi.h"

#define STREAM_LIBC_MALLOC              av_malloc
#define STREAM_LIBC_FREE                av_free
#define STREAM_LIBC_ZALLOC              av_zalloc

#define SCALE1_H264_RECV_MAX            2
#define SCALE1_H264_LINE_NUM            16
#define SCALE1_H264_DONE_TIMEOUT        500
#define SCALE1_H264_WAIT_TIMEOUT        500
#define SCALE1_H264_TIMER_INTERVAL_US   100
#define SCALE1_H264_TIMER_WAIT_MAX      (SCALE1_H264_WAIT_TIMEOUT * 1000 / SCALE1_H264_TIMER_INTERVAL_US)

enum
{
    MSI_SCALE1_H264_THREAD_DEAD       = BIT(0),
    MSI_SCALE1_H264_THREAD_STOP       = BIT(1),
    MSI_SCALE1_H264_THREAD_KICK       = BIT(2),
    MSI_SCALE1_H264_THREAD_READY      = BIT(3),
    MSI_SCALE1_H264_THREAD_TIMEOUT    = BIT(4),
};

struct scale1_h264_s
{
    struct msi          *msi;
    struct msi          *register_h264_msi;
    struct scale_device *scale_dev;
    struct framebuff    *fb;
    struct os_event      evt;
    uint8_t             *line_buf;
    uint32_t             line_buf_size;
    uint8_t              scale_configured;
    uint16_t             filter_type;
    uint16_t             out_w;
    uint16_t             out_h;
};

struct scale1_h264_timer_s
{
    struct scale1_h264_s *scale1_h264;
    struct scale_device  *scale_dev;
    struct timer_device  *timer_dev;
    uint32_t              target_height;
    uint32_t              wait_count;
    volatile uint8_t      active;
};

static int32_t scale1_h264_done(uint32_t irq_flag, uint32_t irq_data, uint32_t param1)
{
    struct scale1_h264_s *scale1_h264 = (struct scale1_h264_s *)irq_data;

    os_event_set(&scale1_h264->evt, MSI_SCALE1_H264_THREAD_KICK, NULL);
    return 0;
}

static void scale1_h264_timer_wait(uint32 irq_data, uint32 irq_flag)
{
    struct scale1_h264_timer_s      *timer_wait = (struct scale1_h264_timer_s *)irq_data;
    uint32_t                         event_flag;

    if (!(irq_flag & TIMER_IRQ_FLAG_TIMEOUT) || !timer_wait->active)
    {
        return;
    }

    if (scale_get_heigh_cnt(timer_wait->scale_dev) > timer_wait->target_height)
    {
        event_flag = MSI_SCALE1_H264_THREAD_READY;
    }
    else
    {
        timer_wait->wait_count++;
        if (timer_wait->wait_count < SCALE1_H264_TIMER_WAIT_MAX)
        {
            return;
        }
        event_flag = MSI_SCALE1_H264_THREAD_TIMEOUT;
    }

    timer_wait->active = 0;
    timer_device_stop(timer_wait->timer_dev);
    os_event_set(&timer_wait->scale1_h264->evt, event_flag, NULL);
}

static int32_t scale1_h264_prepare_line_buf(struct scale1_h264_s *scale1_h264, uint32_t w, uint32_t line_num)
{
    uint32_t need_size = line_num * 2 * w * 3 / 2;

    if (scale1_h264->line_buf && scale1_h264->line_buf_size >= need_size)
    {
        return RET_OK;
    }

    if (scale1_h264->line_buf)
    {
        STREAM_LIBC_FREE(scale1_h264->line_buf);
        scale1_h264->line_buf      = NULL;
        scale1_h264->line_buf_size = 0;
    }

    scale1_h264->line_buf = (uint8_t *)STREAM_LIBC_MALLOC(need_size);
    if (!scale1_h264->line_buf)
    {
        return RET_ERR;
    }

    scale1_h264->line_buf_size = need_size;
    return RET_OK;
}

static int32_t scale1_soft_from_psram_to_enc(struct scale1_h264_s *scale1_h264, uint8_t *line_buf, int line_num, uint8_t *psram_data, uint32_t w, uint32_t h, uint32_t ow, uint32_t oh)
{
    struct scale_device            *scale_dev = scale1_h264->scale_dev;
    struct timer_device            *timer_dev;
    struct scale1_h264_timer_s      timer_wait = {0};
    uint32_t                        flags;
    uint32_t                        timer_period;
    uint16                          icount = 2;

    timer_dev = (struct timer_device *)dev_get(HG_SIMTMR4_DEVID);
    if (!timer_dev)
    {
        os_printf(KERN_ERR "scale1 h264 get timer failed\r\n");
        return RET_ERR;
    }

    if (timer_device_open(timer_dev, TIMER_TYPE_PERIODIC, 0))
    {
        os_printf(KERN_ERR "scale1 h264 open timer3 failed\r\n");
        return RET_ERR;
    }

    timer_wait.scale1_h264 = scale1_h264;
    timer_wait.scale_dev   = scale_dev;
    timer_wait.timer_dev   = timer_dev;
    timer_period           = system_clock_get() / (1000000 / SCALE1_H264_TIMER_INTERVAL_US);
    
    if (!scale1_h264->scale_configured)
    {
        os_printf("%s  %d*%d====>%d*%d\r\n", __func__, w, h, ow, oh);
        scale_close(scale_dev);
        scale_set_in_out_size(scale_dev, w, h, ow, oh);
        scale_set_step(scale_dev, w, h, ow, oh);
        scale_set_start_addr(scale_dev, 0, 0);
        scale_set_data_from_vpp(scale_dev, 0);
        scale_set_line_buf_num(scale_dev, line_num * 2);
        scale_request_irq(scale_dev, FRAME_END, (scale_irq_hdl)&scale1_h264_done, (uint32_t)scale1_h264);
        scale_release_irq(scale_dev, INBUF_OV);
        scale_release_irq(scale_dev, ERROR_PEND);
        scale1_h264->scale_configured = 1;
    }
    scale_open(scale_dev);
    scale_set_inbuf_num(scale_dev, 0, 0);
    if (scale_get_inbuf_num(scale_dev) == 0)
    {
        icount = 2;
        scale_set_new_frame(scale_dev, 1);
    }
    else
    {
        os_printf(KERN_ERR "scale1 h264 input buffer reset failed\r\n");
        goto scale1_h264_timer_error;
    }
    
    hw_memcpy_no_cache(line_buf, psram_data, (line_num * icount) * w);
    hw_memcpy_no_cache(line_buf + (line_num * icount) * w, psram_data + h * w, (line_num * icount) * w / 4);
    hw_memcpy_no_cache(line_buf + (line_num * icount) * w + (line_num * icount) * w / 4, psram_data + h * w + h * w / 4, (line_num * icount) * w / 4);
    scale_set_in_yaddr(scale_dev, (uint32)line_buf);
    scale_set_in_uaddr(scale_dev, (uint32)line_buf + (line_num * icount) * w);
    scale_set_in_vaddr(scale_dev, (uint32)line_buf + (line_num * icount) * w + (line_num * icount) * w / 4);
    scale_set_inbuf_num(scale_dev, icount * line_num - 1, line_num * 2 - 1);

    while (icount < ((h + line_num - 1) / line_num))
    {
        if (scale_get_heigh_cnt(scale_dev) > ((icount - 1) * line_num))
        {
            if ((icount % 2) == 0)
            {
                hw_memcpy_no_cache(line_buf, psram_data + (line_num * icount) * w, line_num * w);
                hw_memcpy_no_cache(line_buf + line_num * 2 * w, psram_data + h * w + (line_num * icount) * w / 4, line_num * w / 4);
                hw_memcpy_no_cache(line_buf + line_num * 2 * w + line_num * 2 * w / 4, psram_data + h * w + h * w / 4 + (line_num * icount) * w / 4, line_num * w / 4);
            }
            else
            {
                hw_memcpy_no_cache(line_buf + line_num * w, psram_data + (line_num * icount) * w, line_num * w);
                hw_memcpy_no_cache(line_buf + line_num * 2 * w + line_num * 2 * w / 8, psram_data + h * w + (line_num * icount) * w / 4, line_num * w / 4);
                hw_memcpy_no_cache(line_buf + line_num * 2 * w + line_num * 2 * w / 4 + line_num * 2 * w / 8, psram_data + h * w + h * w / 4 + (line_num * icount) * w / 4, line_num * w / 4);
            }
            icount++;
            if (icount % 2)
            {
                scale_set_inbuf_num(scale_dev, icount * line_num, line_num);
            }
            else
            {
                scale_set_inbuf_num(scale_dev, icount * line_num, 0);
            }
        }
        else
        {
            flags                    = 0;
            timer_wait.target_height = (icount - 1) * line_num;
            timer_wait.wait_count    = 0;
            timer_wait.active        = 1;

            if (timer_device_start(timer_dev, timer_period, scale1_h264_timer_wait, (uint32)&timer_wait))
            {
                timer_wait.active = 0;
                os_printf(KERN_ERR "scale1 h264 start timer3 failed\r\n");
                goto scale1_h264_timer_error;
            }

            os_event_wait(&scale1_h264->evt, MSI_SCALE1_H264_THREAD_READY | MSI_SCALE1_H264_THREAD_TIMEOUT, &flags, OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, -1);
            if (flags & MSI_SCALE1_H264_THREAD_TIMEOUT)
            {
                os_printf(KERN_ERR "scale1 h264 wait input timeout,height:%d\r\n", scale_get_heigh_cnt(scale_dev));
                goto scale1_h264_timer_error;
            }
        }
    }

    timer_wait.active = 0;
    timer_device_stop(timer_dev);
    timer_device_close(timer_dev);
    int32 ret = os_event_wait(&scale1_h264->evt, MSI_SCALE1_H264_THREAD_KICK, NULL, OS_EVENT_WMODE_OR, 10);
    if (ret)
    {
        os_printf(KERN_ERR "scale1 h264 wait kick timeout\r\n");
        goto scale1_h264_timer_error;
    }
    scale_close(scale_dev);
    return RET_OK;

scale1_h264_timer_error:
    timer_wait.active = 0;
    timer_device_stop(timer_dev);
    timer_device_close(timer_dev);
    scale_close(scale_dev);
    return RET_ERR;
}

static int32_t scale1_h264_process_fb(struct scale1_h264_s *scale1_h264, uint16_t in_w, uint16_t in_h, uint16_t out_w, uint16_t out_h)
{
    uint8_t  line_num = SCALE1_H264_LINE_NUM;
    int32_t  ret;

    ret = scale1_h264_prepare_line_buf(scale1_h264, in_w, line_num);
    if (ret)
    {
        return ret;
    }
    ret = scale1_soft_from_psram_to_enc(scale1_h264, scale1_h264->line_buf, line_num, scale1_h264->fb->data, (uint32_t)in_w, (uint32_t)in_h, (uint32_t)out_w, (uint32_t)out_h);
    return ret;
}

static void scale1_h264_work(void *d)
{
    struct scale1_h264_s *scale1_h264 = (struct scale1_h264_s *)d;
    uint32_t                    flags = 0;
    int32_t                       ret = RET_OK;
    int32_t                delay_time = -1;
    uint16_t                     in_w = 0;
    uint16_t                     in_h = 0;
    uint16_t                    out_w = 0;
    uint16_t                    out_h = 0;
    uint16_t              scale_out_w = 0;
    uint16_t              scale_out_h = 0;

    while (1)
    {
        flags = 0;
        ret   = os_event_wait(&scale1_h264->evt, MSI_SCALE1_H264_THREAD_KICK | MSI_SCALE1_H264_THREAD_STOP, &flags, OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, delay_time);
        if (flags & MSI_SCALE1_H264_THREAD_STOP)
        {
            break;
        }

        if (!scale1_h264->fb)
        {
            scale1_h264->fb = msi_get_fb(scale1_h264->msi, 0);
        }

        if (scale1_h264->fb)
        {
            if (!msi_output_fb(scale1_h264->msi, NULL))
            {
                msi_delete_fb(NULL, scale1_h264->fb);
                scale1_h264->fb = NULL;
                delay_time      = scale1_h264->line_buf ? 1000 : -1;
                continue;
            }

            if (!scale1_h264->register_h264_msi)
            {
                struct yuv_arg_s *yuv_msg = (struct yuv_arg_s *) scale1_h264->fb->priv;
                in_w = yuv_msg->out_w;
                in_h = yuv_msg->out_h;
                out_w = scale1_h264->out_w ? scale1_h264->out_w : in_w;
                out_h = scale1_h264->out_h ? scale1_h264->out_h : in_h;

                struct msi *h264_msi = h264_msi_init_with_mode(SCALER_DATA, out_w, out_h, ~0, 0, 0);
                if (h264_msi)
                {
                    ret = msi_add_output(h264_msi, NULL, scale1_h264->msi->name);
                    if (ret == RET_OK)
                    {
                        scale1_h264->register_h264_msi = h264_msi;
                    }
                    else
                    {
                        msi_destroy(h264_msi);
                    }
                }
            }

            if (!scale1_h264->register_h264_msi)
            {
                os_printf(KERN_ERR "scale1 h264 enc init err\r\n");
                delay_time = 1;
                continue;
            }

            scale_out_w = (out_w + 0xf) & ~0xf;
            scale_out_h = (out_h + 0xf) & ~0xf;
            ret = scale1_h264_process_fb(scale1_h264, in_w, in_h, scale_out_w, scale_out_h);

            if (ret == RET_OK || ret == RET_ERR)
            {
                msi_delete_fb(NULL, scale1_h264->fb);
                scale1_h264->fb = NULL;
                delay_time      = 0;
            }
            else
            {
                delay_time = 1;
            }
        }
        else
        {
            if (ret && delay_time > 0 && scale1_h264->line_buf)
            {
                STREAM_LIBC_FREE(scale1_h264->line_buf);
                scale1_h264->line_buf      = NULL;
                scale1_h264->line_buf_size = 0;
            }
            delay_time = scale1_h264->line_buf ? 1000 : -1;
        }
    }

    if (scale1_h264->fb)
    {
        msi_delete_fb(NULL, scale1_h264->fb);
        scale1_h264->fb = NULL;
    }

    scale_close(scale1_h264->scale_dev);
    os_event_set(&scale1_h264->evt, MSI_SCALE1_H264_THREAD_DEAD, NULL);
    msi_put(scale1_h264->msi);
}

static int32_t scale1_h264_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    int32_t                  ret          = RET_OK;
    struct scale1_h264_s *scale1_h264 = (struct scale1_h264_s *)msi->priv;

    switch (cmd_id)
    {
        case MSI_CMD_POST_DESTROY:
        {
            os_event_wait(&scale1_h264->evt, MSI_SCALE1_H264_THREAD_DEAD, NULL, OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, -1);
            if (scale1_h264->line_buf)
            {
                STREAM_LIBC_FREE(scale1_h264->line_buf);
                scale1_h264->line_buf = NULL;
            }
            if (scale1_h264->register_h264_msi)
            {
                msi_del_output(scale1_h264->register_h264_msi, NULL, scale1_h264->msi->name);
                msi_destroy(scale1_h264->register_h264_msi);
                scale1_h264->register_h264_msi = NULL;
            }
            os_event_del(&scale1_h264->evt);
            STREAM_LIBC_FREE(scale1_h264);
        }
        break;

        case MSI_CMD_PRE_DESTROY:
        {
            os_event_set(&scale1_h264->evt, MSI_SCALE1_H264_THREAD_STOP, NULL);
        }
        break;

        case MSI_CMD_TRANS_FB:
        {
            struct framebuff *fb = (struct framebuff *)param1;

            if (!fb || !fb->data)
            {
                ret = RET_ERR;
                break;
            }

            if (fb->mtype == F_H264 && fb->stype == FSTYPE_H264_SCALER_DATA)
            {
                fb_get(fb);
                msi_output_fb(msi, fb);
                ret = RET_OK + 1;
                break;
            }

            if (fb->mtype != F_YUV)
            {
                ret = RET_ERR;
            }

            if (scale1_h264->filter_type && fb->stype != scale1_h264->filter_type)
            {
                ret = RET_ERR;
            }
        }
        break;

        case MSI_CMD_TRANS_FB_END:
        {
            os_event_set(&scale1_h264->evt, MSI_SCALE1_H264_THREAD_KICK, NULL);
        }
        break;

        case MSI_CMD_SCALE1:
        {
            uint32_t cmd_self = param1;
            uint32_t arg      = param2;

            switch (cmd_self)
            {
                case MSI_SCALE1_RESET_DPI:
                {
                    scale1_h264->out_w = (arg >> 16) & 0xffff;
                    scale1_h264->out_h = arg & 0xffff;
                }
                break;

                default:
                    break;
            }
        }
        break;

        default:
            break;
    }

    return ret;
}

struct msi *scale1_h264_msi_init(const char *name, uint16_t filter_type, uint16_t out_w, uint16_t out_h)
{
    uint8_t                   isnew;
    struct msi               *msi;
    struct scale1_h264_s *scale1_h264;

    msi = msi_new(name, SCALE1_H264_RECV_MAX, &isnew);
    if (!msi)
    {
        return NULL;
    }

    scale1_h264 = (struct scale1_h264_s *)msi->priv;
    if (isnew)
    {
        scale1_h264 = (struct scale1_h264_s *)STREAM_LIBC_ZALLOC(sizeof(struct scale1_h264_s));
        if (!scale1_h264)
        {
            msi_destroy(msi);
            return NULL;
        }

        scale1_h264->msi       = msi;
        scale1_h264->scale_dev = (struct scale_device *)dev_get(HG_SCALE1_DEVID);
        msi->priv              = scale1_h264;
        msi->action            = scale1_h264_action;
        msi->enable            = 1;
        os_event_init(&scale1_h264->evt);

        msi_get(msi);
        os_task_create("scale1_h264", scale1_h264_work, scale1_h264, OS_TASK_PRIORITY_ABOVE_NORMAL, 0, NULL, 1024);
    }

    scale1_h264->filter_type = filter_type;
    scale1_h264->out_w       = out_w;
    scale1_h264->out_h       = out_h;
    msi->enable              = 1;

    return msi;
}
