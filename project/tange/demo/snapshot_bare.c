/*******************************************************************************
 * 裸写 JPG 抓拍 - 不依赖 msi / auto_jpg / jpg_concat 框架
 *
 * 由 project_config.h 的 SNAPSHOT_USE_LEGACY=0 启用本实现.
 * 对外接口 snapshot_capture() / snapshot_release() 和老实现完全一致, 可无缝替换.
 *
 * 初始化(snapshot_init):
 *   - 一次性 os_malloc_psram 申请 SNAP_NODE_COUNT 个 node 作硬件输出缓冲
 *   - 常驻不释放, 多次抓拍复用, 避免反复 malloc/free 产生碎片
 *
 * 每次抓拍(snapshot_capture):
 *   1. 抢 JPG 硬件互斥锁 (和 H264/解码/其他使用者协调)
 *   2. 配置硬件 (量化表/尺寸/数据源 VPP_DATA1=子码流)
 *   3. 注册 3 个中断 ISR (BUF_FULL / DONE / ERROR)
 *   4. jpg_open 启动硬件, 等 DONE 事件
 *   5. 把 node 链里的数据 memcpy 到用户 PSRAM buf 返回
 *   6. 释放 IRQ + jpg_close + 解锁
 *
 * 特点:
 *   - 常驻占用 = SNAP_NODE_COUNT * SNAP_NODE_LEN (node 池)
 *   - 抓拍时额外 ~sizeof(snap_ctx) + 实际帧长度 的 PSRAM 临时占用
 *   - 不受 SDK msi 调度影响, 抓拍延迟稳定
 ******************************************************************************/

#include "basic_include.h"
#include "project_config.h"

#if !SNAPSHOT_USE_LEGACY

#include "osal/string.h"
#include "osal/sleep.h"
#include "lib/heap/av_psram_heap.h"
#include "dev.h"
#include "devid.h"
#include "hal/jpeg.h"
#include "lib/video/dvp/jpeg/jpg.h"
#include "lib/video/dvp/jpeg/jpg_common.h"
#include "stream_define.h"

/* ==== 可调参数 ====
 *
 * 当前所有 chip 都统一走"JPG 硬件直抓 VPP 通道"的裸调路径.
 * TXW826: 主码流 720P (1280x720), VPP_DATA0 直抓
 * TXW828: 子码流 640x360, VPP_DATA1 直抓
 */
#define SNAP_JPG_ID        JPGID0        /* 用哪个 JPG 硬件 */

#if defined(__TXW826__)
#define SNAP_SRC_FROM      VPP_DATA0     /* 主码流直接在 VPP_DATA0 通道 */
#define SNAP_IMG_W         1280          /* 主码流宽 */
#define SNAP_IMG_H         720           /* 主码流高 */
#define SNAP_NODE_COUNT    5             /* node 数量 */
#define SNAP_NODE_LEN      (10 * 1024)
#else
/* TXW828 / TXW827: 子码流 640x360 在 VPP_DATA1 */
#define SNAP_SRC_FROM      VPP_DATA1     /* 输入源: VPP_DATA0=主码流, VPP_DATA1=子码流 */
#define SNAP_IMG_W         640           /* 子码流宽 */
#define SNAP_IMG_H         360           /* 子码流高 */
#define SNAP_NODE_COUNT    5             /* node 数量 */
#define SNAP_NODE_LEN      (5 * 1024)    /* 单 node 长度, 5*5KB=25KB 覆盖 640x360 JPEG 典型帧长 */
#endif

#define SNAP_JPG_LOCK      JPG_LOCK_ENCODE
/* DQT 量化表索引 (quality_tab, 0~11 共 12 张, idx 越大压缩越狠帧越小):
 *   826: 720P JPEG 在复杂场景 (高纹理/高亮) 下超过 50KB node 池触发 err.
 *        SNAP_QT 已到硬件上限 0xF 不能再降, 改用更粗的量化表 8 让帧降到
 *        原 55% 的 ~38%, 稳妥落入 50KB. 画质下降明显但满足"抓拍存证"用途.
 *   828: 有 PSRAM node 池 (5*5KB=25KB 已够), 保持 SDK 默认 5. */
#if defined(__TXW826__)
#define SNAP_DQT_TAB_IDX   8
#else
#define SNAP_DQT_TAB_IDX   DQT_DEF
#endif

/* JPG 质量 / 重试策略 (按 chip 分):
 *   826: VPP_DATA0 被主码流H264 + 子码流gen420 + 抓拍JPG 三路并发读,
 *        ISP overflow 高概率触发 JPG 硬件 err (p1=0x8b08).
 *        策略: 降低质量让 JPG 帧更小更快完成 + 失败自动重试 2 次.
 *   828: VPP 通道分得开, 没有三路竞争, 保持高质量无需重试. */
/* jpg_set_qt 的参数 = DMA_CON 寄存器的 QT 字段, 只有 4 bits, 范围 0x0~0xF;
 * 值越大量化越粗, 质量越低, 帧越小. 硬件会截断 >0xF 的值, 不要超过. */
#if defined(__TXW826__)
#define SNAP_QT            0xF           /* 最粗量化, JPG 帧再降一档 (~17KB), 处理更快 */
#define SNAP_MAX_RETRY     2             /* 失败后重试 2 次, 每次失败 sleep 让 ISP 清 fifo */
#define SNAP_RETRY_DELAY_MS 100
#else
/* 828: 注释曾误判"VPP 通道分得开无三路竞争, 无需重试".
 * 实际并发: 子码流 H264 编码器持续读 VPP_DATA1 (SUB_STREAM_EN=1)
 *           + JPG 抓拍也读 VPP_DATA1
 * → 硬件冲突, snap_err_isr 触发 (41244 实测 p1=0x2f24, 与 826 的 0x8b08 不同).
 * 策略: 重试 2 次, 间隔 100ms 落在子码流 H264 帧间隙 (15fps≈66ms/帧). */
#define SNAP_QT            0xC           /* 标准质量 */
#define SNAP_MAX_RETRY     2
#define SNAP_RETRY_DELAY_MS 100
#endif

/* 内存分配策略按 chip 分:
 *   826: PSRAM 仅 4MB 非常紧张, 抓拍 buffer 挪到 SRAM (相对富余)
 *   828: PSRAM 宽裕, SRAM 紧张, buffer 留在 PSRAM 省 SRAM
 * 注意: SNAP_FREE 必须和 SNAP_MALLOC 同池, 跨池 free 会破坏堆元数据 */
//#if defined(__TXW826__)
//#define SNAP_MALLOC        os_malloc
//#define SNAP_FREE          os_free
//#else
#define SNAP_MALLOC        os_malloc_psram
#define SNAP_FREE          os_free_psram
//#endif

/* SDK 提供的量化表 (jpg.h extern) */
extern char quality_tab[6][128];

/* ==== 事件位 ==== */
#define EV_DONE            BIT(0)
#define EV_ERROR           BIT(1)

struct snap_ctx {
    struct jpg_device   *jpg;
    uint8_t             *nodes[SNAP_NODE_COUNT];      /* 指向常驻 node buf */
    volatile uint8_t     node_in_use[SNAP_NODE_COUNT];
    volatile uint8_t     node_done[SNAP_NODE_COUNT];
    uint8_t              node_done_list[SNAP_NODE_COUNT];
    volatile uint8_t     node_done_count;
    volatile uint32_t    frame_total_len;
    struct os_event      evt;
};

static volatile uint8_t g_snap_busy = 0;

/* ==== 常驻 node 池: snapshot_init 时分配, 永不释放, 多次抓拍复用 ==== */
static uint8_t         *g_snap_nodes[SNAP_NODE_COUNT] = {NULL};
static volatile uint8_t g_snap_nodes_ready = 0;

/* 选一个空闲 node, 标记 in_use, 返回索引 */
static int snap_get_free_node(struct snap_ctx *ctx)
{
    for (int i = 0; i < SNAP_NODE_COUNT; i++) {
        if (!ctx->node_in_use[i] && !ctx->node_done[i]) {
            ctx->node_in_use[i] = 1;
            return i;
        }
    }
    return -1;
}

/* 根据地址查 node 索引 */
static int snap_node_index_of(struct snap_ctx *ctx, uint32_t addr)
{
    for (int i = 0; i < SNAP_NODE_COUNT; i++) {
        if ((uint32_t) ctx->nodes[i] == addr) return i;
    }
    return -1;
}

/* ISR: 当前 node 写满, 给硬件配下一个 */
static int32 snap_buf_full_isr(uint32 irq_flag, uint32 irq_data, uint32 param1, uint32 param2)
{
    struct snap_ctx *ctx = (struct snap_ctx *) irq_data;

    int cur_idx = snap_node_index_of(ctx, param1);
    if (cur_idx < 0) {
        /* 硬件回报的 node 地址不在我们的池里, 说明驱动状态错乱.
         * 不算致命: 继续配下一个 node 让硬件继续跑, 但先把异常打出来 */
        os_printf(KERN_ERR "snap_buf_full_isr: unknown node addr=0x%x done_cnt=%u\n",
                  (unsigned)param1, (unsigned)ctx->node_done_count);
    } else {
        ctx->node_in_use[cur_idx] = 0;
        ctx->node_done[cur_idx]   = 1;
        if (ctx->node_done_count < SNAP_NODE_COUNT) {
            ctx->node_done_list[ctx->node_done_count++] = (uint8_t) cur_idx;
        }
    }

    int next_idx = snap_get_free_node(ctx);
    if (next_idx < 0) {
        /* node 池耗尽: JPG 帧实际大小 > SNAP_NODE_COUNT*SNAP_NODE_LEN.
         * 828 子码流 640x360 QT=0xC 帧典型 ~10-15KB, 池 5*5KB=25KB 应该够; 若触发说明
         * 复杂场景帧暴涨, 需要调大 SNAP_NODE_LEN 或 SNAP_NODE_COUNT */
        os_printf(KERN_ERR "snap_buf_full_isr: node pool exhausted, done_cnt=%u, increase SNAP_NODE_COUNT or SNAP_NODE_LEN\n",
                  (unsigned)ctx->node_done_count);
        os_event_set(&ctx->evt, EV_ERROR, NULL);
        return 0;
    }
    sys_dcache_invalid_range((uint32_t *) ctx->nodes[next_idx], SNAP_NODE_LEN);
    jpg_set_addr(ctx->jpg, (uint32) ctx->nodes[next_idx], SNAP_NODE_LEN);
    return 0;
}

/* ISR: 一帧编码完成 */
static int32 snap_done_isr(uint32 irq_flag, uint32 irq_data, uint32 param1, uint32 param2)
{
    struct snap_ctx *ctx = (struct snap_ctx *) irq_data;

    /* 硬件 param1 携带总帧长度 (参考 jpg_v3_msi.c 的 jpg_msi_done_isr:
     * uint32_t jpg_len = param1;) */
    ctx->frame_total_len = param1;

    /* 正在被硬件写的最后一个 node 也算 done */
    for (int i = 0; i < SNAP_NODE_COUNT; i++) {
        if (ctx->node_in_use[i]) {
            ctx->node_in_use[i] = 0;
            ctx->node_done[i]   = 1;
            if (ctx->node_done_count < SNAP_NODE_COUNT) {
                ctx->node_done_list[ctx->node_done_count++] = (uint8_t) i;
            }
        }
    }

    os_event_set(&ctx->evt, EV_DONE, NULL);
    return 0;
}

/* ISR: 异常 */
static int32 snap_err_isr(uint32 irq_flag, uint32 irq_data, uint32 param1, uint32 param2)
{
    struct snap_ctx *ctx = (struct snap_ctx *) irq_data;
    /* param1/param2 是硬件寄存器错误码,输出方便诊断 */
    os_printf(KERN_ERR "snap_err_isr: flag=0x%x p1=0x%x p2=0x%x\n",
              irq_flag, param1, param2);
    os_event_set(&ctx->evt, EV_ERROR, NULL);
    return 0;
}

/* =========================================================================
 * 对外接口 (和老 snapshot_capture/snapshot_release 签名一致)
 * ========================================================================= */

/**
 * snapshot_init - 一次性申请常驻 node 池
 * 需要在 fpv_app_init 的 JPG_EN 块里调用.
 * 常驻占用 SNAP_NODE_COUNT * SNAP_NODE_LEN 系统堆 (826=SRAM, 828=PSRAM), 多次抓拍复用.
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
 *   -3 = ctx malloc 失败 (PSRAM/SRAM OOM)
 *   -4 = JPG 设备句柄获取失败 (硬件未注册)
 *   -6 = jpg_mutex_lock 超时 500ms (其他线程持锁未释放)
 *   -7 = 启动时无空闲 node (常驻池有 bug)
 *   -8 = EV_DONE 未等到 (硬件 timeout 或 snap_err_isr 触发 EV_ERROR)
 *   -9 = frame_total_len/node_done_count 为 0 (硬件返回异常)
 *   -10 = 输出 buf malloc 失败
 *   -11 = snapshot_init 未调用 (node 池未就绪) */
static int _snapshot_capture_once(uint8_t **out_data, uint32_t *out_len, uint32_t timeout_ms)
{
    *out_data = NULL;
    *out_len  = 0;

    int ret = -1;
    uint8_t have_lock = 0;
    uint8_t hw_opened = 0;
    uint8_t irq_registered = 0;

    struct snap_ctx *ctx = (struct snap_ctx *) SNAP_MALLOC(sizeof(*ctx));
    if (!ctx) {
        os_printf(KERN_ERR "snapshot: ctx malloc(%u) failed\n", (unsigned)sizeof(*ctx));
        ret = -3; goto out;
    }
    os_memset(ctx, 0, sizeof(*ctx));
    os_event_init(&ctx->evt);

    /* 1. 拿 JPG 硬件句柄 */
    ctx->jpg = (struct jpg_device *) dev_get(HG_JPG0_DEVID + SNAP_JPG_ID);
    if (!ctx->jpg) {
        os_printf(KERN_ERR "snapshot: dev_get JPG%d failed (HG_JPG0_DEVID+%d)\n",
                  SNAP_JPG_ID, SNAP_JPG_ID);
        ret = -4; goto out;
    }

    /* 2. 引用常驻 node 池, 重置本次抓拍的状态 */
    for (int i = 0; i < SNAP_NODE_COUNT; i++) {
        ctx->nodes[i] = g_snap_nodes[i];
        sys_dcache_invalid_range((uint32_t *) ctx->nodes[i], SNAP_NODE_LEN);
    }

    /* 3. 抢 JPG 互斥锁 (最多等 500ms) */
    uint8_t last_value;
    uint32_t t_start = os_jiffies();
    uint32_t lock_retry_cnt = 0;
    while (jpg_mutex_lock(SNAP_JPG_ID, SNAP_JPG_LOCK, &last_value) != 0) {
        lock_retry_cnt++;
        if (os_jiffies() - t_start > 500) {
            /* JPG_LOCK_ENCODE 被其他业务持有超过 500ms.
             * 828: 可能是子码流 H264 编码器持锁;
             * 826: 可能是 H264 + gen420 + JPG 三路抢锁 */
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
    jpg_set_size(ctx->jpg, SNAP_IMG_H, SNAP_IMG_W);
    jpg_set_data_from(ctx->jpg, SNAP_SRC_FROM);
    jpg_set_hw_check(ctx->jpg, 1);

    /* 5. 注册 3 个 ISR */
    jpg_request_irq(ctx->jpg, snap_buf_full_isr, JPG_IRQ_FLAG_JPG_BUF_FULL, ctx);
    jpg_request_irq(ctx->jpg, snap_err_isr,      JPG_IRQ_FLAG_ERROR,        ctx);
    jpg_request_irq(ctx->jpg, snap_done_isr,     JPG_IRQ_FLAG_JPG_DONE,     ctx);
    irq_registered = 1;

    jpg_set_ready(ctx->jpg);
    jpg_set_vsync_dly(ctx->jpg, 1);
    jpg_select_oe_using(ctx->jpg, 0, 1);

    /* 6. 双缓冲启动: 第一次 set_addr 成 now, 第二次成 last */
    int i0 = snap_get_free_node(ctx);
    int i1 = snap_get_free_node(ctx);
    if (i0 < 0 || i1 < 0) {
        /* 起步阶段拿不到 2 个空闲 node, 不可能发生 (池有 SNAP_NODE_COUNT=5 块全空).
         * 若触发说明 ctx 初始化路径或 snap_get_free_node 状态机有 bug */
        os_printf(KERN_ERR "snapshot: get free node failed (i0=%d i1=%d, COUNT=%d)\n",
                  i0, i1, SNAP_NODE_COUNT);
        ret = -7; goto out;
    }
    jpg_set_addr(ctx->jpg, (uint32) ctx->nodes[i0], SNAP_NODE_LEN);
    jpg_set_addr(ctx->jpg, (uint32) ctx->nodes[i1], SNAP_NODE_LEN);

    /* 7. 启动 JPG 硬件, 等 DONE
     * JPG 硬件独立从 VPP 通道读 YUV, 不需要 gen420 协作 */
    jpg_open(ctx->jpg);
    hw_opened = 1;

    uint32_t rflags = 0;
    os_event_wait(&ctx->evt, EV_DONE | EV_ERROR, &rflags,
                  OS_EVENT_WMODE_OR | OS_EVENT_WMODE_CLEAR, timeout_ms);

    if (!(rflags & EV_DONE)) {
        /* rflags=0  → 等了 timeout_ms 也没收到任何事件, JPG 硬件可能根本没启动
         * rflags=EV_ERROR (0x2) → snap_err_isr 触发 (硬件 ERROR), 上面已打印 p1/p2
         * rflags=其他 → 接到了非预期事件 */
        const char *cause = "unknown";
        if (rflags == 0)              cause = "hw timeout (no IRQ at all)";
        else if (rflags & EV_ERROR)   cause = "EV_ERROR from snap_err_isr (see prior p1/p2)";
        os_printf(KERN_ERR "snapshot: wait done failed, rflags=0x%x timeout_ms=%u (%s) done_cnt=%u\n",
                  (unsigned)rflags, (unsigned)timeout_ms, cause,
                  (unsigned)ctx->node_done_count);
        ret = -8;
        goto out;
    }
    if (!ctx->frame_total_len || ctx->node_done_count == 0) {
        /* DONE 来了但帧长 0 或没收到 buf_full: 硬件给的统计异常.
         * 可能 ISP 出图时刚好为空帧, 或 jpg_set_size 配置不对 */
        os_printf(KERN_ERR "snapshot: invalid frame len=%u done_cnt=%u (DONE but no data)\n",
                  ctx->frame_total_len, ctx->node_done_count);
        ret = -9;
        goto out;
    }

    /* 8. 拷贝 node 链 -> 输出 buf */
    uint8_t *out_buf = (uint8_t *) SNAP_MALLOC(ctx->frame_total_len);
    if (!out_buf) {
        /* PSRAM (828) 或 SRAM (826) 不足以分配 JPG 帧 (~10-50KB).
         * 注意: 抓拍是临时分配, 外层 snapshot_release 会还回去 */
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

    *out_data = out_buf;
    *out_len  = ctx->frame_total_len;
    ret = 0;
    os_printf(KERN_INFO "snapshot: ok, len=%u addr=%p\n", ctx->frame_total_len, out_buf);

out:
    /* 清理 */
    if (hw_opened) {
        jpg_close(ctx->jpg);
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
        os_event_del(&ctx->evt);
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
    if (!g_snap_nodes_ready) {
        os_printf(KERN_ERR "snapshot: not initialized, call snapshot_init() first\n");
        return -11;
    }
    if (g_snap_busy) {
        /* 抓拍并发: 上一次还没结束, 拒绝新调用. 调用者应等当前完成后再触发. */
        os_printf(KERN_WARNING "snapshot_capture: busy, previous capture not finished\n");
        return -2;
    }
    g_snap_busy = 1;

    int ret = -1;
    uint8_t attempt = 0;
    for (attempt = 0; attempt <= SNAP_MAX_RETRY; attempt++) {
        if (attempt > 0) {
            /* 前一次失败, 让 ISP/VPP 清 FIFO 再试.
             * 100ms 是为了躲开子码流 H264 编码器 (15fps=66ms/帧) 当前帧, 落到下一帧间隙 */
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

    g_snap_busy = 0;
    return ret;
}

void snapshot_release(void *jpg_data)
{
    if (jpg_data) {
        os_printf(KERN_INFO "snapshot_release: free jpg addr=%p\r\n", jpg_data);
        SNAP_FREE(jpg_data);
    }
}

#endif  /* !SNAPSHOT_USE_LEGACY */
