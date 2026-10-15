#include "basic_include.h"
#include "lib/heap/av_heap.h"
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

/* TXW826、TXW828 单目和 TXW828 双目子码流 H264 来源不同:
 *   826: VPP → gen420 硬件生成 YUV420 → H264, stype=FSTYPE_H264_GEN420_DATA
 *   828 双目拼接: VPP → gen420 → H264, stype=FSTYPE_H264_GEN420_DATA
 *   828 单目: VPP → VPP_DATA1 直出 YUV → H264, stype=FSTYPE_H264_VPP_DATA1
 * 下面的 action 白名单和主循环分流都按 chip 区分. */
#if defined(SYS_DOUBLE_SENSOR_SPICE_DEMO) || defined(__TXW826__)
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
    auadc_msi_add_output(MAIN_MIC_ID, REV_TEST_AUDIO);
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
            if(audio_fb->mtype == F_AUDIO && audio_fb->data &&
               audio_fb->len >= AUDIO_PCM_SAMPLES_PER_FRAME * sizeof(short))
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
    if (!stack_buf) {
        os_printf("av_send: stack allocation failed\n");
        return;
    }
    OS_TASK_INIT("av_send", &test_task_hdl, av_send_thread, NULL, OS_TASK_PRIORITY_NORMAL, stack_buf, 4096);
#endif
}

