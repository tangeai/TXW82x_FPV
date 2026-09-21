#include "basic_include.h"
#include "lib/multimedia/msi.h"
#include "stream_define.h"
#include "lib/video/dvp/jpeg/jpg.h"
#include "dev/jpg/hgjpg.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "lib/video/vpp/vpp_dev.h"
#include "jpg_concat_msi.h"
#include "user_work/user_work.h"
#include "hal/scale.h"
#include "hal/isp.h"
#define MAX_JPG_NODE_NUM 100  // 最大节点数量
#define MAX_JPG_BUF_TTL  5000 // buf超时释放
#define PRE_ALLOC_COUNT  2    // 最小预分配数量
#if JPG_EN
#define HARDWARE_JPG_NUM 2

// jpg中断记录的msg,通过jpg_node预留空间来及记录
struct jpg_isr_msg
{
    uint8_t  stype;
    uint8_t  src_from;
    uint8_t  srcID;
    uint8_t  datatag;
    uint16_t w, h;
    uint32_t time;
    uint32_t len;
};

enum
{
    MSI_JPG_DONE_ERR             = BIT(0), // done的时候报错
    MSI_JPG_BUF_FULL_ERR         = BIT(1), // buf full的时候报错
    MSI_JPG_BUF_ERR              = BIT(2), // 硬件直接报错
    MSI_SCALE1_DATA0_DATA1_CLOSE = BIT(3),
    MSI_JPG_END_FLAG             = BIT(4),
};

// data申请空间函数
#define STREAM_MALLOC av_psram_malloc
#define STREAM_FREE   av_psram_free
#define STREAM_ZALLOC av_psram_zalloc

// 结构体申请空间函数
#define STREAM_LIBC_MALLOC av_psram_malloc
#define STREAM_LIBC_FREE   av_psram_free
#define STREAM_LIBC_ZALLOC av_psram_zalloc

struct msi *g_jpg_msi[HARDWARE_JPG_NUM] = {NULL, NULL};

// 采用默认jpg方式,如果遇到频繁切换mjpg模式,会变慢
#ifndef FAST_JPG

// 如果是从vpp buf0或者vpp buf1,则更新jpg的w,h
static int32_t update_jpg_w_h(struct jpg_V3_msi_s *jpg_msg)
{
    if (jpg_msg->src_from == VPP_DATA0)
    {
        get_vpp_w_h(&jpg_msg->w, &jpg_msg->h);
    }
    else if (jpg_msg->src_from == VPP_DATA1)
    {
        get_vpp1_w_h(&jpg_msg->w, &jpg_msg->h);
    }
    return 0;
}
static int32_t vpp_start_JPEG(uint32_t irq_data)
{
    struct jpg_V3_msi_s *jpg_msg = (struct jpg_V3_msi_s *) irq_data;
    struct jpg_device   *jpg     = jpg_msg->jpg;
    update_jpg_w_h(jpg_msg);
    // 拼接或者已经第二个镜头已经done或者是单镜头,则启动
    if (video_msg.video_type_cur == ISP_VIDEO_1 || video_msg.camera_mode == CAM_SINGLE_MASTER_MODE)
    {
        jpg_set_size(jpg_msg->jpg, jpg_msg->h, jpg_msg->w);
        jpg_set_ready(jpg_msg->jpg);
        jpg_open(jpg);
        return 1;
    }

    else
    {
        return 0;
    }
}

int jpg_quality_pidCtrl(struct jpg_V3_msi_s *jpg_msg, int diff, int p, int i, int d)
{
    int32_t res = p * diff + i * jpg_msg->diff_sum + d * (diff - jpg_msg->diff_prev);
    jpg_msg->diff_sum += diff;
    jpg_msg->diff_prev = diff;
    res                = res >> 16;
    res                = LIMITING(res, 120, -120);
    jpg_msg->diff_sum  = LIMITING(jpg_msg->diff_sum, 2000, -2000);
    //_os_printf("diff_sum:%d\n",diff_sum);
    return res;
}

static uint8_t jpg_msi_quality_tidy(struct jpg_V3_msi_s *jpg_msg, uint32_t cur_len, uint8_t *dqt_index_diff)
{
    uint8_t updata_dqt    = 0;
    int32_t jpg_len_diff  = cur_len - jpg_msg->target_len;
    int32_t res           = jpg_quality_pidCtrl(jpg_msg, jpg_len_diff, QUALITY_CTRL_P, QUALITY_CTRL_I, QUALITY_CTRL_D);
    uint8_t qt_diff       = os_abs(res) % 0x10;
    uint8_t qt_index_diff = os_abs(res) / 0x10;
    *dqt_index_diff       = qt_index_diff;
    if (res > 0)
    {
        if (qt_diff + (jpg_msg->qt) > 0xf)
        {
            if ((jpg_msg->dqtable_index + qt_index_diff + 1 + (qt_diff - (0xf - jpg_msg->qt)) / 8) > DQT_MAX_INDEX)
            {
                *dqt_index_diff = DQT_MAX_INDEX - jpg_msg->dqtable_index;
                jpg_msg->qt     = 0xf;
            }
            else
            {
                jpg_msg->qt     = 0x8 + (qt_diff - (0xf - jpg_msg->qt)) % 8;
                *dqt_index_diff = qt_index_diff + 1 + (qt_diff - (0xf - jpg_msg->qt)) / 8;
            }
        }
        else
        {
            jpg_msg->qt += qt_diff;
        }
        if ((*dqt_index_diff) >= 1 && jpg_msg->dqtable_index < DQT_MAX_INDEX)
        {
            *dqt_index_diff = ((*dqt_index_diff) > (DQT_MAX_INDEX - jpg_msg->dqtable_index)) ? (DQT_MAX_INDEX - jpg_msg->dqtable_index) : (*dqt_index_diff);
            updata_dqt      = 2;
        }
    }
    else if (res < 0)
    {
        if (jpg_msg->qt - qt_diff < 0)
        {
            if ((jpg_msg->dqtable_index - (qt_index_diff + 1 + (qt_diff - jpg_msg->qt) / 8)) < 0)
            {
                *dqt_index_diff = jpg_msg->dqtable_index;
                jpg_msg->qt     = 0;
            }
            else
            {
                jpg_msg->qt     = 0x8 - (qt_diff - jpg_msg->qt) % 8; //(qt_diff-(*qt)) maybe > 0x8
                *dqt_index_diff = qt_index_diff + 1 + (qt_diff - jpg_msg->qt) / 8;
            }
        }
        else
        {
            jpg_msg->qt -= qt_diff;
        }
        if ((*dqt_index_diff) >= 1 && jpg_msg->dqtable_index > 0)
        {
            *dqt_index_diff = ((*dqt_index_diff) > (jpg_msg->dqtable_index)) ? jpg_msg->dqtable_index : (*dqt_index_diff);
            updata_dqt      = 1;
        }
    }
    if (updata_dqt == 0)
    {
        *dqt_index_diff = 0;
    }

    return updata_dqt;
}

static void jpg_msi_DQT_updata(struct jpg_V3_msi_s *jpg_msg, uint8_t upOdown, uint8_t diff)
{
    uint32_t *ptable   = NULL;
    int8_t    pdqt_tab = jpg_msg->dqtable_index;
    if (upOdown == 1)
    {
        pdqt_tab -= diff;
        if (pdqt_tab < 0)
        {
            pdqt_tab = 0;
        }
    }
    else
    {
        pdqt_tab += diff;
        if (pdqt_tab > DQT_MAX_INDEX)
        {
            pdqt_tab = DQT_MAX_INDEX;
        }
    }
    jpg_msg->dqtable_index = pdqt_tab;

    ptable = (uint32 *) quality_tab[pdqt_tab];
    jpg_updata_dqt(jpg_msg->jpg, ptable);
}

static int32 jpg_msi_done_isr(uint32 irq_flag, uint32 irq_data, uint32 param1, uint32 param2)
{
    uint8                dqt_index_diff = 0;
    int8_t               err            = RET_OK;
    struct msi          *msi            = (struct msi *) irq_data;
    struct jpg_V3_msi_s *jpg_msg        = (struct jpg_V3_msi_s *) msi->priv;
    // 如果没有异常,send_fb应该通过信号量发出去
    struct framebuff    *send_fb        = jpg_msg->now_fb;
    struct framebuff    *fb;
    uint32_t             jpg_len = param1;
    uint8_t              last_qt = jpg_msg->qt;
    uint16_t             jpg_w, jpg_h;
    // gpio_set_val(MACRO_PIN(PIN_DVP_DATA5), toggle++&0x01);
    jpg_w = jpg_msg->w;
    jpg_h = jpg_msg->h;
    // gpio_set_val(MACRO_PIN(PIN_SPI2_IO1), toggle++&0x01);
    //  如果数据不对,需要处理异常情况(需要将fb->now_fb里面的链表全部放回到pool中)
    //  如果异常,这里关闭jpg,workqueue去清理资源,重新启动mjpg
    if (jpg_msg->err)
    {
        err = RET_ERR;
        goto jpg_msi_done_isr_end;
    }

    update_jpg_w_h(jpg_msg);
    jpg_set_size(jpg_msg->jpg, jpg_msg->h, jpg_msg->w);
    // 最后配置的fb,这个是没有被用的,可以重复利用
    fb = jpg_msg->last_fb;
    // 这里不应该进来,进来后,需要检查是否正常
    if (!fb)
    {
        err = RET_ERR;
        goto jpg_msi_done_isr_end;
    }
    jpg_set_addr(jpg_msg->jpg, (uint32) fb->data, fb->len);

    // 先将fb记录在last_fb
    jpg_msg->last_fb = fb;

    // 分配空间
    uint8_t *p_buf = (uint8_t *) mem_cache_alloc(jpg_msg->mem_info, jpg_msg->mem_info_size, jpg_msg->need_size, NULL);
    if (!p_buf)
    {
        err = RET_ERR;
        goto jpg_msi_done_isr_end;
    }
    fb = fbpool_get(&jpg_msg->pool, 0, NULL);
    // 空间不够,则通知workqueue检查或者重新启动mjpg
    if (!fb)
    {
        err = RET_ERR;
        goto jpg_msi_done_isr_end;
    }
    fb->data             = p_buf;
    fb->len              = jpg_msg->jpg_node_len;
    // 这里确认足够空间重新启动jpg,可以将now_fb重新赋值
    jpg_msg->now_fb      = jpg_msg->last_fb;
    jpg_msg->use_last_fb = jpg_msg->last_fb;

    // 配置第二次的寄存器
    jpg_set_addr(jpg_msg->jpg, (uint32) fb->data, fb->len);
    jpg_msg->last_fb = fb;

    // 如果应用需要关闭,等待关闭流程
    if (!jpg_msg->end_flag)
    {
        jpg_set_ready(jpg_msg->jpg);
    }

    send_fb->datatag = jpg_msg->datatag;
    if (os_msgq_put(&jpg_msg->msgq, (uint32_t) send_fb, 0))
    {
        struct framebuff *tmp_fb = send_fb;
        // 发送失败,应该将fb全部放回pool池
        while (tmp_fb)
        {
            tmp_fb = send_fb->next;
            mem_cache_free(tmp_fb->data);
            fbpool_put(&jpg_msg->pool, send_fb);
            send_fb = tmp_fb;
        }
    }
    else
    {
        uint32_t time = 0;

        // 分配空间的时候已经预留了jpg_node_s的大小,所以这里直接赋值即可,不需要再申请空间
        struct jpg_node_s *jpg_priv = (struct jpg_node_s *) (send_fb->data + jpg_msg->jpg_node_len);
        jpg_priv->output_msi        = NULL;
        // send_fb->mtype = F_JPG_NODE;
        if (jpg_msg->src_from == GEN420_DATA)
        {
            // 配置这个是从gen420来的数据,后续要考虑是否要独立配置类型(因为gen420来源数据可能是内存或者其他来源),或者说由其他回调接口去配置吧
            // 这里需要调用msi命令去配置
            send_fb->stype       = jpg_msg->gen420_type;
            send_fb->srcID       = FRAMEBUFF_SOURCE_JPG_GEN420;
            time                 = jpg_msg->set_time;
            jpg_priv->output_msi = (void *) jpg_msg->output_msi;
        }
        else if (jpg_msg->src_from == SCALER_DATA)
        {
            send_fb->stype       = jpg_msg->scale1_type ? jpg_msg->scale1_type : (FSTYPE_VIDEO_VPP_DATA0 + jpg_msg->src_from);
            send_fb->srcID       = FRAMEBUFF_SOURCE_JPG_SCALER;
            time                 = jpg_msg->set_time;
            jpg_priv->output_msi = (void *) jpg_msg->output_msi;
        }
        else
        {
            if (video_msg.camera_mode == CAM_DUAL_SPLICE_SLAVE_MODE)
            {
                send_fb->srcID = FRAMEBUFF_SOURCE_CAMERA0;
            }
            else
            {
                send_fb->srcID = FRAMEBUFF_SOURCE_CAMERA0 + video_msg.video_type_cur;
            }
            send_fb->stype = FSTYPE_VIDEO_VPP_DATA0 + jpg_msg->src_from;
        }

        if (!time)
        {
            time = os_jiffies();
        }

        jpg_priv->w       = jpg_w;
        jpg_priv->h       = jpg_h;
        jpg_priv->jpg_len = jpg_len;
        send_fb->priv     = (void *) jpg_priv;
        // 配置子类型
        send_fb->datatag  = jpg_msg->datatag;

        send_fb->mtype = F_JPG_NODE;
        send_fb->time  = time;
    }
    // os_printf("jpglen:%d %d %d\n", jpg_len, jpg_msg->qt, jpg_msg->dqtable_index);
    uint8_t update = jpg_msi_quality_tidy(jpg_msg, jpg_len, &dqt_index_diff);
    // os_printf("quality res:%d %s%d\n", jpg_msg->qt, ((update>1)?"+":"-"),dqt_index_diff);
    if (last_qt != jpg_msg->qt)
    {
        jpg_set_qt(jpg_msg->jpg, jpg_msg->qt);
    }
    if (update)
    {
        jpg_msi_DQT_updata(jpg_msg, update, dqt_index_diff);
    }

jpg_msi_done_isr_end:
    os_run_work(&jpg_msg->work);
    if (err == RET_OK)
    {
        _os_printf(KERN_DEBUG "JD");
    }
    else
    {
        jpg_msg->err |= MSI_JPG_DONE_ERR;
        jpg_close(jpg_msg->jpg);
        os_event_set(&jpg_msg->evt, MSI_JPG_DONE_ERR, NULL);
        _os_printf(KERN_DEBUG "JDE");
    }
    return 0;
}

static int32 jpg_msi_outbuff_full_isr(uint32 irq_flag, uint32 irq_data, uint32 param1, uint32 param2)
{
    int8_t               err     = RET_OK;
    struct msi          *msi     = (struct msi *) irq_data;
    struct jpg_V3_msi_s *jpg_msg = (struct jpg_V3_msi_s *) msi->priv;

    if (jpg_msg->err)
    {
        _os_printf(KERN_ERR "JO");
        goto jpg_msi_outbuff_full_isr_end;
    }
    uint8_t *p_buf = (uint8_t *) mem_cache_alloc(jpg_msg->mem_info, jpg_msg->mem_info_size, jpg_msg->need_size, NULL);
    if (!p_buf)
    {
        err = RET_ERR;
        goto jpg_msi_outbuff_full_isr_end;
    }
    struct framebuff *fb = fbpool_get(&jpg_msg->pool, 0, NULL);
    if (!fb)
    {
        err = RET_ERR;
        goto jpg_msi_outbuff_full_isr_end;
    }
    fb->data = p_buf;
    fb->len  = jpg_msg->jpg_node_len;
    jpg_set_addr(jpg_msg->jpg, (uint32) fb->data, fb->len);
    ASSERT(jpg_msg->last_fb);
    // 放到链表里面(now_fb为head)
    jpg_msg->use_last_fb->next = jpg_msg->last_fb;
    // 记录当前的配置的最后fb
    jpg_msg->use_last_fb       = jpg_msg->last_fb;
    // 最后配置jpg地址的fb(尚未添加到链表)
    jpg_msg->last_fb           = fb;
jpg_msi_outbuff_full_isr_end:
    if (err != RET_OK)
    {
        jpg_msg->err |= MSI_JPG_BUF_FULL_ERR;
        os_event_set(&jpg_msg->evt, MSI_JPG_BUF_FULL_ERR, NULL);
        if (jpg_msg->end_flag)
        {
            jpg_msg->err |= MSI_JPG_END_FLAG;
            os_event_set(&jpg_msg->evt, MSI_JPG_END_FLAG, NULL);
        }
        _os_printf(KERN_ERR "JU");
    }
    // 每次进来都要去唤醒一下work,主要是为了预分配一下空间
    else
    {
        os_run_work(&jpg_msg->work);
    }
    return 0;
}

static int32 jpg_msi_buf_err(uint32 irq_flag, uint32 irq_data, uint32 param1, uint32 param2)
{
    struct msi          *msi     = (struct msi *) irq_data;
    struct jpg_V3_msi_s *jpg_msg = (struct jpg_V3_msi_s *) msi->priv;
    // 关闭jpg
    jpg_close(jpg_msg->jpg);
    // 唤醒workqueue?然后workqueue检查是不是有异常?有异常重新启动jpg?统一由外部线程去重新启动
    os_event_set(&jpg_msg->evt, MSI_JPG_BUF_ERR, NULL);
    os_run_work(&jpg_msg->work);
    jpg_msg->err |= MSI_JPG_BUF_ERR;
    if (jpg_msg->end_flag)
    {
        jpg_msg->err |= MSI_JPG_END_FLAG;
        os_event_set(&jpg_msg->evt, MSI_JPG_END_FLAG, NULL);
    }
    _os_printf(KERN_ERR "JE");
    return 0;
}

// 处理一些临时资源
void clean_jpg_res(struct jpg_V3_msi_s *jpg_msg)
{
    struct framebuff *tmp_fb, *now_fb;
    now_fb = tmp_fb = jpg_msg->now_fb;
    while (now_fb)
    {
        tmp_fb = now_fb;
        now_fb = now_fb->next;
        mem_cache_free(tmp_fb->data);
        tmp_fb->data = NULL;
        fbpool_put(&jpg_msg->pool, tmp_fb);
    }

    if (jpg_msg->last_fb)
    {
        mem_cache_free(jpg_msg->last_fb->data);
        tmp_fb->data = NULL;
        fbpool_put(&jpg_msg->pool, jpg_msg->last_fb);
    }
}

static int32 jpg_msi_work(struct os_work *work)
{
    struct jpg_V3_msi_s *jpg_msg = (struct jpg_V3_msi_s *) work;
    struct framebuff    *fb;
    int32                err     = -1;
    uint32               jpg_err = 0;
    os_event_wait(&jpg_msg->evt, MSI_JPG_DONE_ERR | MSI_JPG_BUF_FULL_ERR | MSI_JPG_BUF_ERR, &jpg_err, OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, 0);
    // 这里去申请一下内存,然后检查内存是否有超时的,如果有就超时释放
    mem_cache_pre_alloc(jpg_msg->mem_info, jpg_msg->mem_info_size, jpg_msg->need_size, STREAM_MALLOC, PRE_ALLOC_COUNT);
    // 检查是否有超时的buf,如果有就释放
    mem_cache_gc(jpg_msg->mem_info, jpg_msg->mem_info_size, MAX_JPG_BUF_TTL, STREAM_FREE);
    fb = (struct framebuff *) os_msgq_get2(&jpg_msg->msgq, 0, &err);
    if (err)
    {
        goto jpg_msi_work_end;
    }
    struct framebuff *tmp_fb = fb;
    while (tmp_fb)
    {
        tmp_fb->msi = jpg_msg->msi;
        // 这个模块是特殊处理
        msi_get(tmp_fb->msi);
        tmp_fb = tmp_fb->next;
    }
    // 为了不添加新结构,使用time作为w和h保存,priv用于保存总长度(仅仅用于这个msi)
    msi_output_fb(jpg_msg->msi, fb);

jpg_msi_work_end:
    // jpg异常,那么去重新启动一下jpg
    // 需要先清除对应资源才行
    if (jpg_err)
    {
        os_printf(KERN_ERR "jpg_err:%X\twhich:%d\n", jpg_err, jpg_msg->which);
        struct framebuff *fb;

        clean_jpg_res(jpg_msg);
        jpg_msg->last_fb = NULL;
        jpg_msg->now_fb  = NULL;

        // 理论这里一定可以申请到空间才对,因为之前刚刚释放内存
        uint8_t *p_buf1 = (uint8_t *) mem_cache_alloc(jpg_msg->mem_info, jpg_msg->mem_info_size, jpg_msg->need_size, STREAM_MALLOC);
        uint8_t *p_buf2 = (uint8_t *) mem_cache_alloc(jpg_msg->mem_info, jpg_msg->mem_info_size, jpg_msg->need_size, STREAM_MALLOC);
        ASSERT(p_buf1 && p_buf2);
        // 重新配置jpg的地址,启动
        fb = fbpool_get(&jpg_msg->pool, 0, NULL);
        ASSERT(fb);
        fb->data = p_buf1;
        fb->len  = jpg_msg->jpg_node_len;
        jpg_set_addr(jpg_msg->jpg, (uint32) fb->data, fb->len);
        jpg_msg->now_fb      = fb;
        jpg_msg->use_last_fb = fb;

        fb = fbpool_get(&jpg_msg->pool, 0, NULL);
        ASSERT(fb);
        fb->data = p_buf2;
        fb->len  = jpg_msg->jpg_node_len;
        jpg_set_addr(jpg_msg->jpg, (uint32) fb->data, fb->len);
        jpg_msg->last_fb = fb;
        jpg_msg->err     = 0;
        jpg_msg->running = 1;

        jpg_set_data_from(jpg_msg->jpg, jpg_msg->src_from);

        if (jpg_msg->src_from == VPP_DATA0 || jpg_msg->src_from == VPP_DATA1)
        {
            vppdone_func_register(VPP_JPEG0_START + jpg_msg->which, vpp_start_JPEG, (uint32_t) jpg_msg);
        }
        else
        {
            jpg_set_ready(jpg_msg->jpg);
            jpg_open(jpg_msg->jpg);
        }

        // jpg_open(jpg_msg->jpg);
    }

    return 0;
}
static int jpg_msi_action(struct msi *msi, uint32 cmd_id, uint32 param1, uint32 param2)
{
    int                  ret     = RET_OK;
    struct jpg_V3_msi_s *jpg_msg = (struct jpg_V3_msi_s *) msi->priv;
    switch (cmd_id)
    {
        case MSI_CMD_POST_DESTROY:
        {
            fbpool_destroy(&jpg_msg->pool);
            os_msgq_del(&jpg_msg->msgq);
            os_event_del(&jpg_msg->evt);
            mem_cache_destroy(jpg_msg->mem_info, jpg_msg->mem_info_size, STREAM_FREE);
            msi->name = NULL; // 这里比较特殊,正常不能清空的
            STREAM_LIBC_FREE(jpg_msg);
        }
        break;
        case MSI_CMD_PRE_DESTROY:
        {
            vppdone_func_unregister(VPP_JPEG0_START + jpg_msg->which);
            // 先关闭workqueue(防止有报错,将jpg重新启动)
            os_work_cancle2(&jpg_msg->work, 1);
            // 如果编码源是VPP_DATA0或者VPP_DATA1就需要特定时间close
            if (jpg_msg->vpp_close_flag)
            {
                jpg_msg->end_flag = 1;
                os_event_wait(&jpg_msg->evt, MSI_JPG_END_FLAG | MSI_JPG_BUF_ERR | MSI_JPG_DONE_ERR | MSI_JPG_BUF_FULL_ERR, NULL, OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, 1000);
            }

            // 关闭jpg
            if (jpg_msg->running)
            {
                jpg_close(jpg_msg->jpg);
                jpg_msg->running = 0;
            }

            g_jpg_msi[jpg_msg->which] = NULL;

            // 清除资源,理论队列的内容已经没有用了,不需要管理

            // 清理一些临时资源,检查一下now_fb是否有数据?(正常流程是去释放,时间不释放也不影响,应为没有对msi进行引用)
            clean_jpg_res(jpg_msg);
            jpg_msg->last_fb = NULL;
            jpg_msg->now_fb  = NULL;
        }
        break;

        case MSI_CMD_GET_RUNNING:
        {
            if (param1)
            {
                uint32_t running = 0;
                if (jpg_msg->running && !jpg_msg->err)
                {
                    running = 1;
                }
                else
                {
                    os_printf(KERN_INFO "jpg_msg:%X\tjpg_msg_err:%d\n", jpg_msg, jpg_msg->err);
                    os_printf("jpg_msg->running:%d\n", jpg_msg->running);
                    os_printf("jpg_msg->err:%d\n", jpg_msg->err);
                }
                *(uint32_t *) param1 = running;
            }
        }
        break;

        case MSI_CMD_HARDWARE_JPEG:
        {
            uint32_t cmd_self = (uint32_t) param1;
            uint32_t arg      = param2;
            switch (cmd_self)
            {
                // 如果需要实现多个编码size,可能需要保存,然后通过中断自动切换
                case MSI_JPEG_HARDWARE_MSG:
                {
                    uint16_t w, h;
                    w          = arg >> 16;
                    h          = arg & 0xffff;
                    jpg_msg->w = w;
                    jpg_msg->h = h;
                    // jpg_set_size(jpg_msg->jpg, h, w);
                }
                break;

                case MSI_JPEG_HARDWARE_START:
                {
                    if (!jpg_msg->running)
                    {
                        // 首先预先分配空间,把私有结构体空间一起分配(浪费一点,但是可以在中断直接赋值)
                        uint8_t *p_buf1 = (uint8_t *) mem_cache_alloc(jpg_msg->mem_info, jpg_msg->mem_info_size, jpg_msg->need_size, STREAM_MALLOC);
                        uint8_t *p_buf2 = (uint8_t *) mem_cache_alloc(jpg_msg->mem_info, jpg_msg->mem_info_size, jpg_msg->need_size, STREAM_MALLOC);
                        if (!p_buf1 && !p_buf2)
                        {
                            os_printf("%s:%d mem_cache_alloc failed\n", __func__, __LINE__);
                            break;
                        }

                        // 这个相当于预先分配,所以这里释放(实际会保留缓冲区,可以让jpg中断快速查找到),因为mem_cache内部是超时或者销毁才会真正释放内存的
                        mem_cache_pre_alloc(jpg_msg->mem_info, jpg_msg->mem_info_size, jpg_msg->need_size, STREAM_MALLOC, PRE_ALLOC_COUNT);
                        OS_WORK_REINIT(&jpg_msg->work);
                        struct framebuff *fb;

                        // 硬件初始化
                        jpg_init(jpg_msg->jpg, jpg_msg->dqtable_index, jpg_msg->qt);
                        jpg_set_size(jpg_msg->jpg, jpg_msg->h, jpg_msg->w);
                        if (jpg_msg->src_from == VPP_DATA0 || jpg_msg->src_from == VPP_DATA1)
                        {
                            jpg_msg->vpp_close_flag = 1;
                        }

                        jpg_set_data_from(jpg_msg->jpg, jpg_msg->src_from);
                        jpg_set_hw_check(jpg_msg->jpg, 1);

                        // 注册中断
                        jpg_request_irq(jpg_msg->jpg, jpg_msi_outbuff_full_isr, JPG_IRQ_FLAG_JPG_BUF_FULL, msi);
                        jpg_request_irq(jpg_msg->jpg, jpg_msi_buf_err, JPG_IRQ_FLAG_ERROR, msi);
                        jpg_request_irq(jpg_msg->jpg, jpg_msi_done_isr, JPG_IRQ_FLAG_JPG_DONE, msi);

                        jpg_set_vsync_dly(jpg_msg->jpg, 1);
                        jpg_select_oe_using(jpg_msg->jpg, 0, 1);

                        fb = fbpool_get(&jpg_msg->pool, 0, NULL);
                        ASSERT(fb);
                        fb->data = p_buf1;
                        fb->len  = jpg_msg->jpg_node_len;
                        jpg_set_addr(jpg_msg->jpg, (uint32) fb->data, fb->len);
                        jpg_msg->now_fb      = fb;
                        jpg_msg->use_last_fb = fb;

                        fb = fbpool_get(&jpg_msg->pool, 0, NULL);
                        ASSERT(fb);
                        fb->data = p_buf2;
                        fb->len  = jpg_msg->jpg_node_len;
                        jpg_set_addr(jpg_msg->jpg, (uint32) fb->data, fb->len);

                        // 设置一下scale1是手动还是自动
                        jpg_set_autoscale(jpg_msg->jpg, jpg_msg->scale1_flag);
                        // 记录最后一个配置链表
                        jpg_msg->last_fb = fb;
                        jpg_msg->running = 1;
                        if (jpg_msg->src_from == VPP_DATA0 || jpg_msg->src_from == VPP_DATA1)
                        {
                            vppdone_func_register(VPP_JPEG0_START + jpg_msg->which, vpp_start_JPEG, (uint32_t) jpg_msg);
                        }
                        else
                        {
                            jpg_set_ready(jpg_msg->jpg);
                            jpg_open(jpg_msg->jpg);
                        }
                        // jpg_open(jpg_msg->jpg);
                        // 唤醒work,然后去预先分配空间
                        os_run_work(&jpg_msg->work);
                    }
                    else
                    {
                    }
                }
                break;

                case MSI_JPEG_HARDWARE_STOP:
                {
                    if (jpg_msg->running)
                    {
                        os_work_cancle2(&jpg_msg->work, 1);
                        if (jpg_msg->scale1_flag)
                        {
                            jpg_msg->scale1_flag = 0;
                            scale_close(jpg_msg->scale_dev);
                            jpg_close(jpg_msg->jpg);
                        }
                        else if (jpg_msg->vpp_close_flag)
                        {
                            // 如果编码源是VPP_DATA0或者VPP_DATA1就需要特定时间close
                            if (jpg_msg->vpp_close_flag)
                            {
                                jpg_msg->end_flag = 1;
                                os_event_wait(&jpg_msg->evt, MSI_JPG_END_FLAG | MSI_JPG_BUF_ERR | MSI_JPG_DONE_ERR | MSI_JPG_BUF_FULL_ERR, NULL, OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, 100);
                            }
                            jpg_msg->vpp_close_flag = 0;
                            jpg_close(jpg_msg->jpg);
                        }
                        else
                        {
                            jpg_close(jpg_msg->jpg);
                        }
                        jpg_msg->running = 0;
                        jpg_msg->err     = 0;
                        // 清除异常
                        os_event_wait(&jpg_msg->evt, MSI_JPG_DONE_ERR | MSI_JPG_BUF_FULL_ERR | MSI_JPG_BUF_ERR, NULL, OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, 0);
                        // 需要清理一下队列?这个时候队列的数据应该是不需要了以及now_fb的数据也要清除
                        {
                            struct framebuff *fb;
                            int32             err = 0;
                            // 将队列的数据帮忙发送出去
                            while (!err)
                            {
                                fb = (struct framebuff *) os_msgq_get2(&jpg_msg->msgq, 0, &err);
                                if (!err)
                                {
                                    struct framebuff *tmp_fb = fb;
                                    while (tmp_fb)
                                    {
                                        tmp_fb->msi = jpg_msg->msi;
                                        // 这个模块是特殊处理
                                        msi_get(tmp_fb->msi);
                                        tmp_fb = tmp_fb->next;
                                    }
                                    msi_output_fb(jpg_msg->msi, fb);
                                }
                            }
                        }

                        // 中断的临时资源清除
                        {
                            clean_jpg_res(jpg_msg);
                            jpg_msg->last_fb = NULL;
                            jpg_msg->now_fb  = NULL;
                        }
                    }
                    else
                    {
                        os_work_cancle2(&jpg_msg->work, 1);
                    }
                }
                break;

                case MSI_JPEG_HARDWARE_FROM:
                {
                    if (arg >= VPP_DATA0 && arg <= SOFT_DATA)
                    {
                        jpg_msg->src_from = arg;
                        // jpg_set_data_from(jpg_msg->jpg, arg);
                    }
                    else
                    {
                        os_printf(KERN_ERR "jpg set data from err:%d\n", arg);
                    }
                }
                break;

                // 配置gen42的类型
                case MSI_JPEG_HARDWARE_SET_GEN420_TYPE:
                {
                    jpg_msg->gen420_type = arg;
                }
                break;

                case MSI_JPEG_HARDWARE_SET_SCALE1_TYPE:
                {
                    jpg_msg->scale1_type = arg;
                }
                break;

                case MSI_JPEG_SET_TIME:
                {
                    jpg_msg->set_time = arg;
                }
                break;

                case MSI_JPEG_SET_LEN:
                {
                    jpg_msg->target_len = arg;
                }
                break;

                case MSI_JPEG_SET_SCALE1_FLAG:
                {
                    jpg_msg->scale1_flag = arg;
                }
                break;
                case MSI_JPEG_SET_OUTPUT_MSI:
                {
                    jpg_msg->output_msi = (struct msi *) arg;
                }
                break;
                default:
                    break;
            }
        }
        break;
        case MSI_CMD_FREE_FB:
        {
            struct framebuff *fb = (struct framebuff *) param1;
            if (fb->data)
            {
                mem_cache_free(fb->data);
            }
            if (fb->priv)
            {
                fb->priv = NULL;
            }
            fbpool_put(&jpg_msg->pool, fb);
            // 不需要内核去释放fb
            ret = RET_OK + 1;
        }
        break;

        case MSI_CMD_SET_DATATAG:
        {
            uint8_t datatag  = (uint8_t) param1;
            jpg_msg->datatag = datatag;
        }
        break;
    }
    return ret;
}

// 这个接口尽量不在外部调用,因为硬件只有一个
// 所以由中间流对这个硬件控制
struct msi *hardware_jpg_msi(uint8_t which_jpg, uint8_t src_from, uint16_t jpg_node_len, uint16_t jpg_node_count)
{
    if (which_jpg >= HARDWARE_JPG_NUM)
    {
        return NULL;
    }
    // 硬件只有一个,所以只能启动一次,外部显式调用一次msi_destroy才可以
    if (g_jpg_msi[which_jpg])
    {
        return NULL;
    }
    struct jpg_V3_msi_s *jpg_msg = (struct jpg_V3_msi_s *) STREAM_LIBC_ZALLOC(sizeof(struct jpg_V3_msi_s) + sizeof(struct mem_info *) * MAX_JPG_NODE_NUM);
    struct msi          *msi     = NULL;
    uint8_t              isnew   = 0;
    if (jpg_msg)
    {
        os_sprintf(jpg_msg->msi_name, "H_JPG%d_%08X", which_jpg, (uint32) os_jiffies() & 0xFFFFFFFF);
        msi = msi_new(jpg_msg->msi_name, 0, &isnew);
        ASSERT(msi);
        // 这里一定是新创建
        ASSERT(isnew);
        msi->priv              = (void *) jpg_msg;
        jpg_msg->mem_info      = (struct mem_info **) (jpg_msg + 1); // 放到结构体的后面
        jpg_msg->mem_info_size = MAX_JPG_NODE_NUM;
        jpg_msg->which         = which_jpg;
        jpg_msg->src_from      = src_from;
        jpg_msg->qt            = 0xf;
        // 这里式分配多少帧,如果分配1帧,就是应用要快速去处理,否则可能来不及
        // 如果帧数据量大,就可能出现最后节点不够的可能
        os_msgq_init(&jpg_msg->msgq, 1);
        os_event_init(&jpg_msg->evt);
        if (msi)
        {
            fbpool_init(&jpg_msg->pool, MAX_JPG_NODE_NUM);
            jpg_msg->msi          = msi;
            // 固定输出,外部尽量不要用这个msi,统一给到RS_JPG_CONCAT这个msi去管理
            jpg_msg->jpg_node_len = jpg_node_len;
            jpg_msg->need_size    = jpg_node_len + sizeof(struct jpg_node_s);
            jpg_msg->jpg          = (struct jpg_device *) dev_get(which_jpg + HG_JPG0_DEVID);
            jpg_msg->scale_dev    = (struct scale_device *) dev_get(HG_SCALE1_DEVID);
            jpg_close(jpg_msg->jpg);

            jpg_msg->dqtable_index = DQT_DEF;
            jpg_msg->target_len    = TARGET_JPG_LEN;
            jpg_msg->qt            = 0xf;
            msi->action            = jpg_msi_action;
            msi->enable            = 1;
            // 启动一个workqueue,去将fb发送出去,中断唤醒一次,workqueue执行一次
            OS_WORK_INIT(&jpg_msg->work, jpg_msi_work, 0);
        }
    }

    if (!msi)
    {
        if (jpg_msg)
        {
            STREAM_LIBC_FREE(jpg_msg);
        }
    }
    else
    {
        g_jpg_msi[which_jpg] = msi;
    }
    return msi;
}

void jpg_mem_init(int num)
{
    return;
}

// 减少频繁申请空间,使用mjpg自己的内存池,暂时只是支持了预先分配
#else
#endif
#else
struct msi *hardware_jpg_msi(uint8_t which_jpg, uint8_t src_from, uint16_t jpg_node_len, uint16_t jpg_node_count)
{
    return NULL;
}
#endif