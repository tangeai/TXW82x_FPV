/*******************************************************************************
 * 裸调 JPG 抓拍，输出单张 JPEG，不启动常驻 MSI 编码链路。
 * 双目使用 VPP_DATA0 的完整纵向拼接帧（当前 1280x1440）。
 * 参照原厂 jpg_v3_msi：在第二颗摄像头 VPP done 边界启动，
 * 从下一组摄像头 0/1 的连续数据编码，避免从半帧或摄像头 1 开始。
 *
 * snapshot_init() 分配常驻输出节点；snapshot_capture() 获取 JPG 锁，
 * 等待帧边界和编码完成，再按硬件双缓冲顺序组包。
 * 完成、错误或超时均撤销启动回调并关闭硬件；输出由 snapshot_release() 释放。
 ******************************************************************************/

#include "basic_include.h"
#include "project_config.h"
#include "osal/string.h"
#include "osal/sleep.h"
#include "osal/irq.h"
#include "osal/time.h"
#include "lib/video/vpp/vpp_dev.h"
#include "lib/heap/av_psram_heap.h"
#include "dev.h"
#include "devid.h"
#include "hal/jpeg.h"
#include "hal/isp.h"
#include "lib/video/dvp/jpeg/jpg.h"
#include "lib/video/dvp/jpeg/jpg_common.h"
#include "stream_define.h"

/* 双目图像尺寸必须与 VPP 返回的完整拼接尺寸一致，不能只把 JPEG 头改高。 */
#define SNAP_JPG_ID        JPGID0

#if defined(SYS_DOUBLE_SENSOR_SPICE_DEMO)
#define SNAP_SRC_FROM      VPP_DATA0
#define SNAP_IMG_W         1280
#define SNAP_IMG_H         1440
#define SNAP_NODE_COUNT    20
#define SNAP_NODE_LEN      (10 * 1024)   /* 200KB 节点池；双缓冲需保留一个备用节点 */
#elif defined(__TXW826__)
#define SNAP_SRC_FROM      VPP_DATA0
#define SNAP_IMG_W         1280
#define SNAP_IMG_H         720
#define SNAP_NODE_COUNT    5
#define SNAP_NODE_LEN      (10 * 1024)
#else
#define SNAP_SRC_FROM      VPP_DATA1
#define SNAP_IMG_W         640
#define SNAP_IMG_H         360
#define SNAP_NODE_COUNT    5
#define SNAP_NODE_LEN      (5 * 1024)
#endif

#define SNAP_JPG_LOCK      JPG_LOCK_ENCODE
/* 量化表及质量：保留现有画质；编码异常后最多重试两次。 */
#if defined(__TXW826__)
#define SNAP_DQT_TAB_IDX    8
#define SNAP_QT             0xF
#else
#define SNAP_DQT_TAB_IDX    DQT_DEF
#define SNAP_QT             0xC
#endif
#define SNAP_MAX_RETRY      2
#define SNAP_RETRY_DELAY_MS 100
#define SNAP_MALLOC         os_malloc_psram
#define SNAP_FREE           os_free_psram

/* ==== 事件位 ==== */
#define EV_DONE            BIT(0)
#define EV_ERROR           BIT(1)

struct snap_ctx {
    struct jpg_device   *jpg;
    uint8_t             *nodes[SNAP_NODE_COUNT];
    uint8_t              node_done_list[SNAP_NODE_COUNT];
    volatile uint8_t     node_done_count;
    volatile uint8_t     current_node;
    volatile uint8_t     next_node;
    volatile uint8_t     allocated_nodes;
    volatile uint8_t     hw_opened;
    volatile uint8_t     finished;
    volatile uint32_t    frame_total_len;
    uint16_t             img_w;
    uint16_t             img_h;
    struct os_event      evt;
};

static volatile uint8_t g_snap_busy = 0;
static uint8_t *g_snap_nodes[SNAP_NODE_COUNT] = {NULL};
static volatile uint8_t g_snap_nodes_ready = 0;

/* IRQ 和任务退出路径都可以调用；先禁止再次启动，再停止 DMA。 */
static void snap_stop(struct snap_ctx *ctx)
{
    ctx->finished = 1;
    if (ctx->hw_opened) {
        jpg_close(ctx->jpg);
        ctx->hw_opened = 0;
    }
}

static void snap_complete(struct snap_ctx *ctx, uint32_t event)
{
    if (ctx->finished) return;
    snap_stop(ctx);
    os_event_set(&ctx->evt, event, NULL);
}

/* 45229 OUTBUF_FULL 的 param1 不是节点地址。
 * 初始 current=0、next=1；每次 FULL 将 current 收入链表，
 * next 成为 current，再配置一个新的备用节点，顺序与 jpg_v3_msi 一致。 */
static int32 snap_buf_full_isr(uint32 irq_flag, uint32 irq_data, uint32 param1, uint32 param2)
{
    struct snap_ctx *ctx = (struct snap_ctx *)irq_data;
    if (ctx->finished) return 0;
    if (ctx->node_done_count >= SNAP_NODE_COUNT ||
        ctx->allocated_nodes >= SNAP_NODE_COUNT) {
        os_printf(KERN_ERR "snapshot: node pool exhausted (%u/%u)\n",
                  ctx->node_done_count, SNAP_NODE_COUNT);
        snap_complete(ctx, EV_ERROR);
        return 0;
    }
    ctx->node_done_list[ctx->node_done_count++] = ctx->current_node;
    ctx->current_node = ctx->next_node;
    ctx->next_node = ctx->allocated_nodes++;
    jpg_set_addr(ctx->jpg, (uint32)ctx->nodes[ctx->next_node], SNAP_NODE_LEN);
    return 0;
}

static int32 snap_done_isr(uint32 irq_flag, uint32 irq_data, uint32 param1, uint32 param2)
{
    struct snap_ctx *ctx = (struct snap_ctx *)irq_data;
    if (ctx->finished) return 0;
    uint32_t full_bytes = (uint32_t)ctx->node_done_count * SNAP_NODE_LEN;
    /* param2 非零表示驱动检测到节点中断计数不一致，不返回残缺图像。 */
    if (param2 || !param1 || param1 < full_bytes ||
        param1 - full_bytes > SNAP_NODE_LEN || ctx->node_done_count >= SNAP_NODE_COUNT) {
        snap_complete(ctx, EV_ERROR);
        return 0;
    }
    /* 只收实际写入的尾节点；next 是尚未使用的备用节点。 */
    if (param1 > full_bytes)
        ctx->node_done_list[ctx->node_done_count++] = ctx->current_node;
    ctx->frame_total_len = param1;
    snap_complete(ctx, EV_DONE);
    return 0;
}

static int32 snap_err_isr(uint32 irq_flag, uint32 irq_data, uint32 param1, uint32 param2)
{
    struct snap_ctx *ctx = (struct snap_ctx *)irq_data;
    if (ctx->finished) return 0;
    os_printf(KERN_ERR "snapshot: JPG error flag=0x%x p1=0x%x p2=0x%x\n",
              irq_flag, param1, param2);
    snap_complete(ctx, EV_ERROR);
    return 0;
}

#if defined(SYS_DOUBLE_SENSOR_SPICE_DEMO)
/* 返回 0 继续等下一颗镜头，返回 1 由 VPP 自动撤销这次回调。 */
static int32_t snap_vpp_start(uint32_t arg)
{
    struct snap_ctx *ctx = (struct snap_ctx *)arg;
    if (ctx->finished) return 1;
    if (video_msg.camera_mode != CAM_DUAL_SPLICE_SLAVE_MODE || video_msg.video_num != 2) {
        snap_complete(ctx, EV_ERROR);
        return 1;
    }
    if (video_msg.video_type_cur != ISP_VIDEO_1) return 0;
    jpg_set_size(ctx->jpg, ctx->img_h, ctx->img_w);
    jpg_set_ready(ctx->jpg);
    ctx->hw_opened = 1;
    if (jpg_open(ctx->jpg) != RET_OK) snap_complete(ctx, EV_ERROR);
    return 1;
}
#endif

/* =========================================================================
 * 对外接口 (和老 snapshot_capture/snapshot_release 签名一致)
 * ========================================================================= */

/**
 * snapshot_init - 一次性申请常驻 node 池
 * 需要在 fpv_app_init 的 JPG_EN 块里调用.
 * 常驻占用 SNAP_NODE_COUNT * SNAP_NODE_LEN PSRAM，多次抓拍复用。
 * 反复调用幂等.
 */
void snapshot_init(void)
{
    if (g_snap_nodes_ready) return;
    for (int i = 0; i < SNAP_NODE_COUNT; i++) {
        g_snap_nodes[i] = (uint8_t *) os_malloc_psram(SNAP_NODE_LEN);
        if (!g_snap_nodes[i]) {
            os_printf(KERN_ERR "snapshot_init: SNAP_MALLOC(%d) failed at %d\n",
                      SNAP_NODE_LEN, i);
            for (int j = 0; j < i; j++) {
                SNAP_FREE(g_snap_nodes[j]);
                g_snap_nodes[j] = NULL;
            }
            return;
        }
        sys_dcache_invalid_range((uint32_t *) g_snap_nodes[i], SNAP_NODE_LEN);
    }
    g_snap_nodes_ready = 1;
    os_printf(KERN_INFO "snapshot_init: %d x %d bytes (total %d bytes, resident)\n",
              SNAP_NODE_COUNT, SNAP_NODE_LEN, SNAP_NODE_COUNT * SNAP_NODE_LEN);
}

/* 单次抓拍 (不含重试/busy 锁, 由外层 snapshot_capture 负责).
 * 返回 0 成功; 负值失败:
 *   -3 = ctx malloc 或事件初始化失败
 *   -4 = JPG 设备句柄获取失败 (硬件未注册)
 *   -6 = jpg_mutex_lock 超时 500ms (其他线程持锁未释放)
 *   -8 = EV_DONE 未等到 (硬件 timeout 或 snap_err_isr 触发 EV_ERROR)
 *   -9 = 帧长度、节点计数或 JPEG 文件头异常
 *   -10 = 输出 buf malloc 失败
 *   -11 = snapshot_init 未调用 (node 池未就绪)
 *   -12 = 双目 VPP 未就绪或拼接尺寸不匹配 */
static int _snapshot_capture_once(uint8_t **out_data, uint32_t *out_len, uint32_t timeout_ms)
{
    *out_data = NULL;
    *out_len  = 0;

    int ret = -1;
    uint8_t have_lock = 0;
    uint8_t vpp_registered = 0;
    uint8_t event_ready = 0;
    uint8_t irq_registered = 0;

    struct snap_ctx *ctx = (struct snap_ctx *) SNAP_MALLOC(sizeof(*ctx));
    if (!ctx) {
        os_printf(KERN_ERR "snapshot: ctx malloc(%u) failed\n", (unsigned)sizeof(*ctx));
        ret = -3; goto out;
    }
    os_memset(ctx, 0, sizeof(*ctx));
    if (os_event_init(&ctx->evt) != RET_OK) { ret = -3; goto out; }
    event_ready = 1;

    /* 1. 拿 JPG 硬件句柄 */
    ctx->jpg = (struct jpg_device *) dev_get(HG_JPG0_DEVID + SNAP_JPG_ID);
    if (!ctx->jpg) {
        os_printf(KERN_ERR "snapshot: dev_get JPG%d failed (HG_JPG0_DEVID+%d)\n",
                  SNAP_JPG_ID, SNAP_JPG_ID);
        ret = -4; goto out;
    }

    ctx->img_w = SNAP_IMG_W;
    ctx->img_h = SNAP_IMG_H;
#if defined(SYS_DOUBLE_SENSOR_SPICE_DEMO)
    /* VPP 会在双目拼接模式返回两颗镜头的总高度。 */
    if (get_vpp_w_h(&ctx->img_w, &ctx->img_h) ||
        video_msg.camera_mode != CAM_DUAL_SPLICE_SLAVE_MODE || video_msg.video_num != 2 ||
        ctx->img_w != SNAP_IMG_W || ctx->img_h != SNAP_IMG_H) {
        os_printf(KERN_ERR "snapshot: dual VPP not ready, mode=%u cameras=%u size=%ux%u\n",
                  video_msg.camera_mode, video_msg.video_num, ctx->img_w, ctx->img_h);
        ret = -12; goto out;
    }
#endif

    /* 2. 引用常驻 node 池, 重置本次抓拍的状态 */
    for (int i = 0; i < SNAP_NODE_COUNT; i++) {
        ctx->nodes[i] = g_snap_nodes[i];
        sys_dcache_invalid_range((uint32_t *) ctx->nodes[i], SNAP_NODE_LEN);
    }

    /* 3. 抢 JPG 互斥锁 (最多等 500ms) */
    uint8_t last_value;
    uint64_t t_start = os_jiffies();
    uint32_t lock_retry_cnt = 0;
    while (jpg_mutex_lock(SNAP_JPG_ID, SNAP_JPG_LOCK, &last_value) != 0) {
        lock_retry_cnt++;
        if (os_jiffies_to_msecs(os_jiffies() - t_start) > 500) {
            /* 等待其他 JPG 使用者释放硬件。 */
            os_printf(KERN_ERR "snapshot: jpg_mutex_lock(id=%d,lock=%d) timeout 500ms, last_value=%u, tried=%u\n",
                      SNAP_JPG_ID, SNAP_JPG_LOCK, (unsigned)last_value, (unsigned)lock_retry_cnt);
            ret = -6; goto out;
        }
        os_sleep_ms(5);
    }
    have_lock = 1;

    /* 4. 硬件配置 */
    jpg_init(ctx->jpg, SNAP_DQT_TAB_IDX, SNAP_QT);
    jpg_set_qt(ctx->jpg, SNAP_QT);
    jpg_updata_dqt(ctx->jpg, (uint32 *) quality_tab[SNAP_DQT_TAB_IDX]);
    jpg_set_size(ctx->jpg, ctx->img_h, ctx->img_w);
    jpg_set_data_from(ctx->jpg, SNAP_SRC_FROM);
    jpg_set_hw_check(ctx->jpg, 1);

    /* 5. 注册 3 个 ISR */
    jpg_request_irq(ctx->jpg, snap_buf_full_isr, JPG_IRQ_FLAG_JPG_BUF_FULL, ctx);
    jpg_request_irq(ctx->jpg, snap_err_isr,      JPG_IRQ_FLAG_ERROR,        ctx);
    jpg_request_irq(ctx->jpg, snap_done_isr,     JPG_IRQ_FLAG_JPG_DONE,     ctx);
    irq_registered = 1;

    jpg_set_vsync_dly(ctx->jpg, 1);
    jpg_select_oe_using(ctx->jpg, 0, 1);

    /* 6. 初始两个节点对应硬件 current/next，FULL 中断后按顺序追加。 */
    ctx->current_node = 0;
    ctx->next_node = 1;
    ctx->allocated_nodes = 2;
    jpg_set_addr(ctx->jpg, (uint32)ctx->nodes[0], SNAP_NODE_LEN);
    jpg_set_addr(ctx->jpg, (uint32)ctx->nodes[1], SNAP_NODE_LEN);

    /* 7. 双目必须在整组帧边界启动，避免上半图来自摄像头 1。 */
#if defined(SYS_DOUBLE_SENSOR_SPICE_DEMO)
    vpp_registered = 1;
    vppdone_func_register(VPP_JPEG0_START + SNAP_JPG_ID, snap_vpp_start, (uint32_t)ctx);
#else
    jpg_set_ready(ctx->jpg);
    ctx->hw_opened = 1;
    if (jpg_open(ctx->jpg) != RET_OK) { ret = -8; goto out; }
#endif

    uint32_t rflags = 0;
    os_event_wait(&ctx->evt, EV_DONE | EV_ERROR, &rflags,
                  OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, timeout_ms);

    if ((rflags & EV_ERROR) || !(rflags & EV_DONE)) {
        /* 帧边界等待、编码或缓冲异常均进入统一清理路径。 */
        const char *cause = "unknown";
        if (rflags == 0)              cause = "frame boundary or encoding timeout";
        else if (rflags & EV_ERROR)   cause = "camera, JPG or output buffer error";
        os_printf(KERN_ERR "snapshot: wait done failed, rflags=0x%x timeout_ms=%u (%s) done_cnt=%u\n",
                  (unsigned)rflags, (unsigned)timeout_ms, cause,
                  (unsigned)ctx->node_done_count);
        ret = -8;
        goto out;
    }
    if (!ctx->frame_total_len || ctx->node_done_count == 0 ||
        ctx->frame_total_len > (uint32_t)ctx->node_done_count * SNAP_NODE_LEN) {
        /* DONE 的有效帧长必须落在实际写入的节点内。 */
        os_printf(KERN_ERR "snapshot: invalid frame len=%u done_cnt=%u (DONE but no data)\n",
                  ctx->frame_total_len, ctx->node_done_count);
        ret = -9;
        goto out;
    }

    /* 8. 拷贝 node 链 -> 输出 buf */
    uint8_t *out_buf = (uint8_t *) SNAP_MALLOC(ctx->frame_total_len);
    if (!out_buf) {
        /* 输出帧额外占用 PSRAM，由调用者通过 snapshot_release 释放。 */
        os_printf(KERN_ERR "snapshot: out_buf malloc(%u) failed\n",
                  (unsigned)ctx->frame_total_len);
        ret = -10; goto out;
    }

    uint32_t offset = 0;
    uint32_t remain = ctx->frame_total_len;
    for (uint8_t k = 0; k < ctx->node_done_count && remain > 0; k++) {
        uint8_t idx = ctx->node_done_list[k];
        uint32_t cp = (remain > SNAP_NODE_LEN) ? SNAP_NODE_LEN : remain;
        sys_dcache_invalid_range((uint32_t *) ctx->nodes[idx], cp);
        os_memcpy(out_buf + offset, ctx->nodes[idx], cp);
        offset += cp;
        remain -= cp;
    }

    if (remain != 0 || offset < 2 || out_buf[0] != 0xff || out_buf[1] != 0xd8) {
        SNAP_FREE(out_buf);
        ret = -9; goto out;
    }
    *out_data = out_buf;
    *out_len  = ctx->frame_total_len;
    ret = 0;
    os_printf(KERN_INFO "snapshot: ok, %ux%u len=%u addr=%p\n",
              ctx->img_w, ctx->img_h, ctx->frame_total_len, out_buf);

out:
    /* 清理 */
    if (ctx) {
        /* 先撤销回调，再释放 ctx；超时后也不能在下一帧重新启动 JPG。 */
        ctx->finished = 1;
        if (vpp_registered)
            vppdone_func_unregister(VPP_JPEG0_START + SNAP_JPG_ID);
        snap_stop(ctx);
    }
    if (irq_registered) {
        jpg_release_irq(ctx->jpg, JPG_IRQ_FLAG_JPG_BUF_FULL);
        jpg_release_irq(ctx->jpg, JPG_IRQ_FLAG_ERROR);
        jpg_release_irq(ctx->jpg, JPG_IRQ_FLAG_JPG_DONE);
    }
    if (have_lock) {
        jpg_mutex_unlock(SNAP_JPG_ID, SNAP_JPG_LOCK);
    }
    if (ctx) {
        /* node 是常驻池, 不在这里释放 */
        if (event_ready) os_event_del(&ctx->evt);
        SNAP_FREE(ctx);
    }
    return ret;
}

/* 对外 API: 带 busy 锁和自动重试的抓拍封装.
 * 826/828 当前都设 SNAP_MAX_RETRY=2 + delay 100ms (应对 VPP/JPG 并发硬件冲突).
 * 错误码语义见 _snapshot_capture_once 头部. */
int snapshot_capture(uint8_t **out_data, uint32_t *out_len, uint32_t timeout_ms)
{
    if (!out_data || !out_len) {
        os_printf(KERN_ERR "snapshot_capture: invalid args out_data=%p out_len=%p\n",
                  out_data, out_len);
        return -1;
    }
    *out_data = NULL;
    *out_len = 0;
    if (!g_snap_nodes_ready) {
        os_printf(KERN_ERR "snapshot: not initialized, call snapshot_init() first\n");
        return -11;
    }
    uint32_t flags = disable_irq();
    if (g_snap_busy) {
        enable_irq(flags);
        /* 抓拍并发: 上一次还没结束, 拒绝新调用. 调用者应等当前完成后再触发. */
        os_printf(KERN_WARNING "snapshot_capture: busy, previous capture not finished\n");
        return -2;
    }
    g_snap_busy = 1;
    enable_irq(flags);

    int ret = -1;
    uint8_t attempt = 0;
    for (attempt = 0; attempt <= SNAP_MAX_RETRY; attempt++) {
        if (attempt > 0) {
            /* 短暂退避；双目重试仍会重新等待完整帧边界。 */
            os_printf(KERN_WARNING "snapshot: prev_ret=%d, retry %d/%d after %dms\n",
                      ret, attempt, SNAP_MAX_RETRY, SNAP_RETRY_DELAY_MS);
            os_sleep_ms(SNAP_RETRY_DELAY_MS);
        }
        ret = _snapshot_capture_once(out_data, out_len, timeout_ms);
        if (ret == 0) break;        /* 成功 */
    }

    if (ret != 0) {
        /* 所有重试都失败, 把最终错误码打出来便于回查 */
        os_printf(KERN_ERR "snapshot_capture: FAILED after %d attempt(s), final ret=%d\n",
                  attempt, ret);
    } else if (attempt > 0) {
        /* 重试成功: 记录用了第几次才成功, 便于评估 retry 配置是否合适 */
        os_printf(KERN_WARNING "snapshot_capture: ok after %d retry\n", attempt);
    }

    flags = disable_irq();
    g_snap_busy = 0;
    enable_irq(flags);
    return ret;
}

void snapshot_release(void *jpg_data)
{
    if (jpg_data) {
        os_printf(KERN_INFO "snapshot_release: free jpg addr=%p\r\n", jpg_data);
        SNAP_FREE(jpg_data);
    }
}

