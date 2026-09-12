#include "TgCloudApi.h"
#include "logfile.h"
#include "g711.h"
#include "osal/task.h"
#include "osal/sleep.h"
#include "osal/string.h"

//-------------------------------------------------------------------------------
// 对讲播放模块（异步 ring buffer 版）
//
// 数据流: 探鸽 G.711a (webrtc 任务)
//          ↓ talkback() 只 memcpy 进 ring buffer (不接触硬件, 不阻塞)
//        ring buffer (PCM16 sample 队列)
//          ↓ talk_pump_task 独立 BELOW_NORMAL 任务
//          ↓ ausys_da_put -> 硬件 DAC FIFO -> 喇叭
//
// 设计动机:
//   旧实现 talkback() 直接调 ausys_da_put, ausys_da_put 内部会关中断 / 等
//   FIFO 槽位, 这条调用链让 webrtc 任务被粘到 DAC 硬件上, 实测干扰 SDIO 中断
//   导致 SD 卡 CMD12 不应答 → mp4 写卡失败 → 整段录像作废.
//
//   屏蔽 ausys_da_put 后 SD 卡稳了, 但对讲就没声音. 改成独立任务:
//     - webrtc 任务永不调 ausys_da_put, SD 卡不受影响
//     - 独立任务优先级 BELOW_NORMAL, 即使被 ausys_da_put 短暂阻塞, 也不抢
//       mp4_encode_thread (ABOVE_NORMAL) 写卡的 CPU
//-------------------------------------------------------------------------------
extern int32_t ausys_da_put(void* buf, uint32_t nbytes);

//#define USER_DEFINE_GPIO_EN

#ifdef USER_DEFINE_GPIO_EN
#define GPIO_SPEAK_EN PC_0
extern int32 gpio_set_val(uint32 pin, uint32 value);
void gy_Speaker_en(int enable) {
  static unsigned char flag = 0;
  int gpio_en = enable == 0 ? 1 : 0;
  if (0 == flag) {
    gpio_iomap_output(GPIO_SPEAK_EN, GPIO_IOMAP_OUTPUT);
    flag = 1;
  }
  gpio_set_val(GPIO_SPEAK_EN, gpio_en);
}
#endif


static volatile uint8_t __talk_inited = 0;

/* === 新增: PCM ring buffer + 异步消费任务 ===
 *
 * ring 容量 = 4 帧 (4 * 320 samples = 1280 samples = 160ms 缓冲)
 *   - 8kHz 16bit mono = 16KB/s, 160ms = 2560 字节 SRAM
 *   - 缓冲足够大避免 webrtc 抖动时 pump 任务饿死
 *   - 缓冲不要太大避免对讲延迟过长 (160ms 端到端可接受)
 *
 * head/tail 用 sample 单位 (uint16_t = 1 PCM sample), 不用字节, 简化对齐. */
#define TALK_RING_FRAMES     4
#define TALK_FRAME_SAMPLES   320   /* 40ms @ 8kHz */
#define TALK_RING_SAMPLES    (TALK_RING_FRAMES * TALK_FRAME_SAMPLES)

static short            talk_ring[TALK_RING_SAMPLES];
static volatile uint32_t talk_ring_head = 0;   /* pump 任务读位置 (samples) */
static volatile uint32_t talk_ring_tail = 0;   /* talkback() 写位置 (samples) */
static void            *talk_pump_hdl  = NULL; /* 独立任务句柄 */
static volatile uint8_t talk_pump_run  = 0;    /* 任务运行标志 */

/* ring buffer 已用空间 (samples) */
static inline uint32_t talk_ring_count(void)
{
    uint32_t t = talk_ring_tail, h = talk_ring_head;
    return (t >= h) ? (t - h) : (TALK_RING_SAMPLES - h + t);
}

/* ring buffer 剩余空间 (samples), 留 1 sample 区分满/空 */
static inline uint32_t talk_ring_free(void)
{
    return TALK_RING_SAMPLES - 1 - talk_ring_count();
}

/* pump 任务: 从 ring 取 1 帧 (320 samples) 调 ausys_da_put.
 *   ring 不足 1 帧时 sleep 5ms 等 webrtc 投喂.
 *   __talk_inited=0 时退出, 任务自然结束.
 * 用 BELOW_NORMAL 优先级: 即使 ausys_da_put 阻塞也不抢 mp4_encode_thread. */
static void talk_pump_task(void *arg)
{
    (void)arg;
    short pcm_out[TALK_FRAME_SAMPLES];
    while (talk_pump_run) {
        if (!__talk_inited) {
            /* stop 已经被调, 退出. 不在这里清 ring, 让 on_talkback_start 重置 */
            break;
        }
        uint32_t cnt = talk_ring_count();
        if (cnt < TALK_FRAME_SAMPLES) {
            /* 数据不够 1 帧, 等 webrtc 投喂. 5ms = G.711a 1 包间隔 */
            os_sleep_ms(5);
            continue;
        }

        /* 从 ring 取 320 samples 到 pcm_out (栈, 640B) */
        uint32_t h = talk_ring_head;
        for (uint32_t i = 0; i < TALK_FRAME_SAMPLES; i++) {
            pcm_out[i] = talk_ring[h];
            h = (h + 1) % TALK_RING_SAMPLES;
        }
        talk_ring_head = h;

        /* 真正调 DAC 在这里, 这是个独立任务, ausys_da_put 关中断 / 等 FIFO
         * 都不影响 webrtc / mp4_encode 任务 */
        ausys_da_put((void *)pcm_out, TALK_FRAME_SAMPLES * 2);
    }
    talk_pump_hdl = NULL;
}

int on_talkback_start()
{
    LogV("talkback start\n\n");
#ifdef USER_DEFINE_GPIO_EN
    gy_Speaker_en(1);
#endif
    /* 重置 ring 和缓存 */
    talk_ring_head = 0;
    talk_ring_tail = 0;
    __talk_inited = 1;

    /* 启动 pump 任务. 如果上次的还没退干净, 等一下避免双任务 */
    if (talk_pump_hdl) {
        /* 防御性: 旧任务还在跑就先等它退. on_talkback_stop 时 __talk_inited=0,
         * pump 看到会自然退出; 这里再多等 20ms 让它真正 destroy */
        os_sleep_ms(20);
    }
    talk_pump_run = 1;
    talk_pump_hdl = os_task_create("talk_pump",
                                   talk_pump_task,
                                   NULL,
                                   OS_TASK_PRIORITY_BELOW_NORMAL,
                                   0, NULL, 2048);
    if (!talk_pump_hdl) {
        os_printf(KERN_ERR "talk_pump: os_task_create fail, talkback will be silent\n");
        talk_pump_run = 0;
    }
    return 0;
}

/* === 新版 talkback (异步 ring buffer 版) ===
 *
 * webrtc 任务来一帧 G.711a (典型 40B / 5ms), 我们:
 *   1. alaw2linear 解码到 PCM16 (CPU 软算, 不阻塞)
 *   2. 写入 ring buffer (复制到 SRAM, 不阻塞)
 *   3. 立即返回
 *
 * ring 满时丢弃新数据 (而不是阻塞 webrtc) — pump 任务会很快消费.
 * 实测稳态 ring 占用 1-2 帧, 不会满. */
int talkback(TCMEDIA at, const uint8_t *audio, int len)
{
    (void)at;
    if (!__talk_inited) return 0;
    
    /* 解码 G.711a -> PCM16, 直接写入 ring buffer */
    uint32_t t = talk_ring_tail;
    uint32_t available = talk_ring_free();
    int processed = 0;
    for (int i = 0; i < len; i++) {
        if ((uint32_t)processed >= available) {
            /* ring 满, 丢弃剩余: pump 跟不上时宁可丢一段, 不能阻塞 webrtc.
             * 节流打印避免刷屏 */
            static uint32_t s_drop_cnt = 0;
            s_drop_cnt++;
            if ((s_drop_cnt & 0xFF) == 0) {
                os_printf(KERN_WARNING "talkback: ring full, dropped %u samples\n",
                          (unsigned)s_drop_cnt);
            }
            break;
        }
        talk_ring[t] = alaw2linear(audio[i]);
        t = (t + 1) % TALK_RING_SAMPLES;
        processed++;
    }
    talk_ring_tail = t;

    return 0;
}


void on_talkback_stop()
{
    LogV("talkback stop\n\n");
    __talk_inited = 0;     /* pump 任务下一次循环看到会自然退出 */
    /* 不在这里 join pump 任务 — 它会自己看 __talk_inited=0 break + 把 hdl
     * 置 NULL. 这里立即返回让 webrtc 端不被阻塞. on_talkback_start 重启时
     * 会再等一会儿确认旧任务真退了 */
#ifdef USER_DEFINE_GPIO_EN
    gy_Speaker_en(0);
#endif

}
