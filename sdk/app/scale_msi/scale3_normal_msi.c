
#include "scale_msi.h"
#include "dev/vpp/hgvpp.h"
#include "lib/video/vpp/vpp_dev.h"
#include "dev/scale/hgscale.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "user_work/user_work.h"
#include "scale3_normal_msi.h"
#include "hal/isp.h"
#include "mem_cache/mem_cache.h"
#define SCALE3_MAX_TTL   1000
// 按照哪个镜头开始kick
#define START_KICK_VIDEO ISP_VIDEO_0
#define MAX_RECV_COUNT   10

extern int32_t takephoto_name_no_dir_time(char *filename, int filename_size, struct timeval *t);

uint32_t yuv_buf_line(uint8_t which);

#define MAX_COUNT   5
#define EXTRA_COUNT 5 // 额外申请的内存池块数量

/*********************************************************************
 * 这个模块是分别输出两种图片的yuv,一种是原图,一种是缩略图
 ********************************************************************/
// data申请空间函数
#define STREAM_MALLOC av_psram_malloc
#define STREAM_FREE   av_psram_free
#define STREAM_ZALLOC av_psram_zalloc

// 结构体申请空间函数
#define STREAM_LIBC_MALLOC av_malloc
#define STREAM_LIBC_FREE   av_free
#define STREAM_LIBC_ZALLOC av_zalloc

struct scale3_normal_msi
{
    struct os_work             work;
    struct msi                *msi;
    struct scale_device       *scale_dev;
    // 申请一个内存池块,用于保存scale3的数据,支持超时释放,
    struct mem_info          **mem_info;
    uint32_t                   mem_info_size;
    struct fbpool              pool;
    struct os_msgqueue         msgq;
    uint8_t                   *buf;
    uint16_t                   iw, ih;
    uint16_t                   ow, oh;
    uint16_t                   r_ow, r_oh; // 设置scale3即将输出的size(为了某些情况延时配置寄存器,需要记录)
    uint32_t                   magic;
    struct framebuff          *fb;
    struct framebuff          *extern_fb;
    struct framebuff          *scale3_input_fb;
    uint16_t                   seq_count;
    struct timeval             t;
    uint32_t                   mark_time;
    uint8_t                    tmp_seq;
    uint8_t                    force_stype;
    uint8_t                    ready : 1, extern_fb_ready : 1, start : 1, exit : 1, splice : 1, splice_kick : 1, mark_time_flag : 1;
    // 动态输出流表:每个流指定cam_id/w/h/x/y,最大SCALE3_MAX_STREAM个,运行时动态增删
    struct scale3_stream_cfg_s streams[SCALE3_MAX_STREAM];
    uint8_t                    stream_used[SCALE3_MAX_STREAM];
    uint8_t                    cur_stream; // 当前输出流序号,0xff=无
};

void       *get_vpp_buf(uint8_t which);
extern void get_vpp_dev_w_h(uint16_t *dev_type, uint16_t *w, uint16_t *h);

// 申请一个fb的buf
static uint8_t *scale3_fb_buf_malloc(struct scale3_normal_msi *scale3, uint8_t malloc_flag, uint16_t ow, uint16_t oh, uint16_t x, uint16_t y)
{
    uint8_t *p_buf;
    uint32_t buf_size   = ow * oh * 3 / 2;
    // 需要对齐,防止cache影响,takephoto_yuv_arg_s如果放在尾部,会在不同size的时候需要对cache处理,所以统一放在头部,申请后不需要处理cache
    uint32_t align_size = ALIGN(sizeof(struct takephoto_yuv_arg_s), 32);
    uint32_t need_size  = buf_size + align_size;
    if (malloc_flag)
    {
        p_buf = (uint8_t *) mem_cache_alloc(scale3->mem_info, scale3->mem_info_size, need_size, STREAM_MALLOC);
    }
    else
    {
        p_buf = (uint8_t *) mem_cache_alloc(scale3->mem_info, scale3->mem_info_size, need_size, NULL);
    }
    if (p_buf)
    {
        struct takephoto_yuv_arg_s *yuvinfo = (struct takephoto_yuv_arg_s *) (p_buf);
        yuvinfo->yuv_arg.type               = YUV_ARG_NORMAL;
        yuvinfo->yuv_arg.out_w              = ow;
        yuvinfo->yuv_arg.out_h              = oh;
        yuvinfo->yuv_arg.x                  = x;
        yuvinfo->yuv_arg.y                  = y;
        yuvinfo->yuv_arg.y_size             = ow * oh;
        yuvinfo->yuv_arg.y_off              = (uint32_t) p_buf + align_size;
        yuvinfo->yuv_arg.u_off              = (uint32_t) p_buf + align_size + ow * oh;
        yuvinfo->yuv_arg.v_off              = (uint32_t) p_buf + align_size + ow * oh + ow * oh / 4;

        // p_buf指向除去yuv_arg_s后的地址
        p_buf += align_size;
    }
    return p_buf;
}

static int32_t scale3_fb_buf_free(struct scale3_normal_msi *scale3, uint8_t *p_buf)
{
    (void) scale3;
    if (p_buf)
    {
        uint32_t align_size = ALIGN(sizeof(struct takephoto_yuv_arg_s), 32);
        mem_cache_free((uint8_t *) p_buf - align_size);
    }

    return 0;
}

// 从start开始轮转查找下一个used且匹配当前镜头的输出流,无匹配返回0xff
static uint8_t scale3_find_stream(struct scale3_normal_msi *scale3, uint8_t start, uint8_t video_type_cur)
{
    uint8_t i;
    for (i = 0; i < SCALE3_MAX_STREAM; i++)
    {
        uint8_t idx = (uint8_t) ((start + i) % SCALE3_MAX_STREAM);
        if (scale3->stream_used[idx] && (scale3->streams[idx].cam_id == 0xff || scale3->streams[idx].cam_id == video_type_cur))
        {
            return idx;
        }
    }
    return 0xff;
}

static int32_t const_scale3_msi_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    int32_t                   ret    = RET_OK;
    struct scale3_normal_msi *scale3 = (struct scale3_normal_msi *) msi->priv;
    switch (cmd_id)
    {
        // 这里msi已经被删除,那么就要考虑tx_pool的资源释放了
        // 能进来这里,就是代表所有fb都已经用完了
        case MSI_CMD_POST_DESTROY:
        {
            os_msgq_del(&scale3->msgq);
            mem_cache_destroy(scale3->mem_info, scale3->mem_info_size, STREAM_FREE);
            STREAM_FREE(scale3);
        }
        break;

        // 停止硬件,移除没有必要的资源(但是fb的资源不能现在删除,这个时候fb可能外部还在调用)
        case MSI_CMD_PRE_DESTROY:
        {
            scale_close(scale3->scale_dev);
            // 关闭work
            os_work_cancle2(&scale3->work, 1);

            scale3->start = 0;
            scale3->exit  = 1;

            if (scale3->extern_fb)
            {
                msi_delete_fb(scale3->msi, scale3->extern_fb);
                scale3->extern_fb = NULL;
            }

            if (scale3->fb)
            {
                msi_delete_fb(scale3->msi, scale3->fb);
                scale3->fb = NULL;
            }
            struct framebuff *fb = NULL;
            while (1)
            {
                fb = (struct framebuff *) os_msgq_get2(&scale3->msgq, 0, NULL);
                if (fb)
                {
                    msi_delete_fb(NULL, fb);
                    fb = NULL;
                }
                else
                {
                    break;
                }
            }

            if (scale3->scale3_input_fb)
            {
                msi_delete_fb(NULL, scale3->scale3_input_fb);
                scale3->scale3_input_fb = NULL;
            }
        }
        break;
        case MSI_CMD_FREE_FB:
        {
            struct framebuff *fb = (struct framebuff *) param1;

            scale3_fb_buf_free(scale3, (uint8_t *) fb->data);
            fb->data = NULL;
            if (fb->keyfrm)
            {
                scale3->extern_fb_ready = 1;
            }
            else
            {
                fbpool_put(&scale3->pool, fb);
                ret = RET_OK + 1;
            }
            if (!scale3->exit)
            {
                os_run_work(&scale3->work);
            }
        }
        break;

        // 私有命令,特定结构体{w,h,time,stype}
        case MSI_CMD_SCALE3_NORMAL:
        {
            uint32_t cmd_self = (uint32_t) param1;
            // uint32_t arg      = param2;
            switch (cmd_self)
            {
                case MSI_SCALE3_START:
                {
                    scale3->start = param2 & 0x01;
                    os_run_work(&scale3->work);
                    break;
                }
                break;
                case MSI_SCALE3_ADD_STREAM:
                {
                    // 动态添加一个输出流{struct scale3_stream_cfg_s},存放到空闲槽位
                    // 同一cam_id只允许注册一个输出流,重复注册禁止
                    struct scale3_stream_cfg_s *cfg = (struct scale3_stream_cfg_s *) param2;
                    uint8_t                     i;
                    for (i = 0; i < SCALE3_MAX_STREAM; i++)
                    {
                        if (scale3->stream_used[i] && scale3->streams[i].cam_id == cfg->cam_id)
                        {
                            os_printf("scale3 add repeat stream cam_id=%d\n", cfg->cam_id);
                            ret = RET_ERR;
                            break;
                        }
                        if (!scale3->stream_used[i])
                        {
                            scale3->streams[i]     = *cfg;
                            scale3->stream_used[i] = 1;
                            os_run_work(&scale3->work);
                            return RET_OK;
                        }
                    }
                    os_printf("scale3 add stream fail,already full\n");
                    ret = RET_ERR;
                    break;
                }

                case MSI_SCALE3_DEL_STREAM:
                {
                    // 按cam_id删除输出流,在途帧自然走完即可
                    uint8_t cam_id = (uint8_t) param2;
                    uint8_t i;
                    for (i = 0; i < SCALE3_MAX_STREAM; i++)
                    {
                        if (scale3->stream_used[i] && scale3->streams[i].cam_id == cam_id)
                        {
                            scale3->stream_used[i] = 0;
                            os_run_work(&scale3->work);
                            break;
                        }
                    }
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
        break;

        case MSI_CMD_TRANS_FB:
        {
            // 收到命令类型才接收
            struct framebuff *fb = (struct framebuff *) param1;
            if (fb->mtype == F_YUV_CMD)
            {
            }
            else
            {
                ret = RET_OK + 1;
            }
        }
        break;

        case MSI_CMD_TRANS_FB_END:
        {
            os_run_work(&scale3->work);
        }
        break;

        default:
            break;
    }
    return ret;
}

static struct framebuff *scale3_get_fb(struct scale3_normal_msi *scale3, uint8_t malloc_flag)
{
    uint8_t          *p_buf;
    uint16_t          ow, oh;
    uint32_t          align_size = ALIGN(sizeof(struct takephoto_yuv_arg_s), 32);
    struct framebuff *fb         = NULL;
    if (scale3->start && scale3->cur_stream != 0xff)
    {
        struct scale3_stream_cfg_s *cfg = &scale3->streams[scale3->cur_stream];
        ow                              = cfg->w;
        oh                              = cfg->h;
        // 先检查内存池是否有符合内存块,没有就去workqueue去申请
        uint32_t buf_size               = ow * oh * 3 / 2;
        p_buf                           = scale3_fb_buf_malloc(scale3, malloc_flag, ow, oh, cfg->x, cfg->y);
        if (p_buf)
        {
            fb = fbpool_get(&scale3->pool, 0, scale3->msi);
            // 没有fb,则释放内存
            if (!fb)
            {
                scale3_fb_buf_free(scale3, p_buf);
            }
            else
            {
                // 结构体在头部位置
                fb->priv  = (void *) (p_buf - align_size);
                fb->data  = (uint8_t *) p_buf;
                fb->len   = buf_size;
                fb->stype = cfg->force_stype;
            }
        }
    }
    return fb;
}
static int32_t scale3_stream_done(uint32 irq_flag, uint32 irq_data, uint32 param1)
{
    struct scale3_normal_msi   *scale3 = (struct scale3_normal_msi *) irq_data;
    struct takephoto_yuv_arg_s *arg;
    struct framebuff           *fb = NULL;
    uint8_t                    *p_buf;
    uint16_t                    ow = 0, oh = 0;
    uint16_t                    r_oh = 0;
    uint16_t                    iw;
    uint16_t                    ih;
    uint8_t                     new_frame_flag = 0;
    // 报错,直接退出
    if (param1)
    {
        return 0;
    }

    // 是否可以获取新的帧
    if (scale3->splice_kick % 2 == 0)
    {
        new_frame_flag = 1;
    }
    else
    {
        fb = scale3->fb;
    }
    // 获取新的一帧数据
    if (new_frame_flag)
    {
        if (scale3->extern_fb)
        {
            fb                = scale3->extern_fb;
            scale3->extern_fb = NULL;
        }
        else
        {
            // 轮转到下一个匹配当前镜头的输出流,无匹配则不取帧(关闭等待work重试)
            scale3->cur_stream = scale3_find_stream(scale3, (uint8_t) (scale3->cur_stream + 1), video_msg.video_type_next);
            if (scale3->cur_stream != 0xff)
            {
                fb = scale3_get_fb(scale3, 0);
            }
        }
    }
    // 空间不够或者说没有需要产生新的帧,则关闭scale3
    if (!fb)
    {
        goto scale3_stream_done_end;
    }

    // 输出参数已经在fb->priv中,直接获取
    arg = (struct takephoto_yuv_arg_s *) fb->priv;
    ow  = arg->yuv_arg.out_w;
    oh  = arg->yuv_arg.out_h;

    fb->srcID = video_msg.video_type_next + FRAMEBUFF_SOURCE_CAMERA0;
    // 重新修改scale3的参数
    get_vpp_dev_w_h(NULL, &scale3->iw, &scale3->ih);
    uint32_t y, u, v;
    get_vpp_buf_y_u_v(0, &y, &u, &v);
    scale_set_in_yaddr(scale3->scale_dev, y);
    scale_set_in_uaddr(scale3->scale_dev, u);
    scale_set_in_vaddr(scale3->scale_dev, v);

    r_oh  = scale3->splice ? oh / 2 : oh;
    iw    = scale3->iw;
    ih    = scale3->ih;
    p_buf = (uint8_t *) fb->data;
    scale_set_in_out_size(scale3->scale_dev, iw, ih, ow, r_oh);
    scale_set_step(scale3->scale_dev, iw, ih, ow, r_oh);
    scale_set_out_yaddr(scale3->scale_dev, (uint32) p_buf + ow * r_oh * scale3->splice_kick);
    scale_set_out_uaddr(scale3->scale_dev, (uint32) p_buf + ow * oh + ow * r_oh / 4 * scale3->splice_kick);
    scale_set_out_vaddr(scale3->scale_dev, (uint32) p_buf + ow * oh + ow * oh / 4 + ow * r_oh / 4 * scale3->splice_kick);

scale3_stream_done_end:
    // 发送now_data,发送失败也要返回
    if (scale3->splice_kick % 2 == 0)
    {
        if (scale3->fb->keyfrm)
        {
            if (scale3->mark_time_flag)
            {
                scale3->mark_time      = os_jiffies();
                scale3->mark_time_flag = 0;
            }
        }
        if (os_msgq_put(&scale3->msgq, (uint32_t) scale3->fb, 0))
        {
            // 正常不能中断del,但是这个模块是内部,只要del没有一些等待信号量操作,问题不大
            msi_delete_fb(NULL, scale3->fb);
            scale3->fb = NULL;
        }
    }

    if (!fb)
    {
        scale3->ready = 1;
        scale_close(scale3->scale_dev);
    }
    scale3->fb = fb;
    if (scale3->splice)
    {
        scale3->splice_kick++;
    }
    os_run_work(&scale3->work);
    return 0;
}

static int32_t scale3_stream_ov(uint32 irq_flag, uint32 irq_data, uint32 param1)
{
    os_printf("%s:%d\n", __FUNCTION__, __LINE__);
    return 0;
}

static int32_t vpp_start_scale3(uint32_t irq_data)
{
    struct scale3_normal_msi *scale3 = (struct scale3_normal_msi *) irq_data;
    // 如果需要拼接,就要等待镜头2完成才能启动
    if (video_msg.video_type_next == START_KICK_VIDEO)
    {
        get_vpp_dev_w_h(NULL, &scale3->iw, &scale3->ih);
        scale3->fb->srcID = video_msg.video_type_next + FRAMEBUFF_SOURCE_CAMERA0;
        scale_set_in_out_size(scale3->scale_dev, scale3->iw, scale3->ih, scale3->r_ow, scale3->r_oh);
        scale_set_step(scale3->scale_dev, scale3->iw, scale3->ih, scale3->r_ow, scale3->r_oh);

        uint32_t y, u, v;
        get_vpp_buf_y_u_v(0, &y, &u, &v);
        scale_set_in_yaddr(scale3->scale_dev, y);
        scale_set_in_uaddr(scale3->scale_dev, u);
        scale_set_in_vaddr(scale3->scale_dev, v);
        scale_open(scale3->scale_dev);
        return 1;
    }
    else
    {
        return 0;
    }
}

// 如果空间申请不到,就延时去输出
static int32 scale3_normal_msi_work(struct os_work *work)
{
    struct scale3_normal_msi   *scale3     = (struct scale3_normal_msi *) work;
    struct scale_device        *scale_dev  = scale3->scale_dev;
    uint16_t                    delay_time = 1000;
    struct framebuff           *fb         = NULL;
    struct framebuff           *e_fb       = NULL;
    struct takephoto_yuv_arg_s *arg        = NULL;
    uint16_t                    ow         = 0;
    uint16_t                    oh         = 0;
    int32_t                     err        = -1;
    uint32_t                    align_size = ALIGN(sizeof(struct takephoto_yuv_arg_s), 32);
    uint32_t                    buf_size;
    uint8_t                     update_time = 0;
    struct scale3_fb_data_s    *cmd         = NULL;
    struct scale3_fb_input_s   *cmd_input   = NULL;

    fb = (struct framebuff *) os_msgq_get2(&scale3->msgq, 0, &err);
    if (fb)
    {
        if (!fb->keyfrm)
        {
            _os_printf(KERN_INFO "S");
            fb->mtype         = F_YUV;
            fb->time          = os_jiffies();
            arg               = fb->priv;
            arg->yuv_arg.type = YUV_ARG_NONE;
            msi_output_fb(scale3->msi, fb);
        }
        else
        {
            fb->time = scale3->mark_time;
            if (fb->rev)
            {
                msi_recv_fb(scale3->scale3_input_fb->msi, fb);
                msi_delete_fb(NULL, fb);
            }
            else
            {
                msi_output_fb(scale3->msi, fb);
            }
        }

        fb = NULL;
    }

    // 先去检查是否有需要生成额外的yuv数据没
    // 检查如果没有extern_fb,并且有额外命令,则生成一个fb
    if (scale3->extern_fb_ready && !scale3->extern_fb)
    {
        if (scale3->scale3_input_fb)
        {
            cmd_input = (struct scale3_fb_input_s *) scale3->scale3_input_fb->data;
            // 检查是否已经完成,完成后就删除重新获取新的fb
            if (cmd_input->count == scale3->seq_count)
            {
                os_printf("del\n");
                msi_delete_fb(NULL, scale3->scale3_input_fb);
                scale3->scale3_input_fb = NULL;
            }
        }

        if (!scale3->scale3_input_fb)
        {
            scale3->scale3_input_fb = msi_get_fb(scale3->msi, 0);
            if (scale3->scale3_input_fb)
            {
                scale3->tmp_seq   = 0;
                scale3->seq_count = 0;
                update_time       = 1;
                cmd_input         = (struct scale3_fb_input_s *) scale3->scale3_input_fb->data;
                cmd               = &cmd_input->cmd[scale3->seq_count];
                scale3->tmp_seq   = cmd->seq;
            }
        }
        else
        {
            cmd_input = (struct scale3_fb_input_s *) scale3->scale3_input_fb->data;
            cmd       = &cmd_input->cmd[scale3->seq_count];
            if (scale3->tmp_seq != cmd->seq)
            {
                // seq不一样,需要更新一下参数
                update_time = 1;
            }
            scale3->tmp_seq = cmd->seq;
        }
        if (update_time)
        {
            scale3->mark_time_flag = 1;
            gettimeofday(&scale3->t, NULL);
        }

        if (scale3->scale3_input_fb)
        {
            scale3->seq_count++;
            if (cmd->w && cmd->h)
            {
                ow = cmd->w;
                oh = cmd->h;
            }
            else
            {
                ow = scale3->iw;
                oh = scale3->ih;
            }
            // 申请yuv的空间
            buf_size      = ow * oh * 3 / 2;
            uint8_t *data = (uint8_t *) scale3_fb_buf_malloc(scale3, 1, ow, oh, 0, 0);
            if (data)
            {
                arg                  = (struct takephoto_yuv_arg_s *) (data - align_size);
                e_fb                 = fb_alloc(data, buf_size, F_YUV << 8 | FSTYPE_NONE, scale3->msi);
                // 如果w和h其中一个为0,则使用iw和ih
                e_fb->datatag        = ~0; // 设置特殊标志,用于识别是否是额外生成的fb
                e_fb->priv           = (void *) (arg);
                e_fb->keyfrm         = 1;
                arg->yuv_arg.magic   = cmd->magic;
                arg->yuv_arg.type    = cmd_input->force_type;
                arg->target_encode_w = cmd->encode_w;
                arg->target_encode_h = cmd->encode_h;
                takephoto_name_no_dir_time(arg->name, sizeof(arg->name), &scale3->t);
                scale3->extern_fb_ready = 0;
                if (cmd->forward_en)
                {
                    e_fb->rev = 1;
                }
                else
                {
                    e_fb->rev = 0;
                }
                scale3->extern_fb = e_fb;
            }
        }
    }

    // scale3可能空间不够关闭了中断,也可能是第一次启动,所以如果是不同模式,第一次启动应该有条件,比如一定要等到镜头1才可以启动
    if (scale3->ready)
    {
        if (scale3->extern_fb)
        {
            scale3->fb        = scale3->extern_fb;
            scale3->extern_fb = NULL;
            fb                = NULL;
        }
        else
        {
            // 找到下一个匹配当前镜头的输出流,无匹配则延时等待
            // 固定启动第一个镜头?
            scale3->cur_stream = scale3_find_stream(scale3, scale3->cur_stream, START_KICK_VIDEO);
            if (scale3->cur_stream == 0xff)
            {
                goto scale3_normal_msi_work_end;
            }
            scale3->fb = scale3_get_fb(scale3, 1);
        }
        if (!scale3->fb)
        {
            goto scale3_normal_msi_work_end;
        }
        uint16_t r_oh;

        // 输出参数已经在fb->priv中,直接获取
        arg = (struct takephoto_yuv_arg_s *) scale3->fb->priv;
        ow  = arg->yuv_arg.out_w;
        oh  = arg->yuv_arg.out_h;

        // 拼接虽然是两个镜头拼接,实际硬件还是一个一个镜头数据输入,所以output偏移修改,硬件寄存器依然按照原来配置
        if (scale3->splice)
        {
            r_oh = oh / 2;
        }
        else
        {
            r_oh = oh;
        }
        scale3->r_ow = ow;
        scale3->r_oh = r_oh;
        scale_set_start_addr(scale_dev, 0, 0);
        // 暂时固定,如果遇到需要动态修改的,可以通过参数之类来切换
        scale_set_dma_to_memory(scale_dev, 1);
        scale_set_data_from_vpp(scale_dev, 1);
        scale_set_line_buf_num(scale_dev, yuv_buf_line(0));
        scale_set_out_yaddr(scale_dev, (uint32) scale3->fb->data);
        scale_set_out_uaddr(scale_dev, (uint32) scale3->fb->data + ow * oh);
        scale_set_out_vaddr(scale_dev, (uint32) scale3->fb->data + ow * oh + ow * oh / 4);
        scale_request_irq(scale_dev, FRAME_END, scale3_stream_done, (uint32) scale3);
        scale_request_irq(scale_dev, INBUF_OV, scale3_stream_ov, (uint32) scale3);
        scale3->ready       = 0;
        scale3->splice_kick = 0;
        // 双镜头拼接,需要等待特定镜头完成才能正常开始
        if (scale3->splice)
        {
            scale3->splice_kick++;
        }
        vppdone_func_register(SCALE3_KICK, vpp_start_scale3, (uint32) scale3);
    }

scale3_normal_msi_work_end:
    if (delay_time)
    {
        os_run_work_delay(work, delay_time);
    }
    mem_cache_gc(scale3->mem_info, scale3->mem_info_size, SCALE3_MAX_TTL, STREAM_FREE);
    return 0;
}

static struct msi *scale3_common(const char *name, uint16_t ow, uint16_t oh, uint8_t max_fb_count, uint8_t *new)
{
    uint8_t     isnew;
    struct msi *msi = msi_new(name, MAX_RECV_COUNT, &isnew);
    if (new)
    {
        *new = isnew;
    }
    if (isnew)
    {
        struct scale_device      *scale_dev = (struct scale_device *) dev_get(HG_SCALE3_DEVID);
        struct scale3_normal_msi *scale3    = (struct scale3_normal_msi *) STREAM_ZALLOC(sizeof(struct scale3_normal_msi) + sizeof(struct mem_info *) * (EXTRA_COUNT + max_fb_count));
        scale3->scale_dev                   = scale_dev;
        scale3->ready                       = 1;
        scale3->ow                          = ow;
        scale3->oh                          = oh;
        scale3->msi                         = msi;
        scale3->extern_fb_ready             = 1;
        scale3->splice_kick                 = 0;
        scale3->cur_stream                  = 0xff;
        scale3->mem_info                    = (struct mem_info **) (scale3 + 1); // 放到结构体的后面
        scale3->mem_info_size               = max_fb_count + EXTRA_COUNT;
        msi->priv                           = (void *) scale3;
        msi->action                         = const_scale3_msi_action;
        msi->enable                         = 1;
        if (!max_fb_count)
        {
            os_msgq_init(&scale3->msgq, EXTRA_COUNT);
        }
        else
        {
            os_msgq_init(&scale3->msgq, max_fb_count);
            fbpool_init(&scale3->pool, max_fb_count);
        }
        get_vpp_dev_w_h(NULL, &scale3->iw, &scale3->ih);
        OS_WORK_INIT(&scale3->work, scale3_normal_msi_work, 0);
    }
    return msi;
}

// 旧接口兼容:注册一个通配所有镜头的默认输出流
static void scale3_stream_default(struct scale3_normal_msi *scale3, uint16_t ow, uint16_t oh, uint8_t force_stype)
{
    struct scale3_stream_cfg_s cfg;
    cfg.cam_id             = 0xff; // 通配所有镜头,保持旧接口行为
    cfg.x                  = 0;
    cfg.y                  = 0;
    cfg.w                  = ow;
    cfg.h                  = oh;
    cfg.force_stype        = force_stype;
    scale3->streams[0]     = cfg;
    scale3->stream_used[0] = 1;
    scale3->cur_stream     = 0;
}

int32_t scale3_normal_msi_add_stream(struct msi *msi, const struct scale3_stream_cfg_s *cfg)
{
    return msi_do_cmd(msi, MSI_CMD_SCALE3_NORMAL, MSI_SCALE3_ADD_STREAM, (uint32_t) cfg);
}

int32_t scale3_normal_msi_del_stream(struct msi *msi, uint8_t cam_id)
{
    return msi_do_cmd(msi, MSI_CMD_SCALE3_NORMAL, MSI_SCALE3_DEL_STREAM, (uint32_t) cam_id);
}

struct msi *scale3_normal_msi(const char *name, uint16_t ow, uint16_t oh)
{
    uint8_t     isnew = 0;
    struct msi *msi   = scale3_common(name, ow, oh, MAX_COUNT, &isnew);

    if (isnew)
    {
        struct scale3_normal_msi *scale3 = (struct scale3_normal_msi *) msi->priv;
        scale3->extern_fb_ready          = 1;
        scale3->force_stype              = FSTYPE_NONE;
        scale3->start                    = 1;
        msi->enable                      = 1;
        scale3_stream_default(scale3, ow, oh, FSTYPE_NONE);
        os_run_work(&scale3->work);
    }
    return msi;
}

struct msi *scale3_normal_msi2(const char *name, uint8_t force_stype, uint16_t ow, uint16_t oh)
{
    uint8_t     isnew = 0;
    struct msi *msi   = scale3_common(name, ow, oh, MAX_COUNT, &isnew);

    if (isnew)
    {
        struct scale3_normal_msi *scale3 = (struct scale3_normal_msi *) msi->priv;
        scale3->extern_fb_ready          = 1;
        scale3->force_stype              = force_stype;
        scale3->start                    = 0;
        msi->enable                      = 1;
        scale3_stream_default(scale3, ow, oh, force_stype);
        os_run_work(&scale3->work);
    }
    return msi;
}

/************************************************************
// 双镜头需要拼接的scale3
// 不带屏
 * splice: 是否需要拼接
 ************************************************************/

struct msi *scale3_msi_no_lcd(const char *name, uint8_t splice, uint8_t force_stype, uint16_t ow, uint16_t oh)
{
    uint8_t     isnew = 0;
    struct msi *msi   = scale3_common(name, ow, oh, 0, &isnew);

    if (isnew)
    {
        struct scale3_normal_msi *scale3 = (struct scale3_normal_msi *) msi->priv;
        scale3->extern_fb_ready          = 1;
        scale3->force_stype              = force_stype;
        scale3->start                    = 0;
        scale3->splice                   = splice;
        scale3->splice_kick              = 0;
        msi->enable                      = 1;
        scale3_stream_default(scale3, ow, oh, force_stype);
        os_run_work(&scale3->work);
    }
    return msi;
}

/************************************************************
// 动态多流模式
// 不注册默认流,输出规格完全由scale3_normal_msi_add_stream动态配置
// splice: 是否需要拼接  max_fb_count: fb结构池深度,0=不预分配(纯extern模式)
 ************************************************************/

struct msi *scale3_normal_msi_multi(const char *name, uint8_t splice, uint8_t max_fb_count)
{
    uint8_t     isnew = 0;
    struct msi *msi   = scale3_common(name, 0, 0, max_fb_count, &isnew);

    if (isnew)
    {
        struct scale3_normal_msi *scale3 = (struct scale3_normal_msi *) msi->priv;
        scale3->extern_fb_ready          = 1;
        scale3->force_stype              = FSTYPE_NONE;
        scale3->start                    = 1;
        scale3->splice                   = splice;
        scale3->splice_kick              = 0;
        msi->enable                      = 1;
        os_run_work(&scale3->work);
    }
    return msi;
}
