#include "basic_include.h"
#include "hal/lcdc.h"
#include "dev/lcdc/hglcdc.h"
#include "lib/multimedia/msi.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "lib/lcd/lcd.h"
#include "stream_frame.h"
#include "stream_define.h"
#include "user_work/user_work.h"
#include "lcdc_rotate.h"

#define LCDC_ROTATE_DEBUG_PRINTF(fmt, ...)  // os_printf(KERN_DEBUG""fmt, ##__VA_ARGS__)
#define LCDC_ROTATE_ERR_PRINTF(fmt, ...)    os_printf(KERN_ERR""fmt, ##__VA_ARGS__)

#define STREAM_MALLOC            av_malloc
#define STREAM_FREE              av_free
#define STREAM_ZALLOC            av_zalloc
#define CAPTURE_MALLOC           av_psram_malloc
#define CAPTURE_ZALLOC           av_psram_zalloc
#define CAPTURE_FREE             av_psram_free

#define MAX_ROTATE_NUM           1
#define LCDC_YUV_OUT_SIZE(w, h)  ((w) * (h) * 3 / 2)

enum lcdc_rotate_buf_state {
    LCDC_ROTATE_BUF_FREE,
    LCDC_ROTATE_BUF_CAPTURING,
    LCDC_ROTATE_BUF_READY,
    LCDC_ROTATE_BUF_IN_MSI,
};

struct lcdc_rotate_buf {
    uint8_t             *data;
    struct yuv_arg_s     yuv_arg;
    volatile uint8_t     state;
};

struct lcdc_rotate {
    struct os_work               work;
    struct msi                  *msi;
    struct lcdc_device          *lcd_dev;
    struct framebuff            *volatile current_fb;
    struct lcdc_rotate_buf      *buf;
    struct lcdc_rotate_buf      *volatile capture_buf;
    void                        *line_buf;
    uint8_t                      buf_num;
    uint8_t                      line_buf_num;
    uint16_t                     line_buf_width;
    volatile uint8_t             initialized;
    volatile uint8_t             stopping;
    volatile uint8_t             hardware_busy;
    uint8_t                      hardware_configured;
    uint32_t                     start_jiffies;
    struct lcdc_rotate_config    config;
};

static void lcdc_rotate_release_fb(struct lcdc_rotate *rotate);
static int32_t lcdc_rotate_work(struct os_work *work);
static void lcdc_rotate_kick(struct lcdc_rotate *rotate);

static struct lcdc_rotate_buf *lcdc_rotate_find_free_buf(struct lcdc_rotate *rotate)
{
    if (!rotate || !rotate->buf)
    {
        return NULL;
    }

    for (uint8_t i = 0; i < rotate->buf_num; i++)
    {
        if (rotate->buf[i].state == LCDC_ROTATE_BUF_FREE)
        {
            return &rotate->buf[i];
        }
    }
    return NULL;
}

static void lcdc_rotate_free_buf(struct lcdc_rotate_buf *buf, uint8_t buf_num)
{
    if (!buf)
    {
        return;
    }

    for (uint8_t i = 0; i < buf_num; i++)
    {
        if (buf[i].data)
        {
            CAPTURE_FREE(buf[i].data);
            buf[i].data = NULL;
        }
        buf[i].state = LCDC_ROTATE_BUF_FREE;
        os_memset(&buf[i].yuv_arg, 0, sizeof(buf[i].yuv_arg));
    }
}

static int32_t lcdc_rotate_malloc_buf(const struct lcdc_rotate_config *config, uint8_t buf_num, struct lcdc_rotate_buf *buf)
{
    uint32_t capture_len = LCDC_YUV_OUT_SIZE(config->capture_w, config->capture_h);

    for (uint8_t i = 0; i < buf_num; i++)
    {
        buf[i].data = (uint8_t *)CAPTURE_ZALLOC(capture_len);
        if (!buf[i].data)
        {
            lcdc_rotate_free_buf(buf, i);
            return RET_ERR;
        }

        buf[i].state          = LCDC_ROTATE_BUF_FREE;
        buf[i].yuv_arg.type   = YUV_ARG_NORMAL;
        buf[i].yuv_arg.y_size = (uint32_t)config->capture_w * config->capture_h;
        buf[i].yuv_arg.y_off  = (uint32_t)buf[i].data;
        buf[i].yuv_arg.u_off  = (uint32_t)buf[i].data + buf[i].yuv_arg.y_size;
        buf[i].yuv_arg.v_off  = (uint32_t)buf[i].data + buf[i].yuv_arg.y_size + buf[i].yuv_arg.y_size / 4;
        buf[i].yuv_arg.uv_off = 0;
        buf[i].yuv_arg.out_w  = config->capture_w;
        buf[i].yuv_arg.out_h  = config->capture_h;
    }
    return RET_OK;
}

static void lcdc_rotate_stop_hardware(struct lcdc_rotate *rotate)
{
    if (!rotate || !rotate->lcd_dev || !rotate->hardware_configured)
    {
        return;
    }

    lcdc_set_video_en(rotate->lcd_dev, 0);
    lcdc_screen_start(rotate->lcd_dev, 0);
    lcdc_release_irq(rotate->lcd_dev, TIMEOUT_IRQ);
    lcdc_release_irq(rotate->lcd_dev, SCREEN_DONE_IRQ);
    lcdc_close(rotate->lcd_dev);
    lcdc_deinit(rotate->lcd_dev);
    rotate->hardware_configured = 0;
}

static int32_t lcdc_rotate_msi_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    int32_t             ret    = RET_OK;
    struct lcdc_rotate *rotate = (struct lcdc_rotate *)msi->priv;
    switch (cmd_id)
    {
        case MSI_CMD_PRE_DESTROY:
        {
            if (rotate)
            {
                rotate->stopping = 1;
                rotate->initialized = 0;
                os_work_cancle2(&rotate->work, 1);
                lcdc_rotate_stop_hardware(rotate);

                if (rotate->capture_buf)
                {
                    rotate->capture_buf->state = LCDC_ROTATE_BUF_FREE;
                    rotate->capture_buf = NULL;
                }
                lcdc_rotate_release_fb(rotate);
            }
        }
        break;
        case MSI_CMD_POST_DESTROY:
        {
            if (rotate)
            {
                lcdc_rotate_free_buf(rotate->buf, rotate->buf_num);
                if (rotate->buf)
                {
                    STREAM_FREE(rotate->buf);
                    rotate->buf = NULL;
                }
                if (rotate->line_buf)
                {
                    STREAM_FREE(rotate->line_buf);
                    rotate->line_buf = NULL;
                }
                STREAM_FREE(rotate);
            }
        }
        break;
        case MSI_CMD_TRANS_FB:
        {
            struct framebuff *fb = (struct framebuff *)param1;
            if (!rotate || !rotate->initialized || rotate->stopping || !fb || !fb->data || fb->mtype != F_YUV ||
                fb->stype != rotate->config.input_stype)
            {
                ret = RET_ERR;
            }
        }
        break;
        case MSI_CMD_FREE_FB:
        {
            if (rotate)
            {
                struct framebuff *fb = (struct framebuff *)param1;
                if (fb)
                {
                    for (uint8_t i = 0; i < rotate->buf_num; i++)
                    {
                        if (fb->priv == &rotate->buf[i].yuv_arg)
                        {
                            rotate->buf[i].state = LCDC_ROTATE_BUF_FREE;
                            lcdc_rotate_kick(rotate);
                            break;
                        }
                    }
                }
            }
        }
        break;
    }
    return ret;
}

static void lcdc_rotate_release_fb(struct lcdc_rotate *rotate)
{
    struct framebuff *fb;

    if (!rotate)
    {
        return;
    }
    fb = rotate->current_fb;
    rotate->current_fb = NULL;
    rotate->hardware_busy = 0;

    if (fb)
    {
        LCDC_ROTATE_DEBUG_PRINTF("lcdc rotate time: %d\r\n", os_jiffies() - rotate->start_jiffies);
        msi_delete_fb(NULL, fb);
    }
}

static void lcdc_rotate_kick(struct lcdc_rotate *rotate)
{
    if (rotate && rotate->initialized && !rotate->stopping)
    {
        os_run_work(&rotate->work);
    }
}

static int32_t lcdc_rotate_timeout(uint32_t irq_flag, uint32_t irq_data, uint32_t param1)
{
    struct lcdc_rotate *rotate = (struct lcdc_rotate *)irq_data;

    if (!rotate || !rotate->lcd_dev)
    {
        return RET_OK;
    }

    LCDC_ROTATE_ERR_PRINTF("lcdc rotate timeout\r\n");
    lcdc_set_video_en(rotate->lcd_dev, 0);
    lcdc_screen_start(rotate->lcd_dev, 0);
    if (rotate->capture_buf && rotate->capture_buf->state == LCDC_ROTATE_BUF_CAPTURING)
    {
        rotate->capture_buf->state = LCDC_ROTATE_BUF_FREE;
        rotate->capture_buf = NULL;
    }
    lcdc_rotate_release_fb(rotate);
    lcdc_rotate_kick(rotate);
    return RET_OK;
}

static int32_t lcdc_rotate_capture_done(uint32_t irq_flag, uint32_t irq_data, uint32_t param1)
{
    struct lcdc_rotate *rotate = (struct lcdc_rotate *)irq_data;

    if (rotate && rotate->capture_buf && rotate->capture_buf->state == LCDC_ROTATE_BUF_CAPTURING)
    {
        rotate->capture_buf->state = LCDC_ROTATE_BUF_READY;
        lcdc_rotate_kick(rotate);
    }
    return RET_OK;
}

static int32_t lcdc_rotate_capture_work(struct lcdc_rotate *rotate)
{
    struct lcdc_rotate_buf *buf = (struct lcdc_rotate_buf *)rotate->capture_buf;
    if (!buf || buf->state != LCDC_ROTATE_BUF_READY)
    {
        return 0;
    }
    buf->state = LCDC_ROTATE_BUF_IN_MSI;
    rotate->capture_buf = NULL;

    lcdc_set_video_en(rotate->lcd_dev, 0);
    lcdc_screen_start(rotate->lcd_dev, 0);
    lcdc_rotate_release_fb(rotate);

    uint8_t output_stype = rotate->config.output_stype ? rotate->config.output_stype : FSTYPE_YUV_OTHER;
    uint32_t size = LCDC_YUV_OUT_SIZE(rotate->config.capture_w, rotate->config.capture_h);
    struct framebuff *fb = fb_alloc(buf->data, size, (F_YUV << 8) | output_stype, rotate->msi);
    if (!fb)
    {
        buf->state = LCDC_ROTATE_BUF_FREE;
        lcdc_rotate_kick(rotate);
        return 0;
    }
    fb->priv = &buf->yuv_arg;
    fb->time = os_jiffies();
    fb->srcID = FRAMEBUFF_SOURCE_LCDC;
    if (msi_output_fb(rotate->msi, fb) <= 0 && buf->state == LCDC_ROTATE_BUF_IN_MSI)
    {
        buf->state = LCDC_ROTATE_BUF_FREE;
    }
    lcdc_rotate_kick(rotate);
    return 0;
}

static int32_t lcdc_rotate_work(struct os_work *work)
{
    struct lcdc_rotate *rotate = (struct lcdc_rotate *)work;

    if (!rotate->initialized || rotate->stopping)
    {
        return 0;
    }
    if (rotate->capture_buf && rotate->capture_buf->state == LCDC_ROTATE_BUF_READY)
    {
        return lcdc_rotate_capture_work(rotate);
    }
    if (rotate->hardware_busy)
    {
        os_run_work_delay(work, 2);
        return 0;
    }
    if (!rotate->current_fb)
    {
        rotate->current_fb = msi_get_fb(rotate->msi, 0);
    }
    if (!rotate->current_fb)
    {
        os_run_work_delay(work, 2);
        return 0;
    }

    rotate->start_jiffies = os_jiffies();
    if (rotate->capture_buf || !lcdc_rotate_find_free_buf(rotate))
    {
        lcdc_rotate_release_fb(rotate);
        os_run_work_delay(work, 2);
        return 0;
    }

    struct framebuff *fb = rotate->current_fb;
    struct yuv_arg_s *yuv = (struct yuv_arg_s *)fb->priv;
    if (!yuv || !yuv->out_w || !yuv->out_h)
    {
        lcdc_rotate_release_fb(rotate);
        os_run_work_delay(work, 1);
        return 0;
    }
    uint32_t y_size = yuv->y_size ? yuv->y_size : (uint32_t)yuv->out_w * yuv->out_h;
    uint32_t y_off = yuv->y_off ? yuv->y_off : (uint32_t)fb->data;
    uint32_t u_off = yuv->u_off ? yuv->u_off : (uint32_t)fb->data + yuv->uv_off + y_size;
    uint32_t v_off = yuv->v_off ? yuv->v_off : (uint32_t)fb->data + y_size + y_size / 4 + yuv->uv_off;
    lcdc_set_p0_rotate_y_src_addr(rotate->lcd_dev, y_off);
    lcdc_set_p0_rotate_u_src_addr(rotate->lcd_dev, u_off);
    lcdc_set_p0_rotate_v_src_addr(rotate->lcd_dev, v_off);
    lcdc_set_rotate_p0p1_size(rotate->lcd_dev, yuv->out_w, yuv->out_h, 0, 0);
    lcdc_set_rotate_p0p1_start_location(rotate->lcd_dev, 0, 0, 0, 0);
    lcdc_set_rotate_mirror(rotate->lcd_dev, 0, rotate->config.rotate_mode);
    lcdc_set_p0p1_enable(rotate->lcd_dev, 1, 0);
    lcdc_set_rotate_linebuf_y_addr(rotate->lcd_dev, (uint32_t)rotate->line_buf);
    lcdc_set_rotate_linebuf_u_addr(rotate->lcd_dev, (uint32_t)rotate->line_buf + rotate->line_buf_width * rotate->line_buf_num);
    lcdc_set_rotate_linebuf_v_addr(rotate->lcd_dev, (uint32_t)rotate->line_buf + rotate->line_buf_width * rotate->line_buf_num + (rotate->line_buf_width / 2) * rotate->line_buf_num);
    lcdc_set_video_en(rotate->lcd_dev, 1);

    struct lcdc_rotate_buf *capture = lcdc_rotate_find_free_buf(rotate);
    if (!capture)
    {
        lcdc_set_video_en(rotate->lcd_dev, 0);
        lcdc_rotate_release_fb(rotate);
        os_run_work_delay(work, 1);
        return 0;
    }
    capture->state = LCDC_ROTATE_BUF_CAPTURING;
    rotate->capture_buf = capture;
    lcdc_screen_yuv_addr(rotate->lcd_dev, (uint32_t)capture->data, (uint32_t)capture->data + capture->yuv_arg.y_size, (uint32_t)capture->data + capture->yuv_arg.y_size + capture->yuv_arg.y_size / 4);
    if (lcdc_screen_start(rotate->lcd_dev, 1) != RET_OK)
    {
        rotate->capture_buf->state = LCDC_ROTATE_BUF_FREE;
        rotate->capture_buf = NULL;
        lcdc_set_video_en(rotate->lcd_dev, 0);
        lcdc_rotate_release_fb(rotate);
        os_run_work_delay(work, 1);
        return 0;
    }

    rotate->hardware_busy = 1;
    lcdc_set_start_run(rotate->lcd_dev);
    return 0;
}

static int32_t lcdc_rotate_config_hardware(struct lcdc_rotate *rotate)
{
    rotate->lcd_dev = (struct lcdc_device *)dev_get(HG_LCDC_DEVID);
    if (!rotate->lcd_dev)
    {
        LCDC_ROTATE_ERR_PRINTF("lcdc rotate device not found\r\n");
        return RET_ERR;
    }
    struct dsi_device *dsi_dev = (struct dsi_device *)dev_get(HG_DSI_DEVID);
    if (!dsi_dev)
    {
        LCDC_ROTATE_ERR_PRINTF("lcdc rotate dsi device not found\r\n");
        return RET_ERR;
    }
    if (lcdc_init(rotate->lcd_dev) != RET_OK)
    {
        dev_put((struct dev_obj *)dsi_dev);
        return RET_ERR;
    }

    lcdc_set_color_mode(rotate->lcd_dev, LCD_MODE_888);
    lcdc_set_bus_width(rotate->lcd_dev, LCD_BUS_WIDTH_24);
    lcdc_set_interface(rotate->lcd_dev, LCD_BUS_MIPI);
    lcdc_set_colrarray(rotate->lcd_dev, 0);
    lcdc_set_lcd_vaild_size(rotate->lcd_dev,
                            rotate->config.output_w + LCDC_ROTATE_HSA +
                            LCDC_ROTATE_HBP + LCDC_ROTATE_HFP,
                            rotate->config.output_h + LCDC_ROTATE_VSA +
                            LCDC_ROTATE_VBP + LCDC_ROTATE_VFP, 1);
    lcdc_set_lcd_visible_size(rotate->lcd_dev, rotate->config.output_w, rotate->config.output_h, 1);
    lcdc_signal_config(rotate->lcd_dev, 1, 1, 1, 0, 0, 0, 1);
    lcdc_set_invalid_line(rotate->lcd_dev, LCDC_ROTATE_VSA);
    lcdc_set_valid_dot(rotate->lcd_dev, LCDC_ROTATE_HSA + LCDC_ROTATE_HBP, LCDC_ROTATE_VSA + LCDC_ROTATE_VBP);
    lcdc_set_hlw_vlw(rotate->lcd_dev, LCDC_ROTATE_HSA, 0);
    lcdc_set_baudrate(rotate->lcd_dev, LCDC_ROTATE_DCLK);
    lcdc_set_bigendian(rotate->lcd_dev, 1);
    lcdc_clock_alway_on(rotate->lcd_dev, 1);
    mipi_dsi_init_no_panel(rotate->config.output_w, rotate->config.output_h,
                           LCDC_ROTATE_DCLK, LCDC_ROTATE_VSA, LCDC_ROTATE_VBP,
                           LCDC_ROTATE_VFP, LCDC_ROTATE_HSA, LCDC_ROTATE_HBP,
                           LCDC_ROTATE_HFP, LCDC_ROTATE_LANE_NUM, LCD_MODE_888);
    lcdc_dsi_select_edpi_or_dpi(rotate->lcd_dev, 0);
    dev_put((struct dev_obj *)dsi_dev);

    uint8_t  line_num   = LCDC_ROTATE_LINE_NUM;
    uint32_t line_width = ((uint32_t)rotate->config.output_w + 0xf) / 16 * 16;
    uint32_t line_size  = line_num * line_width * 2;
    if (!rotate->line_buf)
    {
        rotate->line_buf = STREAM_MALLOC(line_size);
        if (!rotate->line_buf)
        {
            LCDC_ROTATE_ERR_PRINTF("%s %d, line buf malloc fail!\r\n", __FUNCTION__, __LINE__);
            lcdc_set_video_en(rotate->lcd_dev, 0);
            lcdc_close(rotate->lcd_dev);
            lcdc_deinit(rotate->lcd_dev);
            return RET_ERR;
        }
    }
    rotate->line_buf_width = line_width;
    rotate->line_buf_num = line_num;
    lcdc_set_video_size(rotate->lcd_dev, rotate->config.output_w, rotate->config.output_h);
    lcdc_set_rotate_p0_up(rotate->lcd_dev, 0);
    lcdc_set_rotate_p0p1_start_location(rotate->lcd_dev, 0, 0, 0, 0);
    lcdc_set_rotate_linebuf_num(rotate->lcd_dev, line_num);
    lcdc_set_video_data_from(rotate->lcd_dev, VIDEO_FROM_MEMORY_ROTATE);
    lcdc_set_video_start_location(rotate->lcd_dev, 0, 0);
    lcdc_set_video_en(rotate->lcd_dev, 0);
    lcdc_set_p0p1_enable(rotate->lcd_dev, 0, 0);
    lcdc_set_osd_en(rotate->lcd_dev, 0);
    lcdc_set_timeout_info(rotate->lcd_dev, 1, 3);
    lcdc_screen_start(rotate->lcd_dev, 0);
    lcdc_request_irq(rotate->lcd_dev, TIMEOUT_IRQ, lcdc_rotate_timeout, (uint32_t)rotate);
    lcdc_request_irq(rotate->lcd_dev, SCREEN_DONE_IRQ, lcdc_rotate_capture_done, (uint32_t)rotate);
    lcdc_open(rotate->lcd_dev);
    rotate->hardware_configured = 1;
    return RET_OK;
}

struct msi *lcdc_rotate_msi_init(const struct lcdc_rotate_config *config)
{
    uint8_t isnew = 0;
    struct msi *msi = msi_new(config->name, MAX_ROTATE_NUM, &isnew);
    if (!msi)
    {
        return NULL;
    }

    if(isnew)
    {
        struct lcdc_rotate *rotate = (struct lcdc_rotate *)STREAM_ZALLOC(sizeof(*rotate));
        if (!rotate)
        {
            LCDC_ROTATE_ERR_PRINTF("%s %d, malloc failed!\r\n", __FUNCTION__, __LINE__);
            msi_destroy(msi);
            return NULL;
        }
        rotate->config = *config;
        rotate->buf_num = LCDC_ROTATE_CAPTURE_BUF_NUM;
        rotate->buf = (struct lcdc_rotate_buf *)STREAM_ZALLOC(sizeof(*rotate->buf) * rotate->buf_num);
        if (!rotate->buf)
        {
            LCDC_ROTATE_ERR_PRINTF("%s %d, capture buf info malloc failed!\r\n", __FUNCTION__, __LINE__);
            msi->priv = NULL;
            msi_destroy(msi);
            STREAM_FREE(rotate);
            return NULL;
        }
        rotate->msi = msi;
        msi->priv = rotate;
        msi->action = lcdc_rotate_msi_action;
        msi->enable = 1;
        OS_WORK_INIT(&rotate->work, lcdc_rotate_work, 0);

        if (lcdc_rotate_config_hardware(rotate) != RET_OK)
        {
            LCDC_ROTATE_ERR_PRINTF("%s %d, hardware config err!\r\n", __FUNCTION__, __LINE__);
            STREAM_FREE(rotate->buf);
            rotate->buf = NULL;
            msi->priv = NULL;
            msi_destroy(msi);
            STREAM_FREE(rotate);
            return NULL;
        }

        if (lcdc_rotate_malloc_buf(config, rotate->buf_num, rotate->buf) != RET_OK)
        {
            STREAM_FREE(rotate->buf);
            rotate->buf = NULL;
            if (rotate->line_buf)
            {
                STREAM_FREE(rotate->line_buf);
                rotate->line_buf = NULL;
            }
            lcdc_rotate_stop_hardware(rotate);
            msi->priv = NULL;
            msi_destroy(msi);
            STREAM_FREE(rotate);
            return NULL;
        }

        rotate->initialized = 1;
        os_run_work_delay(&rotate->work, 1);
    }
    else
    {
        msi_destroy(msi);
        return NULL;
    }
    
    return msi;
}
