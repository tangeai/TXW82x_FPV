#include "basic_include.h"
#include "project_config.h"
#include "lib/multimedia/msi.h"
#include "stream_define.h"
#include "app_lcd/app_lcd.h"
#include "lib/video/dvp/jpeg/jpg.h"
#include "app/video_app/file_thumb.h"
#include "stream_define.h"
#include "osal_file.h"
#include "hal/isp.h"
#include "g711.h"
#include "ec_const.h"
#include "app/audio_msi/audio_adc.h"
#include "platforms.h"
#include "app/video_app/video_msi.h"

#define REV_TEST_H264  "h264_test_video"
#define REV_TEST_AUDIO  "pcm_test_audio_1"
// 结构体申请空间函数
#define CY_P2P_LIBC_MALLOC 	av_malloc
#define CY_P2P_LIBC_FREE 		av_free
#define CY_P2P_LIBC_ZALLOC 	av_zalloc

struct p2p_encode_msi_s
{
    struct msi *msi;
    struct os_event evt;
};

extern int TciSendFrameEx(int channel, int stream, TCMEDIA mt, const uint8_t *pFrame, int length, uint32_t ts, int uFrameFlags);

#define AUDIO_PCM_SAMPLES_PER_FRAME  320
#define AUDIO_G711A_FRAME_SIZE       320
// 音频采样格式: (samplerate<<2)|(databits<<1)|channel = (0<<2)|(1<<1)|0 = 2
#define AUDIO_FMT_8K_16BIT_MONO     ((AUDIO_SAMPLE_8K << 2) | (AUDIO_DATABITS_16 << 1) | AUDIO_CHANNEL_MONO)

/* TXW826 和 TXW828 子码流 H264 来源不同 (chip-specific):
 *   826: VPP → gen420 硬件生成 YUV420 → H264, stype=FSTYPE_H264_GEN420_DATA
 *   828: VPP → VPP_DATA1 直出 YUV → H264, stype=FSTYPE_H264_VPP_DATA1
 * 下面的 action 白名单和主循环分流都按 chip 区分. */
#if defined(__TXW826__)
#define H264_SUB_STREAM_STYPE   FSTYPE_H264_GEN420_DATA
#else
#define H264_SUB_STREAM_STYPE   FSTYPE_H264_VPP_DATA1
#endif

static int32 video_recv_action(struct msi *msi, uint32 cmd_id, uint32 param1, uint32 param2)
{
    switch (cmd_id) {
        case MSI_CMD_TRANS_FB:
        {
            struct framebuff *fb = (struct framebuff *)param1;
            if (fb->mtype == F_H264 &&
                (fb->stype == FSTYPE_H264_VPP_DATA0 ||
                 fb->stype == H264_SUB_STREAM_STYPE))
                return RET_OK;
            return RET_ERR;
        }
        default:
            break;
    }
    return RET_OK;
}

static void av_send_thread(void)
{
    /* ① 先创建接收端 msi, 设好 action/enable */
    struct msi *video_msi = msi_new(REV_TEST_H264, 4, NULL);
    if (video_msi)
    {
        video_msi->action = video_recv_action;
        video_msi->enable = 1;
    }

    /* ② 再挂到上游 AUTO_H264
     * msi_find(name, 1) 第二个参数是 get, 会加引用, 用完必须 msi_put 配对释放 */
    struct msi *auto_h264 = msi_find(AUTO_H264, 1);
    if (!auto_h264)
    {
        os_printf("av_send_thread: AUTO_H264 not found, video disabled\r\n");
    }
    else if (video_msi)
    {
        int32 ret = msi_add_output(auto_h264, NULL, REV_TEST_H264);
#if defined(__TXW828__)
        /* 828 需要显式启动 video demux; 826 的 AUTO_H264 始终启动, 无需此命令 */
        msi_do_cmd(auto_h264, MSI_CMD_VIDEO_DEMUX_CTRL, MSI_VIDEO_DEMUX_START, 0);
#endif
        msi_put(auto_h264);
        os_printf("av_send_thread: AUTO_H264 -> %s linked, ret=%d\r\n", REV_TEST_H264, ret);
    }

    /* ---- 音频 MSI 初始化 ---- */
    struct p2p_encode_msi_s *p2p_encode = NULL;
    struct msi *audio_msi = msi_new(REV_TEST_AUDIO, 8, NULL);
    if (audio_msi && !audio_msi->priv)
    {
        p2p_encode = (struct p2p_encode_msi_s *)CY_P2P_LIBC_ZALLOC(sizeof(struct p2p_encode_msi_s));
        ASSERT(p2p_encode);
        audio_msi->priv = p2p_encode;
        os_event_init(&p2p_encode->evt);
        p2p_encode->msi = audio_msi;
        audio_msi->action = NULL;
        audio_msi->enable = 0;
    }
    auadc_msi_add_output(AUSYS_AUAD, REV_TEST_AUDIO);
    if (audio_msi)
        audio_msi->enable = 1;

    os_printf("av_send_thread: video_msi=%p audio_msi=%p\r\n", video_msi, audio_msi);

    if (!video_msi || !audio_msi) {
        os_printf("av_send_thread: msi init failed, exit\r\n");
        return;
    }

    struct framebuff *fb = NULL;
    struct framebuff *audio_fb = NULL;
    struct fb_h264_s *r_fb_priv = NULL;
    uint8_t g711a_buf[AUDIO_G711A_FRAME_SIZE];
//    uint32_t loop_cnt = 0;
//    uint32_t video_cnt = 0;
//    uint32_t video_cnt1 = 0;
//    uint32_t audio_cnt = 0;
//    uint32_t video_fb_other = 0;
//    uint32_t v0_ts = 0;
//    uint32_t v1_ts = 0;
//    uint32_t a_ts = 0;
    /* ---- 音视频统一发送循环 ---- */
    while (1)
    {
        /* 获取并发送视频帧 */
        fb = msi_get_fb(video_msi, 0);
        if (fb)
        {
            r_fb_priv = (struct fb_h264_s*)fb->priv;
            if((fb->stype == FSTYPE_H264_VPP_DATA0) && (fb->mtype == F_H264))
            {
                TciSendFrameEx(0, 0, TCMEDIA_VIDEO_H264, fb->data, fb->len, fb->time, r_fb_priv->type==1?1:0);
//                video_cnt++;
//                v0_ts = fb->time;
            }
            else if((fb->stype == H264_SUB_STREAM_STYPE) && (fb->mtype == F_H264))
            {
                TciSendFrameEx(0, 1, TCMEDIA_VIDEO_H264, fb->data, fb->len, fb->time, r_fb_priv->type==1?1:0);
//                video_cnt1++;
//                v1_ts = fb->time;
            }
            else
            {
//                video_fb_other++;
            }
            msi_delete_fb(NULL, fb);
            fb = NULL;
        }

        /* 获取并发送音频帧 */
        audio_fb = msi_get_fb(audio_msi, 0);
        if (audio_fb)
        {
            if(audio_fb->mtype == F_AUDIO)
            {
                short *pcm_samples = (short *)audio_fb->data;
                int i;
                for(i = 0; i < AUDIO_PCM_SAMPLES_PER_FRAME; i++)
                    g711a_buf[i] = linear2alaw(pcm_samples[i]);
                TciSendFrameEx(0, 0, TCMEDIA_AUDIO_G711A, g711a_buf, AUDIO_G711A_FRAME_SIZE, audio_fb->time, AUDIO_FMT_8K_16BIT_MONO);
//                audio_cnt++;
//                a_ts = audio_fb->time;
            }
            msi_delete_fb(NULL, audio_fb);
            audio_fb = NULL;
        }

//        loop_cnt++;
//        if ((loop_cnt % 200) == 0)
//        {
//            os_printf("[av] loop=%d v=%d v1=%d a=%d other=%d, v0_ts=%u, v1_ts=%u, a_ts=%u\r\n",
//                      loop_cnt, video_cnt, video_cnt1, audio_cnt, video_fb_other, v0_ts, v1_ts, a_ts);
//        }
        os_sleep_ms(5);
    }
}


struct os_task test_task_hdl;
void avstream_send_demo(void)
{
#ifdef __TXW826__
    /* 826 PSRAM 紧张, 用系统默认 sram 栈 (NULL) */
    OS_TASK_INIT("av_send", &test_task_hdl, av_send_thread, NULL, OS_TASK_PRIORITY_NORMAL, NULL, 4096);
#else
    /* 828 走 psram 栈节省 sram */
    void *stack_buf = _os_malloc_psram(4096);
    OS_TASK_INIT("av_send", &test_task_hdl, av_send_thread, NULL, OS_TASK_PRIORITY_NORMAL, stack_buf, 4096);
#endif
}

#if 0 //SNAPSHOT_USE_LEGACY
/*******************************************************************************
 * 轻量级抓拍模块 (老路径, 由 project_config.h 里 SNAPSHOT_USE_LEGACY 控制是否编译)
 * 复用 AUTO_JPG msi，不额外启动缩略图等 MSI 链路，节省内存
 * 完成后调用 snapshot_release() 释放
 * 使用前需在 fpv_app_init() 的 JPG_EN 块之后调用 snapshot_init()
 ******************************************************************************/
#define SNAPSHOT_MSI_NAME   "snapshot_jpg"
#define SNAPSHOT_RECV_MAX   1

static struct msi      *snapshot_msi  = NULL;
static volatile uint8_t snapshot_busy = 0;

void snapshot_init(void)
{
    snapshot_msi = msi_new(SNAPSHOT_MSI_NAME, SNAPSHOT_RECV_MAX, NULL);
    if (snapshot_msi)
    {
        snapshot_msi->action = NULL;
        snapshot_msi->enable = 0;
    }
    msi_add_output(NULL, AUTO_JPG, SNAPSHOT_MSI_NAME);
    os_printf(KERN_INFO "snapshot: init done, msi=%p, source=AUTO_JPG(main-stream)\r\n", snapshot_msi);
}

/**
 * snapshot_capture - 抓拍一张 JPEG 图片(拷贝模式)
 * 内部申请独立 psram buffer 拷贝 JPEG 数据, 然后立刻归还 msi 的 fb,
 * 避免调用者长时间持有 msi 内部 fb 导致后续 JPEG 流水线阻塞。
 *
 * @out_data:    [out] JPEG 数据指针, 由内部 _os_malloc_psram 分配,
 *               调用者上传完成后通过 snapshot_release() 释放
 * @out_len:     [out] JPEG 数据长度(字节)
 * @timeout_ms:  超时时间, 建议 2000ms
 *
 * return: 0 成功, 负值失败
 */
int snapshot_capture(uint8_t **out_data, uint32_t *out_len, uint32_t timeout_ms)
{
    struct framebuff *fb;

    if (!snapshot_msi || !out_data || !out_len)
    {
        os_printf(KERN_ERR "snapshot: invalid param, msi=%p data=%p len=%p\r\n",
                  snapshot_msi, out_data, out_len);
        return -1;
    }

    if (snapshot_busy)
    {
        os_printf(KERN_ERR "snapshot: busy, capture in progress\r\n");
        return -2;
    }

    snapshot_busy = 1;
    *out_data = NULL;
    *out_len  = 0;

    // 清空残留帧
    while ((fb = msi_get_fb(snapshot_msi, 0)) != NULL)
        msi_delete_fb(NULL, fb);

    // 启用接收, AUTO_JPG 会自动启动 JPEG 编码硬件
    snapshot_msi->enable = 1;

    // 等待一帧 JPEG
    uint32_t start = os_jiffies();
    fb = NULL;
    while ((os_jiffies() - start) < timeout_ms)
    {
        fb = msi_get_fb(snapshot_msi, 0);
        if (fb && fb->mtype == F_JPG)
            break;
        if (fb)
        {
            msi_delete_fb(NULL, fb);
            fb = NULL;
        }
        os_sleep_ms(10);
    }

    // 关闭接收, 通知 auto_jpg 停止 JPEG 硬件
    snapshot_msi->enable = 0;

    // 清空 hold-over 的帧(在等待期间, 硬件可能又产出了 1-2 帧)
    struct framebuff *tmp;
    while ((tmp = msi_get_fb(snapshot_msi, 0)) != NULL)
    {
        if (!fb && tmp->mtype == F_JPG)
        {
            // 如果之前超时没拿到, 这里捡到一帧也算成功
            fb = tmp;
        }
        else
        {
            msi_delete_fb(NULL, tmp);
        }
    }

    int ret;
    if (fb && fb->mtype == F_JPG)
    {
        // 申请独立 psram buffer 拷贝数据, 不使用 msi 内部 fb 的 buffer
        #ifdef __TXW826__
        uint8_t *buf = (uint8_t *) os_malloc(fb->len);
        #else
        uint8_t *buf = (uint8_t *) os_malloc_psram(fb->len);
        #endif
        if (buf)
        {
            os_memcpy(buf, fb->data, fb->len);
            *out_data = buf;
            *out_len  = fb->len;
            os_printf(KERN_INFO "snapshot: ok, len=%d, addr=%p\r\n", fb->len, buf);
            ret = 0;
        }
        else
        {
            os_printf(KERN_ERR "snapshot: _os_malloc_psram(%d) failed\r\n", fb->len);
            ret = -4;
        }
        // 立刻归还 fb, 让 JPEG 流水线可以继续运转, 避免占住 jpg_concat_buf
        msi_delete_fb(NULL, fb);
    }
    else
    {
        os_printf(KERN_ERR "snapshot: timeout %dms, no jpg frame\r\n", timeout_ms);
        ret = -3;
    }

    snapshot_busy = 0;
    return ret;
}

/**
 * snapshot_release - 释放抓拍的图片 psram 缓冲区
 * 上传完成后(on_status 回调中)必须调用此函数
 * @jpg_data: snapshot_capture 返回的 out_data
 */
void snapshot_release(void *jpg_data)
{
    if (jpg_data)
    {
        os_printf(KERN_INFO "snapshot_release: free jpg addr=%p\r\n", jpg_data);

#ifdef __TXW826__
        os_free(jpg_data);
#else
        os_free_psram(jpg_data);
#endif
    }
}
#endif  /* SNAPSHOT_USE_LEGACY */
