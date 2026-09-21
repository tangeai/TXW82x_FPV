/*******************************************************************************
 * H264 + AAC 单 MP4 卡录像 + 探鸽 P2P 回放实现
 *
 * 本文件是单 MP4 方案的完整实现，音视频均从同一个 MP4 读写。
 *
 * 录制时使用 .REC 临时后缀，关闭并通过后台校验后才发布为 .MP4。
 * 文件命名:  0:/REC/YYYYMMDD/HHMMSS_Eee_dd.MP4
 *                                   ↑ ↑
 *                              事件类型(ECEVENT) 时长(秒)
 * 例:
 *   0:/REC/20260423/153045_E00_60.MP4    连续录 60s
 *   0:/REC/20260423/153100_E01_30.MP4    motion 报警 30s
 *
 * 按天持久化录像索引，PSRAM 仅缓存一天；缺失或失效时回退目录扫描。
 * 回放跨文件只在当前日期目录内查找; 找不到下一个就 EndOfEvent.
 ******************************************************************************/

#include "basic_include.h"
#include "project_config.h"      /* 确保 __TXW826__ / __TXW828__ 宏可见 */


/* 日期索引开关：置 0 使用按需目录扫描，不改变单 MP4 录像格式。 */
#ifndef REC_DAY_INDEX_ENABLE
#define REC_DAY_INDEX_ENABLE 1
#endif

#include <stdio.h>               /* sscanf 等标准格式化函数的声明。 */
#include <sys/time.h>
#include "osal/string.h"
#include "osal/irq.h"
#include "osal/time.h"
#include "lib/multimedia/msi.h"
#include "lib/heap/av_heap.h"
#include "lib/heap/av_psram_heap.h"
#include "lib/fs/fatfs/osal_file.h"
#include "lib/fs/fatfs/ff.h"
#include "lib/audio/audio_code/audio_code.h"
#include "stream_define.h"
#include "app/recorder/file_process.h"
#include "app/video_app/file_common_api.h"
#include "app/audio_msi/audio_adc.h"
#include "app/audio_media_ctrl/audio_code_ctrl.h"
#include "TgCloudApi.h"
#include "TgCloudCmd.h"  /* 42229 SDK Tcis_FormatExtStorageResp / TCI_CMD_FORMATEXTSTORAGE_RESP 在这里 */
#include "ec_const.h"
#include "rec_playback.h"
#include "g711.h"                /* linear2alaw: 回放 PCM 转 G.711A 用 */

/* 45229 dual-camera pipeline: main VPP_DATA0=1280x1440,
 * sub GEN420=640x720. Both streams contain the stitched two-camera image. */
#ifndef REC_STREAM_TYPE
#define REC_STREAM_TYPE 0
#endif
#if REC_STREAM_TYPE == 0
#define REC_STREAM_STYPE FSTYPE_H264_VPP_DATA0
#elif REC_STREAM_TYPE == 1
#define REC_STREAM_STYPE FSTYPE_H264_GEN420_DATA
#else
#error "REC_STREAM_TYPE must be 0 (main) or 1 (sub)"
#endif
/* 旧别名: 之前代码用 REC_SUB_STREAM_STYPE, 保留作为向后兼容, 指向同一个值 */
#define REC_SUB_STREAM_STYPE    REC_STREAM_STYPE


/* 系统不支持时区, 自定义全局时区偏移 (秒).
 * 语义: local_secs = utc_secs + _tg_timezone_, 北京时区 = 28800 */
extern long _tg_timezone_;

/* 录像列表/日期扫描用的内存池按芯片分化:
 *   826: PSRAM 仅 4MB 极度紧张, 扫描期间的临时数组挪到 SRAM (相对富余)
 *   828: PSRAM 宽裕, SRAM 紧张, 保持走 PSRAM
 * 注意: MALLOC 和 FREE 必须同池, 跨池 free 会破坏堆元数据 */
#if defined(__TXW826__)
#define RP_MALLOC(sz)       _os_malloc(sz)
#define RP_FREE(p)          do { if(p) { _os_free(p); (p)=NULL; } } while(0)
#else
#define RP_MALLOC(sz)       _os_malloc_psram(sz)
#define RP_FREE(p)          do { if(p) { _os_free_psram(p); (p)=NULL; } } while(0)
#endif

/* =========================================================================
 * 外部依赖声明
 * ========================================================================= */
extern int GetNetworkState(void);
extern volatile uint8_t time_sync_flag;  /* 提前声明，供文件恢复和格式化逻辑使用。 */
extern struct msi *mp4_encode_msi2_init(const char *mp4_msi_name, uint8_t srcID, uint8_t filter_type,
                                        uint8_t rec_time, uint32_t audio_encode,
                                        struct file_process *file_process, uint8_t mode);
extern struct msi *mp4_demux_msi_init(const char *msi_name, const char *filename);
extern uint32_t mp4_demux_active_workers(void);
extern uint32_t mp4_encode_active_workers(void);
extern int fatfs_prepare_remount(void);
extern int fatfs_recover_mount(void);

/* =========================================================================
 * 全局状态
 * ========================================================================= */

/* 录像模式 - 全局变量, 默认全天连续录像 */
static volatile rec_mode_t g_rec_mode = REC_MODE_ALL_DAY;

/* 是否已经初始化 */
static volatile uint8_t g_rp_inited = 0;

/* sd_format 进行中标志: 阻塞 rec_bootstrap_thread 的预删除循环, 防止跟 f_mkfs 抢 SD.
 * sd_format 入口设 1, 出口设 0; 预删除循环每轮开头检查. */
static volatile uint8_t g_sd_formatting = 0;
static volatile uint8_t g_sd_fault_pending;
static volatile uint32_t g_sd_fault_epoch;
static uint8_t g_sd_recovering;
/* 驱动通知可能在磁盘锁内发生：这里只置标志，不等待、不操作文件。 */
int sd_storage_app_managed(void) { return 1; }
int sd_storage_app_busy(void) { return g_sd_formatting || g_sd_fault_pending; }
void sd_storage_request_recovery(void)
{
    uint32_t flags = disable_irq();
    g_sd_fault_pending = 1;
    ++g_sd_fault_epoch;
    enable_irq(flags);
}
/* 门锁只保护准入计数，不在锁内做 SD IO 或等待任务退出。格式化先封门再排空。 */
static struct os_mutex g_sd_gate;
/* Serialize app mode changes, motion triggers and background record controls. */
static struct os_mutex g_rec_control;
static uint32_t g_sd_users;
static uint32_t g_sd_generation;
#define REC_TEMP_EXT ".REC"
static int rec_sd_enter(void)
{
    if (!g_rp_inited) return -1;
    os_mutex_lock(&g_sd_gate, osWaitForever);
    if (g_sd_formatting || g_sd_fault_pending) {
        os_mutex_unlock(&g_sd_gate);
        return -1;
    }
    ++g_sd_users;
    os_mutex_unlock(&g_sd_gate);
    return 0;
}
static void rec_sd_leave(void)
{
    os_mutex_lock(&g_sd_gate, osWaitForever);
    if (g_sd_users) --g_sd_users;
    os_mutex_unlock(&g_sd_gate);
}
/* 本模块目录迭代保留错误码，读取失败不能伪装成目录结束。 */
typedef struct { DIR dir; FILINFO info; FRESULT error; } rec_dir;
static void *rec_dir_open(const char *path)
{
    rec_dir *d = _os_malloc_psram(sizeof(*d));
    if (!d) return NULL;
    d->error = f_opendir(&d->dir, path);
    if (d->error != FR_OK) { _os_free_psram(d); return NULL; }
    return d;
}
static void *rec_dir_read(void *ptr)
{
    rec_dir *d = ptr;
    if (!d || g_sd_formatting || g_sd_fault_pending) { if (d) d->error = FR_NOT_READY; return NULL; }
    d->error = f_readdir(&d->dir, &d->info);
    return d->error == FR_OK && d->info.fname[0] ? &d->info : NULL;
}
static int rec_dir_failed(void *ptr) { return ptr && ((rec_dir *)ptr)->error != FR_OK; }
static void rec_dir_close(void *ptr)
{
    if (!ptr) return;
    f_closedir(&((rec_dir *)ptr)->dir);
    _os_free_psram(ptr);
}
static int rec_has_ext(const char *name, const char *ext)
{
    int n = os_strlen(name);
    return n >= 4 && os_strcasecmp(name + n - 4, ext) == 0;
}
static int rec_recover_file(const char *path);
static void rec_pending_scan(void);
static int sd_get_capacity_raw(uint32_t *total, uint32_t *free);
static int pb_mp4_is_playable(const char *path);
static int day_dir_has_mp4(const char *date_dir);
static volatile uint8_t g_sync_base_valid;

/* 正在删除的文件名/日期目录快照. rec_recycle_oldest 进入 osal_unlink 前设置,
 * 删完立即清空. scan_day_dir 在两遍扫描里都跳过该名字, 这样:
 *   - APP 调 rec_list_get 拿到的列表不含将被删的文件
 *   - pb_locate_file (回放定位) 也会跳过它
 * 避免 "APP 看到 X → 几百 ms 后请求播放 X → fopen 失败" 的竞态.
 * 注意: 名字+目录两个一起匹配, 防止误伤同名跨日文件. */
static volatile char g_recycling_fname[FILE_NAME_LEN + 1] = {0};
static volatile char g_recycling_date_dir[16] = {0};

/* 保护 rec_recycle_oldest 的并发调用. 两个潜在调用者:
 *   1) rec_bootstrap_thread (BELOW_NORMAL): 预删除循环
 *   2) mp4_encode_thread    (ABOVE_NORMAL): rec_create_file_cb 兜底删除
 * 不加锁两路并发会让 FatFS f_unlink 状态错乱, scan_day_dir 选到相同 oldest 文件,
 * g_recycling_fname 也会互相覆盖. mutex 把整个 rec_recycle_oldest 串行化. */
static struct os_mutex g_recycle_lock;
static volatile uint8_t g_recycle_lock_inited = 0;

/* 回放是否在跑的前向访问器: rec_bootstrap_thread 定义在 g_pb (line ~1317) 之前,
 * 不能直接读 g_pb.thread_alive, 通过这个函数转一手, 实现在文件底部. */
static int rec_pb_is_active(void);

/* SD 卡不可恢复故障标志: 连续 REC_UNRECOV_FAIL_LIMIT 次 rec_create_file_cb
 * 返回 NULL 后置 1. 一旦置 1:
 *   - rec_bootstrap_thread 主动停录像 (_rp_record_stop)
 *   - 每 3 秒打印告警, 同时 sd_get_capacity 探测 SD 卡是否自恢复
 *   - 自恢复后清 0, 重启录像
 * 用户也可调 sd_format 强制清状态 (sd_format 内会清这个标志).
 * 设计判定: 看 rec_create_file 失败累计 (最顶层信号), 不看 fwrite/fclose. */
#define REC_UNRECOV_FAIL_LIMIT  3U    /* 连续 3 次创建失败 = 不可恢复 (约 3 分钟内确认故障) */
#define REC_UNRECOV_ALARM_MS    3000U /* 告警打印间隔 */
static volatile uint8_t g_sd_unrecoverable = 0;
/* rec_create_file_cb 连续返 NULL 计数. 文件级 static, 让 rec_create_fail_inc
 * helper 也能访问 (helper 同文件 static 函数). */
static uint32_t g_create_null_streak = 0;

/* rec_create_file_cb 失败 (return NULL) 时统一调这个 helper:
 *   - 累计连续失败次数
 *   - 达到 REC_UNRECOV_FAIL_LIMIT 时设 g_sd_unrecoverable=1, 触发 bootstrap
 *     线程停录像 + 告警轮询
 * 成功 fopen mp4 时调 rec_create_fail_reset() 清零. */
static void rec_create_fail_inc(void)
{
    g_create_null_streak++;
    if (g_create_null_streak >= REC_UNRECOV_FAIL_LIMIT && !g_sd_unrecoverable) {
        g_sd_unrecoverable = 1;
        os_printf(KERN_ERR "rec_create_file: SD card UNRECOVERABLE (%u consecutive NULL returns), "
                           "bootstrap_thread will stop recording and watch for self-heal\n",
                  (unsigned)g_create_null_streak);
    }
}
static inline void rec_create_fail_reset(void)
{
    if (g_create_null_streak) {
        os_printf(KERN_INFO "rec_create_file: SD healthy again, clear null_streak=%u\n",
                  (unsigned)g_create_null_streak);
        g_create_null_streak = 0;
    }
}

/* =========================================================================
 * 断网启动录像 (UNSYNC 模式)
 *
 * 需求: 设备启动时若还没等到 NTP/平台时间同步 (time_sync_flag=0), 也要能录像,
 * 不能因为断网就永远不录. 但设备没有 RTC, 时间没同步时 time(NULL) 是开机相对
 * 时间 (从固定基准走), 直接拿来命名会生成错误日期的文件, NTP 来了又跳变, 乱套.
 *
 * 方案: 时间没同步时进入 UNSYNC 模式, 文件录到独立目录 0:/REC/UNSYNC/, 文件名
 * 用开机秒 boot_sec (单调递增可排序). NTP 同步那一刻记基准 (g_sync_utc,
 * g_sync_boot_sec), 由 bootstrap_thread 把 UNSYNC 目录文件按
 *   真实UTC = g_sync_utc - (g_sync_boot_sec - file_boot_sec)
 * 还原到真实 YYYYMMDD/HHMMSS 路径 (相对时间在同一开机会话内是准的).
 *
 * 重启残留 (上次没等到同步就断电): boot_sec 基准随重启丢失, 时间无法还原,
 * 直接整目录删除 (不保留, 时间不可知的录像留着也无意义).
 * ========================================================================= */
#define REC_UNSYNC_DIR          "0:/REC/UNSYNC"   /* 未同步录像临时目录 */
#define REC_TIME_SYNC_WAIT_MS   15000U            /* 启动等时间同步超时, 超时进 UNSYNC 模式 */

static volatile uint8_t  g_rec_unsync_mode  = 0;  /* 1=当前 UNSYNC 模式录像 (文件录到 UNSYNC 目录) */
static volatile uint8_t  g_sync_pending     = 0;  /* set_time 置 1, 通知 bootstrap_thread 做 migrate */
static volatile uint32_t g_sync_utc         = 0;  /* NTP 同步时刻的真实 UTC 秒 */
static volatile uint32_t g_sync_boot_sec    = 0;  /* NTP 同步时刻的开机秒 (os_jiffies) */

/* 1=有 UNSYNC 录像待迁移到真实时间目录. 由 mp4_encode_thread 在同步后第一次切
 * 文件 (rec_create_file_cb 走真实路径) 时置位, 或 bootstrap 在"同步时无活跃录像"
 * 时直接置位; bootstrap 线程检测后异步执行 rec_unsync_migrate_all.
 * 关键: 迁移绝不在录像活跃时 stop/destroy msi —— msi_destroy 是异步引用计数销毁,
 * 返回时 mp4_encode 线程未必退出, 紧接着 migrate/restart 会在 os_event_del/free 与
 * 线程 os_event_wait 之间踩到 use-after-free (assert evt->magic). 必须等编码线程
 * 自然切到真实目录、旧 UNSYNC 文件 fclose 后再 rename. */
static volatile uint8_t  g_unsync_need_migrate = 0;

/* NTP 时间首次同步回调 (set_time 里调). 只记基准 + 置标志, 零 IO 零阻塞,
 * 真正的 UNSYNC 文件迁移交给 rec_bootstrap_thread 异步做. */
void rec_on_time_synced(uint32_t utc)
{
    if (g_sync_base_valid) return; /* 同一开机会话只固定一次映射，迁移期间不被再次校时改写。 */
    g_sync_utc      = utc;
    g_sync_boot_sec = (uint32_t)(os_jiffies_to_msecs(os_jiffies()) / 1000);
    g_sync_pending  = 1;
    g_sync_base_valid = 1;
}

/* MSI */
static struct msi *g_rec_msi     = NULL;   /* MP4 编码/复用 msi */
static struct msi *g_rec_h264    = NULL;   /* 上游 AUTO_H264 */
static struct msi *g_rec_aac     = NULL;   /* S_AUADC → AAC 编码器 */
static uint8_t     g_rec_aac_attached = 0;

/* 录像侧 AAC encoder 生命周期。只供本文件的单 MP4 方案使用。 */
static int rec_aac_prepare(void)
{
    AUENC_INIT auenc_init;
    struct msi *adc;
    uint32_t samplerate;

    if (g_rec_aac)
        return RET_OK;

    adc = get_auadc_msi(MAIN_MIC_ID);
    if (!adc) {
        os_printf(KERN_ERR "rec_mp4aac: audio ADC msi not ready\n");
        return RET_ERR;
    }

    samplerate = audio_adc_get_samplerate(MAIN_MIC_ID);
    /* 本产品 AAC sample duration 为 128ms，即 1024/8000。
     * 非 8kHz 时继续录像会造成音频时间轴错误，因此明确拒绝启动。 */
    if (samplerate != 8000) {
        os_printf(KERN_ERR "rec_mp4aac: unsupported samplerate=%u, require 8000Hz\n",
                  (unsigned)samplerate);
        return RET_ERR;
    }

    os_memset(&auenc_init, 0, sizeof(auenc_init));
    auenc_init.destroy_self = 0;
    auenc_init.channels = 1; /* 45229 requires the encoder channel count. */
    auenc_init.src_msi = adc;

    g_rec_aac = audio_encode_init(AAC_ENC, samplerate, &auenc_init);
    if (!g_rec_aac) {
        os_printf(KERN_ERR "rec_mp4aac: AAC encoder init failed\n");
        return RET_ERR;
    }

    g_rec_aac_attached = 0;
    os_printf(KERN_INFO "rec_mp4aac: AAC encoder ready, samplerate=%u\n",
              (unsigned)samplerate);
    return RET_OK;
}

static int rec_aac_attach(const char *mp4_msi_name)
{
    int ret;

    if (!g_rec_aac || !mp4_msi_name)
        return RET_ERR;
    if (g_rec_aac_attached)
        return RET_OK;

    ret = audio_code_add_output(g_rec_aac, mp4_msi_name);
    if (ret != RET_OK) {
        os_printf(KERN_ERR "rec_mp4aac: attach AAC -> %s failed, ret=%d\n",
                  mp4_msi_name, ret);
        return ret;
    }

    g_rec_aac_attached = 1;
    os_printf(KERN_INFO "rec_mp4aac: AAC attached to %s\n", mp4_msi_name);
    return RET_OK;
}

static void rec_aac_stop(const char *mp4_msi_name)
{
    if (!g_rec_aac)
        return;

    if (g_rec_aac_attached && mp4_msi_name) {
        audio_code_del_output(g_rec_aac, mp4_msi_name);
        g_rec_aac_attached = 0;
    }

    audio_encode_deinit(g_rec_aac);
    g_rec_aac = NULL;
    os_printf(KERN_INFO "rec_mp4aac: AAC encoder stopped\n");
}

/* 当前正在录的文件信息 (用于报警延长/标记事件类型) */
static struct {
    char      fname[24];                  /* 兼容较长的 UNSYNC 开机秒文件名。 */
    char      fpath[96];                   /* 当前完整路径 */
    uint32_t  start_tick;                  /* First-frame monotonic tick, independent of NTP. */
    uint32_t  t_start;                     /* 开始时刻 UTC */
    uint8_t   event;                       /* 事件类型 */
    uint8_t   duration_sec;                /* 计划时长 */
    uint8_t   extended;                    /* 是否已经延长过 */
} g_curfile;

/* 已关闭 MP4 的时长校正交给 bootstrap 低优先级任务。
 * 编码线程只投递路径，不在切片点读 mdhd/改名，避免 H264 堆积。 */
#define REC_FINALIZE_SLOTS  4
static volatile uint8_t g_finalize_pending[REC_FINALIZE_SLOTS] = {0};
static char             g_finalize_path[REC_FINALIZE_SLOTS][96] = {{0}};
/* stop 与 mp4_encode 真正收尾是异步的；收尾期间仍需将该文件视为“正在写”。 */
static volatile uint8_t g_rec_file_closing = 0;

/* =========================================================================
 * 工具函数
 * ========================================================================= */

/* 从文件名解析事件类型和时长: HHMMSS_Eee_dd.MP4 */
static int parse_filename(const char *name, uint8_t *event, uint16_t *duration,
                          uint8_t *hh, uint8_t *mm, uint8_t *ss)
{
    int h, m, s, e, d;
    if (os_strlen(name) < 17)
        return -1;
    if (sscanf(name, "%02d%02d%02d_E%02d_%d", &h, &m, &s, &e, &d) != 5)
        return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59 || e < 0 || e > 99 || d <= 0 || d > 65535) return -1;
    if (hh) *hh = h;
    if (mm) *mm = m;
    if (ss) *ss = s;
    if (event) *event = e;
    if (duration) *duration = d;
    return 0;
}

/* 从日期目录名解析年月日: YYYYMMDD */
static int parse_dirname(const char *name, uint16_t *year, uint8_t *mon, uint8_t *day)
{
    int y, mm, dd;
    if (os_strlen(name) != 8)
        return -1;
    if (sscanf(name, "%04d%02d%02d", &y, &mm, &dd) != 3)
        return -1;
    if (y < 2020 || y > 2099 || mm < 1 || mm > 12 || dd < 1 || dd > 31) return -1;
    if (year) *year = y;
    if (mon) *mon = mm;
    if (day) *day = dd;
    return 0;
}

/* 时间语义统一: 所有文件名/目录名按"本地时间"命名 (和 APP 下发的时间语义一致).
 * 系统 gmtime/mktime 是纯 UTC 数学转换 (不考虑时区), 自己叠加 _tg_timezone_ 做时区偏移.
 *
 *   utc_to_dir/time(utc)   : 输入 UTC 秒  → 输出本地时间目录/HMS
 *   tmval_to_utc(y..s)     : 输入本地年月日时分秒  → 输出 UTC 秒 */

/* UTC 秒 → 本地日期目录 YYYYMMDD */
static void utc_to_dir(uint32_t utc, char *buf, int buf_sz)
{
    time_t t = (time_t)((long) utc + _tg_timezone_);   /* UTC → local 秒 */
    struct tm *tm = gmtime(&t);                         /* gmtime 做纯数学分解 */
    if (tm) {
        os_snprintf(buf, buf_sz, "%04d%02d%02d",
                    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    } else {
        os_strncpy(buf, "00000000", buf_sz);
    }
}

/* UTC 秒 → 本地 HHMMSS */
static void utc_to_time(uint32_t utc, char *buf, int buf_sz)
{
    time_t t = (time_t)((long) utc + _tg_timezone_);
    struct tm *tm = gmtime(&t);
    if (tm) {
        os_snprintf(buf, buf_sz, "%02d%02d%02d",
                    tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        os_strncpy(buf, "000000", buf_sz);
    }
}

/* 本地 年月日时分秒 → UTC 秒 */
static uint32_t tmval_to_utc(uint16_t y, uint8_t mon, uint8_t day,
                             uint8_t h, uint8_t m, uint8_t s)
{
    struct tm t;
    os_memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = day;
    t.tm_hour = h;
    t.tm_min  = m;
    t.tm_sec  = s;
    time_t local = mktime(&t);          /* 纯数学合成, 得到本地秒 */
    return (uint32_t)((long) local - _tg_timezone_);  /* 本地 → UTC */
}

/* =========================================================================
 * 文件扫描辅助 (按需, 不维护常驻索引)
 * ========================================================================= */

/* 单次查询的临时文件条目.
 * !!! 不存 fname 字符串, 节省内存:
 *     原 fname[17] 使单条 scan_item_t ≈ 28B, 1440 条 = 40KB PSRAM,
 *     在碎片严重时申请失败. 改存 hh/mm/ss 配合 event/duration, 运行时
 *     用 build_fname_from_item() 按需重建 "HHMMSS_Eee_dd.MP4" 字符串.
 *     新结构 12B, 1440 条 ≈ 17KB, 省 23KB. */
typedef struct {
    uint32_t t_start;       /* UTC 秒 */
    uint16_t duration;      /* 文件名中的时长 (文件关闭时已校正为真实时长) */
    uint8_t  event;
    uint8_t  hh, mm, ss;    /* 重建文件名用 */
} scan_item_t;

static FRESULT rec_idx_change(const char *src, const char *dest);
static void rec_idx_empty_day(const char *date);

/* 从 scan_item_t 重建文件名字符串 "HHMMSS_Eee_dd.MP4".
 * buf 至少 FILE_NAME_LEN+1 字节 */
static void build_fname_from_item(const scan_item_t *it, char *buf, int buf_sz)
{
    os_snprintf(buf, buf_sz, "%02u%02u%02u_E%02u_%u%s",
                it->hh, it->mm, it->ss, it->event,
                it->duration, REC_EXT_NAME);
}

/* MP4 box 整数均为大端。关闭后的单文件校正只进入
 * moov/trak/mdia 三层并读取 mdhd，不扫描 mdat 视频数据。 */
static uint32_t rec_mp4_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static uint64_t rec_mp4_be64(const uint8_t *p)
{
    return ((uint64_t)rec_mp4_be32(p) << 32) | rec_mp4_be32(p + 4);
}

static int rec_mp4_box_is(const uint8_t *type, const char *name)
{
    return type[0] == (uint8_t)name[0] && type[1] == (uint8_t)name[1] &&
           type[2] == (uint8_t)name[2] && type[3] == (uint8_t)name[3];
}

/* 递归查找所有 mdhd，取音视频轨中较长的一条作为文件真实覆盖时长。 */
static void rec_mp4_scan_mdhd(F_FILE *fp, uint32_t begin, uint32_t end,
                              uint8_t depth, uint64_t *max_duration_ms)
{
    uint32_t pos = begin;

    if (!fp || !max_duration_ms || depth > 3)
        return;

    while (pos <= end && end - pos >= 8U) {
        uint8_t hdr[8];
        uint32_t box_size;
        uint32_t box_end;

        if (osal_fseek(fp, pos) != FR_OK || osal_fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr))
            return;

        box_size = rec_mp4_be32(hdr);
        if (box_size == 0U)
            box_end = end;
        else {
            /* 本设备 FatFS/文件偏移均为 32 位，不生成 extended-size box。 */
            if (box_size == 1U || box_size < 8U || box_size > end - pos)
                return;
            box_end = pos + box_size;
        }

        if (rec_mp4_box_is(hdr + 4, "mdhd")) {
            uint8_t body[32];
            uint32_t body_len = box_end - (pos + 8U);
            uint32_t timescale = 0;
            uint64_t duration = 0;

            if (body_len > sizeof(body)) body_len = sizeof(body);
            if (body_len >= 20U && osal_fread(body, 1, body_len, fp) == body_len) {
                if (body[0] == 0 && body_len >= 20U) {
                    timescale = rec_mp4_be32(body + 12);
                    duration  = rec_mp4_be32(body + 16);
                } else if (body[0] == 1 && body_len >= 32U) {
                    timescale = rec_mp4_be32(body + 20);
                    duration  = rec_mp4_be64(body + 24);
                }
                if (timescale != 0U && duration != 0U) {
                    uint64_t duration_ms = (duration * 1000ULL + timescale - 1U) / timescale;
                    if (duration_ms > *max_duration_ms)
                        *max_duration_ms = duration_ms;
                }
            }
        } else if (rec_mp4_box_is(hdr + 4, "moov") ||
                   rec_mp4_box_is(hdr + 4, "trak") ||
                   rec_mp4_box_is(hdr + 4, "mdia")) {
            rec_mp4_scan_mdhd(fp, pos + 8U, box_end, depth + 1U,
                              max_duration_ms);
        }

        if (box_end <= pos)
            return;
        pos = box_end;
    }
}

/* 成功返回 0；损坏、未完成或无 mdhd 时由调用者继续使用文件名时长。 */
static int rec_mp4_get_duration_sec(const char *path, uint16_t *duration_sec)
{
    F_FILE *fp;
    uint32_t file_size;
    uint64_t duration_ms = 0;
    uint64_t sec;

    if (!path || !duration_sec)
        return -1;

    fp = osal_fopen(path, "rb");
    if (!fp)
        return -1;

    file_size = osal_fsize(fp);
    if (file_size >= 8U)
        rec_mp4_scan_mdhd(fp, 0, file_size, 0, &duration_ms);
    osal_fclose(fp);

    /* 向上取整到秒，避免毫秒截断人为制造 1 秒列表缝隙。 */
    sec = (duration_ms + 999ULL) / 1000ULL;
    if (sec == 0U || sec > 0xffffU)
        return -1;

    *duration_sec = (uint16_t)sec;
    return 0;
}

/* 编码线程投递一个已 fclose 的文件。正常每 60s 才产生一次，
 * bootstrap 每 3s 消费，两个槽位还能覆盖“刚切片立即停录”；若 SD 卡持续卡顿，
 * 宁可保留新文件的计划时长，也不阻塞 mp4_encode 线程。 */
static void rec_finalize_enqueue(const char *path)
{
    uint8_t i;

    if (!path || !path[0])
        return;
    for (i = 0; i < REC_FINALIZE_SLOTS; i++)
        if (g_finalize_pending[i] && !os_strcmp(g_finalize_path[i], path)) return;
    for (i = 0; i < REC_FINALIZE_SLOTS; i++) {
        if (!g_finalize_pending[i]) {
            os_strncpy(g_finalize_path[i], path, sizeof(g_finalize_path[i]) - 1);
            g_finalize_path[i][sizeof(g_finalize_path[i]) - 1] = '\0';
            g_finalize_pending[i] = 1;       /* 路径写完后再发布 */
            return;
        }
    }
    os_printf(KERN_WARNING "rec_finalize: queue full, background scan will retry: %s\n", path);
}

/* 只由 rec_bootstrap_thread 调用：读取刚关闭 MP4 的 mdhd，
 * 再将 HHMMSS_Eee_60.MP4 改成 HHMMSS_Eee_<真实秒数>.MP4。
 * 列表查询只需解析校正后的文件名。 */
#include "rec_mp4_recover.h"

/* 只有本机临时录像才修复/删除。IO 失败保留，严重结构错误在重复确认后才删除。 */
static int rec_recover_file(const char *path)
{
    if (g_sd_formatting || !rec_has_ext(path, REC_TEMP_EXT)) return -1;
    if ((g_rec_msi || g_rec_file_closing) && !os_strcmp(path, g_curfile.fpath)) return -1;
    int ret = rec_fix_index(path);
    if (ret == 0) {
        uint32_t total = 0;
        /* 再打开确认一次，避免单次读卡异常被误分类；卡状态失败也不能删除。 */
        if (g_sd_formatting || sd_get_capacity_raw(&total, NULL) || !total) return -1;
        ret = rec_fix_index(path);
        if (ret == 0 && !g_sd_formatting) {
            FRESULT res = osal_unlink(path);
            os_printf(KERN_WARNING "rec_recover: invalid index, delete res=%d %s\n", res, path);
            return res == FR_OK ? 0 : -1;
        }
    }
    if (ret != 1 || g_sd_formatting) return -1;
    uint16_t dur;
    if (rec_mp4_get_duration_sec(path, &dur) || !dur || dur > 99U) return -1;
    char dest[96];
    const char *slash = os_strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    unsigned event, old_dur, boot;
    if (os_strstr(path, REC_UNSYNC_DIR) == path) {
        if (sscanf(name, "%u_E%02u_%u", &boot, &event, &old_dur) != 3) return -1;
        os_snprintf(dest, sizeof(dest), "%s/%u_E%02u_%02u.MP4", REC_UNSYNC_DIR, boot, event, dur);
    } else {
        uint8_t ev, h, m, s; uint16_t old;
        if (parse_filename(name, &ev, &old, &h, &m, &s)) return -1;
        int prefix = slash ? (int)(slash - path + 1) : 0;
        if (prefix + 18 >= sizeof(dest)) return -1;
        os_memcpy(dest, path, prefix);
        os_snprintf(dest + prefix, sizeof(dest) - prefix, "%02u%02u%02u_E%02u_%02u.MP4", h, m, s, ev, dur);
    }
    if (g_sd_formatting) return -1;
    FRESULT res = rec_idx_change(path, dest);
    os_printf(KERN_INFO "rec_recover: publish res=%d %s -> %s\n", res, path, dest);
    /* 目标已存在时不覆盖、不删除源文件，交由后续人工核查。 */
    if (res == FR_OK && os_strstr(dest, REC_UNSYNC_DIR) == dest && time_sync_flag)
        g_unsync_need_migrate = 1;
    return res == FR_OK ? 1 : -1;
}
static void rec_finalize_pending_run(void)
{
    if (g_sd_formatting) return;
    for (unsigned i = 0; i < REC_FINALIZE_SLOTS; ++i) {
        if (g_finalize_pending[i] != 1) continue;
        char path[96];
        uint32_t flags = disable_irq();
        os_strncpy(path, g_finalize_path[i], sizeof(path));
        g_finalize_pending[i] = 2;
        enable_irq(flags);
        rec_recover_file(path);
        /* 失败文件仍留在卡上，周期扫描会重试，队列无需无限占用。 */
        g_finalize_pending[i] = 0;
        break;
    }
}

/* 后台每次只恢复一个残留临时文件。游标跨轮推进，坏卡/重名文件不会饿死其它文件。 */
static void rec_pending_scan(void)
{
    static char cursor[96];
    static uint32_t generation = 0xffffffffU;
    if (generation != g_sd_generation) { cursor[0] = 0; generation = g_sd_generation; }
    if (g_sd_formatting) return;
    char best[96] = {0};
    void *root = rec_dir_open(REC_ROOT_PATH);
    if (!root) return;
    void *ent;
    while ((ent = rec_dir_read(root)) != NULL) {
        char date[16];
        const char *name = osal_dirent_name(ent);
        uint16_t y; uint8_t m, d;
        if (!osal_dirent_isdir(ent) || os_strlen(name) >= sizeof(date)) continue;
        int unsync = !os_strcmp(name, "UNSYNC");
        if (!unsync && parse_dirname(name, &y, &m, &d)) continue;
        os_strcpy(date, name);
        char folder[48];
        os_snprintf(folder, sizeof(folder), "%s/%s", REC_ROOT_PATH, date);
        void *dir = rec_dir_open(folder);
        if (!dir) { rec_dir_close(root); return; }
        void *item;
        while ((item = rec_dir_read(dir)) != NULL) {
            const char *fn = osal_dirent_name(item);
            if (osal_dirent_isdir(item) || !rec_has_ext(fn, REC_TEMP_EXT) ||
                os_strlen(fn) >= 24U) continue;
            uint8_t ev, h, mm, ss; uint16_t len;
            unsigned boot, event, duration;
            if (unsync) {
                if (sscanf(fn, "%u_E%02u_%u", &boot, &event, &duration) != 3) continue;
            } else if (parse_filename(fn, &ev, &len, &h, &mm, &ss)) continue;
            char path[96];
            os_snprintf(path, sizeof(path), "%s/%s", folder, fn);
            if ((g_rec_msi || g_rec_file_closing) && !os_strcmp(path, g_curfile.fpath)) continue;
            if (os_strcmp(path, cursor) <= 0) continue;
            if (!best[0] || os_strcmp(path, best) < 0) os_strcpy(best, path);
        }
        int failed = rec_dir_failed(dir);
        rec_dir_close(dir);
        if (failed) { rec_dir_close(root); return; }
    }
    int failed = rec_dir_failed(root);
    rec_dir_close(root);
    if (failed) return;
    if (best[0]) {
        os_strcpy(cursor, best);
        rec_recover_file(best);
    } else cursor[0] = 0;
}

static int cmp_scan_item(const void *a, const void *b)
{
    const scan_item_t *ia = (const scan_item_t *) a;
    const scan_item_t *ib = (const scan_item_t *) b;
    if (ia->t_start < ib->t_start) return -1;
    if (ia->t_start > ib->t_start) return 1;
    return 0;
}

/* 扫描指定日期目录, 返回按时间排序的文件条目数组.
 * @param date_dir  YYYYMMDD, 如 "20260423"
 * @param out_arr   [out] 分配的条目数组, 需要调用者 RP_FREE 释放
 * @param out_cnt   [out] 条目数
 * @return 0 成功
 */
static int scan_day_dir_raw(const char *date_dir, scan_item_t **out_arr, uint32_t *out_cnt,
                            int skip_recycling)
{
    *out_arr = NULL;
    *out_cnt = 0;

    char sub_path[64];
    os_snprintf(sub_path, sizeof(sub_path), "%s/%s", REC_ROOT_PATH, date_dir);

    void *d = rec_dir_open(sub_path);
    if (!d) {
        FILINFO info;
        FRESULT res = f_stat(sub_path, &info);
        return res == FR_NO_FILE || res == FR_NO_PATH ? 0 : -1;
    }

    uint16_t y;
    uint8_t  mon, day;
    if (parse_dirname(date_dir, &y, &mon, &day) != 0) {
        rec_dir_close(d);
        return -1;
    }

    /* 过滤"正在写入的当前录像文件" — 未完成关闭的 MP4 box 结构不完整,
     * 如果回放打开它, mp4_demux 解析会失败, 或者读到尚未 fsync 的脏数据.
     * 录像启动后 g_curfile.fname/fpath 一直指向"当前正在写"的那个文件,
     * 切分新文件时会更新. rec_create_file_cb 先 fclose 旧文件再 fopen 新的,
     * 所以只有"与 g_curfile 完全同名同路径"的才算未完成.
     * !!! g_curfile 仅在录像模式激活时有效; 未激活时 fname 为空字符串 */
    char cur_fname_snapshot[FILE_NAME_LEN + 1] = {0};
    char cur_fpath_snapshot[96] = {0};
    if (g_rec_msi || g_rec_file_closing) {
        os_strncpy(cur_fname_snapshot, g_curfile.fname, sizeof(cur_fname_snapshot) - 1);
        os_strncpy(cur_fpath_snapshot, g_curfile.fpath, sizeof(cur_fpath_snapshot) - 1);
    }

    /* 过滤"正在被预删除线程 unlink 的文件" - 见 g_recycling_fname 定义注释.
     * 同样快照后只读, 避免和 rec_recycle_oldest 写入时的竞态. */
    char recy_fname_snapshot[FILE_NAME_LEN + 1] = {0};
    char recy_dir_snapshot[16] = {0};
    os_strncpy(recy_fname_snapshot, (const char *)g_recycling_fname,
               sizeof(recy_fname_snapshot) - 1);
    os_strncpy(recy_dir_snapshot,   (const char *)g_recycling_date_dir,
               sizeof(recy_dir_snapshot) - 1);

    /* 先数一遍 */
    uint32_t cnt = 0;
    void *fno;
    while ((fno = rec_dir_read(d)) != NULL) {
        char *fname = osal_dirent_name(fno);
        if (!fname || osal_dirent_isdir(fno)) continue;
        int nlen = os_strlen(fname);
        if (nlen < 5 || os_strcasecmp(fname + nlen - 4, REC_EXT_NAME) != 0) continue;
        uint8_t ev; uint16_t name_dur; uint8_t hh, mm, ss;
        if (parse_filename(fname, &ev, &name_dur, &hh, &mm, &ss) != 0) continue;
        /* 过滤正在录的文件: fname 匹配 + date_dir 隶属 fpath */
        if (cur_fname_snapshot[0] &&
            os_strcmp(fname, cur_fname_snapshot) == 0 &&
            os_strstr(cur_fpath_snapshot, date_dir) != NULL) {
            continue;
        }
        /* 过滤正在被预删除的文件 (date_dir + fname 双匹配, 防误伤跨日同名) */
        if (skip_recycling && recy_fname_snapshot[0] &&
            os_strcmp(fname, recy_fname_snapshot) == 0 &&
            os_strcmp(date_dir, recy_dir_snapshot) == 0) {
            continue;
        }
        cnt++;
    }
    int scan_failed = rec_dir_failed(d);
    rec_dir_close(d);
    if (scan_failed) return -1;

    if (cnt == 0) return 0;

    scan_item_t *arr = (scan_item_t *) RP_MALLOC(sizeof(scan_item_t) * cnt);
    if (!arr) return -1;

    /* 第二遍收集 */
    d = rec_dir_open(sub_path);
    if (!d) { RP_FREE(arr); return -1; }

    uint32_t idx = 0;
    while ((fno = rec_dir_read(d)) != NULL && idx < cnt) {
        char *fname = osal_dirent_name(fno);
        if (!fname || osal_dirent_isdir(fno)) continue;
        int nlen = os_strlen(fname);
        if (nlen < 5 || os_strcasecmp(fname + nlen - 4, REC_EXT_NAME) != 0) continue;
        uint8_t ev; uint16_t name_dur; uint8_t hh, mm, ss;
        if (parse_filename(fname, &ev, &name_dur, &hh, &mm, &ss) != 0) continue;
        /* 同 pass1 过滤规则, 保持两遍一致 */
        if (cur_fname_snapshot[0] &&
            os_strcmp(fname, cur_fname_snapshot) == 0 &&
            os_strstr(cur_fpath_snapshot, date_dir) != NULL) {
            continue;
        }
        if (skip_recycling && recy_fname_snapshot[0] &&
            os_strcmp(fname, recy_fname_snapshot) == 0 &&
            os_strcmp(date_dir, recy_dir_snapshot) == 0) {
            continue;
        }
        arr[idx].t_start      = tmval_to_utc(y, mon, day, hh, mm, ss);
        arr[idx].duration     = name_dur;
        arr[idx].event        = ev;
        arr[idx].hh           = hh;
        arr[idx].mm           = mm;
        arr[idx].ss           = ss;
        idx++;
    }
    scan_failed = rec_dir_failed(d);
    rec_dir_close(d);
    if (scan_failed) { RP_FREE(arr); return -1; }

    if (idx > 1)
        qsort(arr, idx, sizeof(scan_item_t), cmp_scan_item);

    *out_arr = arr;
    *out_cnt = idx;
    return 0;
}

static int scan_day_dir(const char *date, scan_item_t **arr, uint32_t *count)
{
    return scan_day_dir_raw(date, arr, count, 1);
}

/* 只在本编译单元展开，避免修改 CDK 自动生成的工程列表。 */
#include "rec_day_index.h"

/* 仅回放扫描做有限重试，不能把扫描失败当成录像列表为空。 */
static int pb_scan_day_retry(const char *date_dir, scan_item_t **arr, uint32_t *cnt)
{
    for (int n = 0; n < 3; ++n) {
        if (scan_day_dir(date_dir, arr, cnt) == 0) return 0;
        os_printf(KERN_WARNING "pb: directory scan failed %s (attempt %d/3)\n",
                  date_dir, n + 1);
        if (n < 2) os_sleep_ms(100);
    }
    return -1;
}

/* 在指定日期目录内查找 "下一个文件".
 * @param date_dir    当前日期 "YYYYMMDD"
 * @param cur_fname   当前文件名
 * @param next_fname  [out] 下一个文件名 (需要 ≥ FILE_NAME_LEN+1)
 * @param next_t0     [out] 下一个文件的起始时间
 * @return 0 成功, -1 没有下一个
 */
static int find_next_in_day(const char *date_dir, const char *cur_fname,
                            char *next_fname, uint32_t *next_t0)
{
    scan_item_t *arr = NULL;
    uint32_t cnt = 0;
    if (pb_scan_day_retry(date_dir, &arr, &cnt) != 0 || cnt == 0) {
        if (arr) RP_FREE(arr);
        return -1;
    }
    int  ret = -1;
    char tmp[FILE_NAME_LEN + 1];
    for (uint32_t i = 0; i < cnt; i++) {
        build_fname_from_item(&arr[i], tmp, sizeof(tmp));
        if (os_strcmp(tmp, cur_fname) > 0) {
            build_fname_from_item(&arr[i], next_fname, FILE_NAME_LEN + 1);
            *next_t0 = arr[i].t_start;
            ret = 0;
            break;
        }
    }
    RP_FREE(arr);
    return ret;
}

/* =========================================================================
 * SD 卡管理
 * ========================================================================= */

int sd_get_capacity(uint32_t *total, uint32_t *free)
{
    if (total) *total = 0;
    if (free) *free = 0;
    if (rec_sd_enter() != 0) return g_sd_formatting ? -3 : -1;
    int ret = sd_get_capacity_raw(total, free);
    rec_sd_leave();
    return ret;
}
static int sd_get_capacity_raw(uint32_t *total, uint32_t *free)
{
    uint32_t tot_mb = 0, free_mb = 0;
    /* osal_fatfsfree 内部已做 扇区 / 2 / 1024 换算, 返回单位就是 MB */
    FRESULT res = osal_fatfsfree("0:", &tot_mb, &free_mb);
    if (res != FR_OK) {
        os_printf(KERN_ERR "sd_get_capacity: err=%d\n", res);
        if (total) *total = 0;
        if (free) *free = 0;
        return -1;
    }
    if (total) *total = tot_mb;
    if (free) *free = free_mb;
    return 0;
}

/* 循环录像: 找 REC 目录下最早的日期, 再删其最早文件 */
/* 真正的回收逻辑, 必须在 g_recycle_lock 持锁状态下调用. */
static int rec_recycle_oldest_locked(void)
{
    if (g_sd_formatting || rec_pb_is_active()) return -1;
    void *root = rec_dir_open(REC_ROOT_PATH);
    if (!root) {
        os_printf(KERN_ERR "rec_recycle: opendir(%s) fail, REC root not exist\n", REC_ROOT_PATH);
        return -1;
    }

    char oldest_dir[16] = {0};
    void *fno;
    uint32_t total_dirs = 0, valid_date_dirs = 0;
    while ((fno = rec_dir_read(root)) != NULL) {
        char *name = osal_dirent_name(fno);
        if (!name || !osal_dirent_isdir(fno)) continue;
        total_dirs++;
        uint16_t y; uint8_t mon, day;
        if (parse_dirname(name, &y, &mon, &day) != 0) {
            os_printf(KERN_INFO "rec_recycle: skip dir '%s' (not YYYYMMDD)\n", name);
            continue;
        }
        valid_date_dirs++;
        int has = day_dir_has_mp4(name);
        if (has < 0) { rec_dir_close(root); return -1; }
        if (!has) continue; /* 只探测是否存在，避免每删一段就排序所有日期的录像。 */
        if (oldest_dir[0] == '\0' || os_strcmp(name, oldest_dir) < 0) {
            os_strncpy(oldest_dir, name, sizeof(oldest_dir) - 1);
            oldest_dir[sizeof(oldest_dir) - 1] = '\0';
        }
    }
    int root_failed = rec_dir_failed(root);
    rec_dir_close(root);
    if (root_failed) return -1;

    if (oldest_dir[0] == '\0') {
        os_printf(KERN_ERR "rec_recycle: no YYYYMMDD dir under %s "
                  "(total dirs=%u, valid=%u). SD may be filled by other files.\n",
                  REC_ROOT_PATH, total_dirs, valid_date_dirs);
        return -1;
    }

    /* 每次只删除最旧的一段已发布 MP4，临时文件交由恢复任务处理。 */
    scan_item_t *arr = NULL; uint32_t cnt = 0;
    if (scan_day_dir(oldest_dir, &arr, &cnt) == 0 && cnt > 0) {
        /* === 路径 1: 删除最旧的一个 MP4 === */
        char full_path[96];
        char fn0[FILE_NAME_LEN + 1];
        build_fname_from_item(&arr[0], fn0, sizeof(fn0));
        os_snprintf(full_path, sizeof(full_path), "%s/%s/%s",
                    REC_ROOT_PATH, oldest_dir, fn0);

        /* 公告"我要开始删 fn0 了": scan_day_dir 看到此名+目录会跳过, 这样
         * 后续 rec_list_get / pb_locate_file 不会返回这个即将消失的文件 */
        os_strncpy((char *)g_recycling_date_dir, oldest_dir,
                   sizeof(g_recycling_date_dir) - 1);
        os_strncpy((char *)g_recycling_fname,    fn0,
                   sizeof(g_recycling_fname)    - 1);

        FRESULT res = rec_idx_change(full_path, NULL);
        if (res == FR_OK) {
            os_printf(KERN_INFO "rec_recycle: deleted %s\n", full_path);
        } else {
            os_printf(KERN_ERR "rec_recycle: unlink %s fail res=%d\n", full_path, res);
        }
        /* 清空"正在删除"快照, 让 scan_day_dir 立即恢复正常 */
        g_recycling_fname[0]    = '\0';
        g_recycling_date_dir[0] = '\0';

        /* 删除最后一段后只尝试移除空目录，目录里其它文件不受影响。 */
        if (res == FR_OK && cnt == 1) {
            char full_dir[64];
            os_snprintf(full_dir, sizeof(full_dir), "%s/%s", REC_ROOT_PATH, oldest_dir);
            rec_idx_empty_day(oldest_dir);
            osal_unlink_dir(full_dir, 0);   /* 空了才会删成功, 不空就保留 */
        }
        RP_FREE(arr);
        return (res == FR_OK) ? 0 : -1;
    }
    if (arr) RP_FREE(arr);

    /* 没有可回收 MP4 不代表目录为空，不能把当前录像、临时文件或未知文件当垃圾。 */
    return -1;
}

/* 对外入口: 加 mutex 串行化, 避免预删除线程和 mp4_encode_thread 兜底删除并发.
 * 在 g_recycle_lock_inited 之前调用 (启动阶段第一次 cleanup) 直接放行,
 * 因为那时只有 bootstrap 一个线程在跑, 不会有并发. */
static int rec_recycle_oldest(void)
{
    if (!g_recycle_lock_inited) {
        return rec_recycle_oldest_locked();
    }
    os_mutex_lock(&g_recycle_lock, osWaitForever);
    int ret = (g_sd_formatting || rec_pb_is_active()) ? -1 : rec_recycle_oldest_locked();
    os_mutex_unlock(&g_recycle_lock);
    return ret;
}

/* 前向声明 */
int _rp_record_start(uint8_t event_type);
int _rp_record_stop(void);
static int rec_start_raw(uint8_t event_type);
static int rec_stop_raw(void);
static void rec_alarm_check(void);

/* 把 UNSYNC 目录里的 MP4 改名到真实时间目录.
 * 仅用于"本会话时间同步"场景, 按 (g_sync_utc, g_sync_boot_sec) 还原真实时间.
 * 跨重启残留 (boot_sec 基准已丢, 时间无法还原) 的文件不走这里, 由调用方直接删除.
 * @param unsync_fname  UNSYNC 目录下文件名, 形如 "<boot_sec>_Eee_dd.MP4"
 * @return 0 成功 / -1 跳过
 *
 * 文件名映射: <boot_sec>_Eee_dd.EXT  →  YYYYMMDD/HHMMSS_Eee_dd.EXT
 *   真实UTC = g_sync_utc - (g_sync_boot_sec - boot_sec) */
static int rec_unsync_migrate_one(const char *unsync_fname)
{
    /* 解析 <boot_sec>_Eee_dd.EXT */
    uint32_t boot_sec = 0; int ev = 0, dur = 0;
    char ext[8] = {0};
    /* 先取扩展名 */
    int nlen = os_strlen(unsync_fname);
    if (nlen < 7) return -1;
    const char *dot = NULL;
    for (int i = nlen - 1; i >= 0; i--) { if (unsync_fname[i] == '.') { dot = unsync_fname + i; break; } }
    if (!dot) return -1;
    os_strncpy(ext, dot, sizeof(ext) - 1);

    if (sscanf(unsync_fname, "%u_E%02d_%d", &boot_sec, &ev, &dur) != 3)
        return -1;

    /* 算目标日期目录 + HHMMSS.
     * 真实 UTC = sync_utc - (sync_boot_sec - boot_sec).
     * boot_sec 早于 sync_boot_sec, 差值为正 */
    char date_dir[16];
    char hms[8];
    int32_t delta_s = (int32_t)(g_sync_boot_sec - boot_sec);
    if (delta_s < 0) delta_s = 0;       /* 防御: 不应为负 */
    uint32_t real_utc = g_sync_utc - (uint32_t)delta_s;
    utc_to_dir(real_utc, date_dir, sizeof(date_dir));
    utc_to_time(real_utc, hms, sizeof(hms));

    /* 确保目标日期目录存在 */
    char dst_dir[48];
    os_snprintf(dst_dir, sizeof(dst_dir), "%s/%s", REC_ROOT_PATH, date_dir);
    void *d = rec_dir_open(dst_dir);
    if (d) rec_dir_close(d);
    else   osal_fmkdir(dst_dir);

    /* 源 / 目标完整路径 */
    char src_path[96], dst_path[96];
    os_snprintf(src_path, sizeof(src_path), "%s/%s", REC_UNSYNC_DIR, unsync_fname);
    os_snprintf(dst_path, sizeof(dst_path), "%s/%s_E%02d_%02d%s",
                dst_dir, hms, ev, dur, ext);

    if (g_sd_formatting) return -1;
    FRESULT r = rec_idx_change(src_path, dst_path);
    if (r != FR_OK) {
        os_printf(KERN_ERR "rec_unsync_migrate: rename %s -> %s fail res=%d\n",
                  src_path, dst_path, r);
        return -1;
    }
    os_printf(KERN_INFO "rec_unsync_migrate: %s -> %s\n", src_path, dst_path);
    return 0;
}

/* 遍历 UNSYNC 目录, 把里面所有文件按真实时间搬到日期目录.
 * 仅用于"本会话时间同步"场景 (g_sync_utc/g_sync_boot_sec 有效).
 * 调用方保证此时没有任何 UNSYNC 文件处于打开状态 (录像已停/已切到真实目录).
 * 跨重启残留场景不调本函数, 由调用方直接删整个 UNSYNC 目录. */
static void rec_unsync_migrate_all(void)
{
    static char cursor[FILE_NAME_LEN + 8];
    static uint32_t generation = 0xffffffffU;
    if (generation != g_sd_generation) { cursor[0] = 0; generation = g_sd_generation; }
    unsigned moved = 0, total = 0;
    /* 按名字推进游标，每轮最多八段；重名/坏卡不能卡住其它待迁移文件。 */
    while (total < 8 && !g_sd_formatting) {
        void *dir = rec_dir_open(REC_UNSYNC_DIR);
        if (!dir) {
            FILINFO info;
            FRESULT res = f_stat(REC_UNSYNC_DIR, &info);
            if (res != FR_NO_FILE && res != FR_NO_PATH) g_unsync_need_migrate = 1;
            break;
        }
        char next[sizeof(cursor)] = {0};
        void *ent;
        while ((ent = rec_dir_read(dir)) != NULL) {
            const char *fn = osal_dirent_name(ent);
            if (osal_dirent_isdir(ent) || !rec_has_ext(fn, REC_EXT_NAME) ||
                os_strlen(fn) >= sizeof(next) || os_strcmp(fn, cursor) <= 0) continue;
            if (!next[0] || os_strcmp(fn, next) < 0) os_strcpy(next, fn);
        }
        int failed = rec_dir_failed(dir);
        rec_dir_close(dir);
        if (failed) { g_unsync_need_migrate = 1; break; }
        if (!next[0]) {
            /* 从头重试此前失败的文件；成功迁移后空目录会被下面删除。 */
            if (cursor[0] || total) g_unsync_need_migrate = 1;
            cursor[0] = 0;
            break;
        }
        os_strcpy(cursor, next);
        ++total;
        if (!rec_unsync_migrate_one(next)) ++moved;
        else g_unsync_need_migrate = 1;
    }
    if (total >= 8 || g_sd_formatting) g_unsync_need_migrate = 1;
    if (!g_sd_formatting) osal_unlink_dir(REC_UNSYNC_DIR, 0);  /* 只删除空目录。 */
    if (total) os_printf(KERN_INFO "rec_unsync_migrate: total=%u moved=%u\n", total, moved);
}

static void sd_format_reply(void *handle, int failed)
{
    Tcis_FormatExtStorageResp resp;
    os_memset(&resp, 0, sizeof(resp));
    resp.storage = 0;
    resp.result = failed ? 1 : 0;
    TciSendCmdResp(handle, TCI_CMD_FORMATEXTSTORAGE_RESP, (char *)&resp, sizeof(resp));
}
void sd_format_handle(void *arg)
{
    rec_mode_t saved_mode = g_rec_mode;
    int failed = 1;
    BYTE *work = NULL;
    unsigned wait;
    os_printf(KERN_INFO "sd_format: quiesce SD users\n");
    /* 先等已经获准的查询/后台任务退出，再停止可能由它们启动的录像与回放。 */
    for (wait = 0; wait < 1000; ++wait) {
        os_mutex_lock(&g_sd_gate, osWaitForever);
        uint32_t users = g_sd_users;
        os_mutex_unlock(&g_sd_gate);
        if (!users) break;
        os_sleep_ms(10);
    }
    if (wait == 1000) { saved_mode = g_rec_mode; goto done; }
    saved_mode = g_rec_mode;  /* 已进入的模式指令可能刚执行完，使用最终模式。 */
    g_rec_mode = REC_MODE_OFF;
    _rp_record_stop();
    pb_stop();
    extern uint32_t mp4_demux_active_workers(void);
    for (wait = 0; wait < 1000 && (g_rec_file_closing || rec_pb_is_active() || mp4_demux_active_workers() || mp4_encode_active_workers()); ++wait)
        os_sleep_ms(10);
    if (g_rec_file_closing || rec_pb_is_active() || mp4_demux_active_workers() || mp4_encode_active_workers()) {
        os_printf(KERN_ERR "sd_format: close timeout, format cancelled\n");
        goto done;
    }
    rec_idx_reset();  /* 格式化前释放缓存，失败也不能沿用旧卷的列表。 */
    work = _os_malloc_psram(FF_MAX_SS);
    if (!work) goto done;
    /* 全部应用 IO 已停止；先隔离旧卷缓存，避免后台把旧 FAT 回写到新文件系统。 */
    extern void fatfs_cache_discard(void);
    extern bool fatfs_register(void);
    fatfs_cache_discard();
    if (fatfs_prepare_remount() != FR_OK) goto done;
    /* 45229 FatFS takes MKFS_PARM rather than the old five-argument API. */
    MKFS_PARM mkfs = {0};
    mkfs.fmt = FM_ANY;
    FRESULT res = f_mkfs("0:", &mkfs, work, FF_MAX_SS);
    os_printf(KERN_INFO "sd_format: f_mkfs=%d\n", res);
    /* 成败都重新挂载检测，不能沿用格式化前的容量缓存。 */
    int mounted = fatfs_register() == 0;
    if (res != FR_OK || !mounted) goto done;
    res = osal_fmkdir(REC_ROOT_PATH);
    if (res != FR_OK && res != FR_EXIST) goto done;
    uint32_t total, free_mb;
    if (sd_get_capacity_raw(&total, &free_mb) || !total) goto done;
    os_memset((void *)g_finalize_pending, 0, sizeof(g_finalize_pending));
    os_memset(&g_curfile, 0, sizeof(g_curfile));
    g_unsync_need_migrate = 0;
    g_rec_unsync_mode = !time_sync_flag;
    g_sd_unrecoverable = 0;
    g_create_null_streak = 0;
    ++g_sd_generation;
    failed = 0;
    os_printf(KERN_INFO "sd_format: ready total=%u free=%uMB\n", total, free_mb);
done:
    if (work) _os_free_psram(work);
    g_rec_mode = saved_mode;
    if (failed) sd_storage_request_recovery();
    os_mutex_lock(&g_sd_gate, osWaitForever);
    g_sd_formatting = 0;
    os_mutex_unlock(&g_sd_gate);
    /* 失败也由后台检测卡状态再恢复，不在失败出口强制创建录像。 */
    if (!failed && saved_mode == REC_MODE_ALL_DAY) _rp_record_start(ECEVENT_NONE);
    sd_format_reply(arg, failed);
}
void sd_format(void *arg)
{
    if (!g_rp_inited) { sd_format_reply(arg, 1); return; }
    os_mutex_lock(&g_sd_gate, osWaitForever);
    if (g_sd_formatting || g_sd_fault_pending) {
        os_mutex_unlock(&g_sd_gate);
        sd_format_reply(arg, 1);
        return;
    }
    g_sd_formatting = 1;
    os_mutex_unlock(&g_sd_gate);
    /* 动态任务不引用调用者栈上的 os_task；重复命令不会创建第二个格式化任务。 */
    if (!os_task_create("sd_format", (os_task_func_t)sd_format_handle, arg,
                        OS_TASK_PRIORITY_NORMAL, 0, NULL, 8192)) {
        os_mutex_lock(&g_sd_gate, osWaitForever);
        g_sd_formatting = 0;
        os_mutex_unlock(&g_sd_gate);
        sd_format_reply(arg, 1);
    }
}

/* =========================================================================
 * 模式控制
 * ========================================================================= */

static int rec_set_mode_raw(rec_mode_t mode);
int rec_set_mode(rec_mode_t mode)
{
    if (mode < REC_MODE_OFF || mode > REC_MODE_ALL_DAY) return -1;
    if (!g_rp_inited) { g_rec_mode = mode; return 0; }  /* 初始化前只保存配置。 */
    if (rec_sd_enter()) return -1;
    os_mutex_lock(&g_rec_control, osWaitForever);
    int ret = rec_set_mode_raw(mode);
    os_mutex_unlock(&g_rec_control);
    rec_sd_leave();
    return ret;
}
static int rec_set_mode_raw(rec_mode_t mode)
{
    if (g_sd_formatting) return -1;

    rec_mode_t old = g_rec_mode;
    g_rec_mode = mode;

    os_printf(KERN_INFO "rec_set_mode: %d -> %d\n", old, mode);

    if (old == mode)
        return 0;

    if (mode == REC_MODE_OFF || mode == REC_MODE_ALARM) {
        rec_stop_raw();
    } else if (mode == REC_MODE_ALL_DAY) {
        rec_start_raw(ECEVENT_NONE);
    }
    return 0;
}

rec_mode_t rec_get_mode(void)
{
    return g_rec_mode;
}

/* =========================================================================
 * 模块初始化
 * ========================================================================= */
/* SDK FatFS mount 标志 (set_fat_ready). 真实挂载入口是 fatfs_test.c 里的 fatfs_register */
extern uint8_t get_fat_isready(void);

/* 后台唯一恢复入口；本函数不能持有 rec_sd_enter 的使用名额。 */
static void rec_sd_recovery_step(void)
{
    static uint32_t last_try_ms;
    if (!g_sd_fault_pending || !g_rp_inited) return;
    os_mutex_lock(&g_sd_gate, osWaitForever);
    if (g_sd_formatting && !g_sd_recovering) {
        os_mutex_unlock(&g_sd_gate);
        return;
    }
    g_sd_formatting = 1;
    g_sd_recovering = 1;
    uint32_t users = g_sd_users;
    os_mutex_unlock(&g_sd_gate);
    uint32_t now = (uint32_t)os_jiffies_to_msecs(os_jiffies());
    if (last_try_ms && (uint32_t)(now - last_try_ms) < 3000U) return;
    last_try_ms = now;
    if (users) {
        os_printf(KERN_WARNING "[SD_RECOVER] wait users=%u\n", users);
        return;
    }
    _rp_record_stop();
    pb_stop();
    if (g_rec_file_closing || mp4_encode_active_workers() ||
        rec_pb_is_active() || mp4_demux_active_workers()) {
        os_printf(KERN_WARNING "[SD_RECOVER] wait close: rec=%u pb=%u\n",
                  mp4_encode_active_workers(), mp4_demux_active_workers());
        return; /* 超时保持封门，绝不强行卸载。 */
    }
    rec_idx_reset();
    /* 内存中的待提交路径不跨卷使用；磁盘上的 .REC 留待原恢复扫描处理。 */
    os_memset((void *)g_finalize_pending, 0, sizeof(g_finalize_pending));
    os_memset(&g_curfile, 0, sizeof(g_curfile));
    uint32_t epoch = g_sd_fault_epoch;
    int ret = fatfs_recover_mount();
    uint32_t total = 0, avail = 0;
    if (!ret) {
        FRESULT mkdir_ret = osal_fmkdir(REC_ROOT_PATH);
        if (mkdir_ret != FR_OK && mkdir_ret != FR_EXIST) ret = mkdir_ret;
    }
    if (!ret) ret = sd_get_capacity_raw(&total, &avail);
    if (ret || !total || epoch != g_sd_fault_epoch) {
        os_printf(KERN_WARNING "[SD_RECOVER] mount failed ret=%d total=%u\n", ret, total);
        return;
    }
    g_sd_unrecoverable = 0;
    g_create_null_streak = 0;
    ++g_sd_generation;
    os_mutex_lock(&g_sd_gate, osWaitForever);
    uint32_t flags = disable_irq();
    if (epoch == g_sd_fault_epoch) {
        g_sd_fault_pending = 0;
        g_sd_recovering = 0;
        g_sd_formatting = 0;
    }
    enable_irq(flags);
    os_mutex_unlock(&g_sd_gate);
    if (!g_sd_fault_pending)
        os_printf(KERN_INFO "[SD_RECOVER] ready total=%u free=%uMB\n", total, avail);
    /* 保留用户录像模式；全天录像由正常后台循环重启，回放等待 APP 新请求。 */
}

/* 启动检测任务: 周期检查 SD 卡是否就绪, 就绪后根据当前模式启动录像 */
static void rec_bootstrap_thread(void *arg)
{
    (void) arg;
    os_printf(KERN_INFO "rec_bootstrap: wait SD card ready...\n");
    int n = 0;
    /* 等待条件分两层:
     *   1. SD 必须就绪 (fat_ready + 有容量): 没卡没法写, 必须死等
     *   2. 时间同步: 等最多 REC_TIME_SYNC_WAIT_MS, 超时进 UNSYNC 模式
     *      (断网启动也要录, 不能因为没同步就永远不录) */
    uint32_t wait_ms = 0;
    while (1) {
        os_sleep_ms(1000);
        rec_sd_recovery_step();
        if (g_sd_formatting) continue;

        if(GetNetworkState() == 1){
            continue;
        }
        
        wait_ms += 1000;
        uint32_t total_mb = 0, free_mb = 0;
        uint8_t fat_ready = get_fat_isready();
        uint8_t sd_ok = (fat_ready && sd_get_capacity(&total_mb, &free_mb) == 0 && total_mb > 0);

        if (sd_ok && time_sync_flag) {
            /* SD 就绪 + 时间已同步: 正常模式启动 */
            os_printf(KERN_INFO "rec_bootstrap: SD ready + time synced, total=%dMB free=%dMB\n",
                      total_mb, free_mb);
            g_rec_unsync_mode = 0;
            break;
        }
        if (sd_ok && !time_sync_flag && wait_ms >= REC_TIME_SYNC_WAIT_MS) {
            /* SD 就绪但时间同步超时: 进 UNSYNC 模式, 先录起来, 文件录到
             * UNSYNC 目录用开机毫秒命名, 等 NTP 同步后再 migrate 到真实时间 */
            os_printf(KERN_WARNING "rec_bootstrap: SD ready but time NOT synced after %ums, "
                                   "enter UNSYNC mode (record to %s, migrate on time-sync)\n",
                      (unsigned)wait_ms, REC_UNSYNC_DIR);
            g_rec_unsync_mode = 1;
            break;
        }
        if(++n % 5 == 0){
            os_printf(KERN_INFO "waitting SDcard ready, time_sync=%d, fat_ready=%d, total_mb=%d, free_mb=%d\n",
                                                    time_sync_flag, fat_ready, total_mb, free_mb);
        }
    }

    while (rec_sd_enter() != 0) { rec_sd_recovery_step(); os_sleep_ms(10); }
    /* SD 就绪, 建立 REC 根目录 */
    void *dir = rec_dir_open(REC_ROOT_PATH);
    if (!dir) {
        osal_fmkdir(REC_ROOT_PATH);
    } else {
        rec_dir_close(dir);
    }

    /* 处理上次会话残留的 UNSYNC 文件 (上次没等到时间同步就断电, boot_sec 基准
     * 已随重启丢失, 时间无法还原). 直接整目录删除 (不再搬占位日期):
     * 这些文件时间不可知, 留着也无法在 APP 时间轴上正确呈现, 删掉更干净.
     * 注意: 必须在本次录像启动 (可能又往 UNSYNC 写) 之前做完. */
    {
        void *ud = rec_dir_open(REC_UNSYNC_DIR);
        if (ud) {
            rec_dir_close(ud);
            os_printf(KERN_WARNING "rec_bootstrap: found leftover UNSYNC dir from previous session, "
                                   "time unrecoverable, deleting it\n");
            osal_unlink_dir(REC_UNSYNC_DIR, 1 /*递归删目录内所有文件*/);
        }
    }

    /* 启动前主动清理: 若剩余容量不足, 删旧文件腾空间;
     * 删完还不足(或没有旧文件可删) 则禁用录像并打印警告 */
    uint32_t tot_mb = 0, free_mb = 0;
    sd_get_capacity(&tot_mb, &free_mb);
    os_printf(KERN_INFO "rec_bootstrap: sd total=%dMB free=%dMB, threshold=%dMB\n",
              tot_mb, free_mb, REC_LOOP_REMAIN_MB);

    int cleanup_try = 0;
    /* 单文件粒度: 每次循环删除一个 MP4，容量够了立即停止。
     * 上限 5000 防死循环, 一般场景几十次就能达标 */
    while (!g_sd_formatting && free_mb < REC_LOOP_REMAIN_MB && cleanup_try++ < 5000) {
        if (rec_recycle_oldest() != 0) {
            os_printf(KERN_ERR "rec_bootstrap: no old files to delete, free=%dMB\n", free_mb);
            break;
        }
        sd_get_capacity(&tot_mb, &free_mb);
    }

    if (free_mb < REC_LOOP_REMAIN_MB) {
        os_printf(KERN_ERR "rec_bootstrap: SD free(%dMB) < threshold(%dMB), recording DISABLED\n",
                  free_mb, REC_LOOP_REMAIN_MB);
        if (free_mb == 0 && cleanup_try > 0) {
            os_printf(KERN_ERR "rec_bootstrap: SD totally full but no recyclable record under %s.\n",
                      REC_ROOT_PATH);
            os_printf(KERN_ERR "rec_bootstrap: SD card likely contains other files. Please format SD or clean up manually.\n");
        } else {
            os_printf(KERN_ERR "rec_bootstrap: please insert a larger SD card\n");
        }
        goto rec_bootstrap_background; /* 保持监测，换卡/格式化后仍可恢复。 */
    }
    if (cleanup_try > 0) {
        os_printf(KERN_INFO "rec_bootstrap: cleanup done, free=%dMB\n", free_mb);
    }

    /* 按模式启动录像 */
    if (g_rec_mode == REC_MODE_ALL_DAY) {
        if (_rp_record_start(ECEVENT_NONE) == 0) {
            os_printf(KERN_INFO "rec_bootstrap: recording started (all-day)\n");
        } else {
            os_printf(KERN_ERR "rec_bootstrap: record start failed, retry in 2s\n");
            os_sleep_ms(2000);
            _rp_record_start(ECEVENT_NONE);
        }
    } else if (g_rec_mode == REC_MODE_ALARM) {
        os_printf(KERN_INFO "rec_bootstrap: alarm mode, wait trigger\n");
    }

rec_bootstrap_background:
    rec_sd_leave();
    /* === 预删除常驻循环 ===
     * 周期性检查 SD 容量, 在 free_mb 触及"预删除阈值"时主动删 1 个最旧 MP4,
     * 把"运行时切文件同步删多文件"的阻塞从 mp4_encode_thread 卸载到这里.
     *
     * 协调点:
     *   - g_sd_formatting: sd_format 进行中, 让位避免和 f_mkfs 抢 SD
     *   - g_pb.thread_alive: 回放进行中, 不删 (可能正读最旧文件)
     *   - g_rec_mode == REC_MODE_OFF: 录像关闭, 没必要预删
     *
     * 触发线 REC_CLEANUP_HIGH_MB = REC_LOOP_REMAIN_MB + 256MB:
     *   主码流 (1024) → 1280MB 开始预删, 给 rec_create_file_cb 留 256MB 余量
     *   子码流  (256) → 512MB  开始预删
     *
     * 单轮只删 1 个文件, unlink ~500ms 内完成, 不会和录像争 SD 太久.
     * 检查间隔 30s: 1080P ~15MB/min 录, 30s 增量 7.5MB, 远低于 256MB 余量,
     * 不会在检查间隙撑爆阈值. */
    #define REC_CLEANUP_CHECK_MS  30000U
    #define REC_CLEANUP_HIGH_MB   (REC_LOOP_REMAIN_MB + 256U)

    os_printf(KERN_INFO "rec_bootstrap: enter cleanup loop, check=%us trigger=%uMB\n",
              (unsigned)(REC_CLEANUP_CHECK_MS / 1000U),
              (unsigned)REC_CLEANUP_HIGH_MB);

    /* 主循环: 3s 间隔, 兼顾 SD 不可恢复故障的 3s 告警节奏.
     * 预删除按 REC_CLEANUP_CHECK_MS 间隔 (用累计 tick 控制), 不每轮都跑 */
    #define REC_TICK_MS         3000
    int32_t cleanup_acc_ms = 0;

    uint8_t maintenance_entered = 0;
    unsigned pending_scan_ticks = 0;
    while (1) {
        if (maintenance_entered) { rec_sd_leave(); maintenance_entered = 0; }
        os_sleep_ms(REC_TICK_MS);
        if (g_sd_unrecoverable && !g_sd_fault_pending) sd_storage_request_recovery();
        rec_sd_recovery_step();
        if (rec_sd_enter()) continue;
        maintenance_entered = 1;

        /* Alarm clips stop on monotonic elapsed time. Check before background IO;
         * the 3s maintenance cadence and task scheduling may add a short tail. */
        rec_alarm_check();

        /* 每轮最多处理一个刚关闭文件。这是低优先级任务，
         * 不再由 APP 列表查询或 mp4_encode 切片线程承担 mdhd IO。 */
        rec_finalize_pending_run();
        rec_idx_background();  /* 索引补建/整理放在低优先级任务，不放在编码线程。 */
        if (++pending_scan_ticks >= 10) { pending_scan_ticks = 0; rec_pending_scan(); }
        if (g_sd_formatting) continue;

        /* === NTP 时间同步 → 迁移 UNSYNC 录像到真实时间 (方案 A: 不打断活跃录像) ===
         * 关键教训: 旧实现在这里 _rp_record_stop() + migrate + _rp_record_start(),
         * 但 msi_destroy 是异步引用计数销毁 —— stop 返回时 mp4_encode 线程未必退出,
         * evt 未必还活着. 紧接着的 migrate/restart 在 os_event_del/free 与线程
         * os_event_wait 之间踩到 use-after-free, 触发 assert "evt->magic" 崩溃.
         * 新策略绝不主动 stop/destroy 正在录的 msi (见下方幂等检查 + 迁移). */
        if (g_sync_pending) {
            g_sync_pending = 0;
            os_printf(KERN_INFO "rec_bootstrap: time synced (utc=%u boot_sec=%u)\n",
                      (unsigned)g_sync_utc, (unsigned)g_sync_boot_sec);
        }

        /* 幂等检查 (不依赖 g_sync_pending 边沿, 每轮都查): 时间已同步但仍处于
         * UNSYNC 模式时, 推动退出. 覆盖两种情况:
         *   - ALL_DAY 录像中: 不打断, 等 mp4_encode 线程下次切文件时 rec_create_file_cb
         *     自动走真实目录并自己退出 UNSYNC 模式 (这里什么都不用做, 只是兜底:
         *     万一切文件那刻没退成, 下面"无活跃录像"条件迟早会命中).
         *   - 无活跃录像 (模式 OFF / 报警录完已 _rp_record_stop, g_rec_msi=NULL):
         *     不会再触发 rec_create_file_cb, 必须在这里主动退出模式 + 排迁移.
         *     此时没有任何线程在写 UNSYNC, rename 安全. */
        if (g_rec_unsync_mode && time_sync_flag) {
            if (!g_rec_msi && !g_rec_file_closing) {
                g_rec_unsync_mode     = 0;
                g_unsync_need_migrate = 1;
                os_printf(KERN_INFO "rec_bootstrap: no active UNSYNC rec, exit UNSYNC mode, "
                                   "migrate pending\n");
            }
            /* 否则: ALL_DAY 录像活跃中, 交给 rec_create_file_cb 在切文件时退出,
             * 不在此打断 (避免 msi_destroy 异步竞态) */
        }

        /* === 执行 UNSYNC 迁移 (标志由 rec_create_file_cb 或上面置位) ===
         * 到这里保证: 已退出 UNSYNC 模式, 编码线程 (若在录) 已在真实目录写新文件,
         * UNSYNC 目录里的旧文件全部 fclose, 可安全 rename. 不碰任何 msi. */
        if (g_unsync_need_migrate && !g_rec_file_closing) {
            g_unsync_need_migrate = 0;
            os_printf(KERN_INFO "rec_bootstrap: migrating UNSYNC files to real time dir\n");
            rec_unsync_migrate_all();
            os_printf(KERN_INFO "rec_bootstrap: UNSYNC migrate done (no record interruption)\n");
        }

        /* === 优先处理: SD 不可恢复故障监控 === */
        if (g_sd_unrecoverable) {
            /* 下一轮先归还使用名额，再由统一恢复入口处理，不再凭容量直接复录。 */
            sd_storage_request_recovery();
            continue;
        }

        /* 无活跃录像时持续重试，覆盖格式化后收尾尚未完成、启动时卡满等情况。 */
        if (g_rec_mode == REC_MODE_ALL_DAY && !g_rec_msi && !g_rec_file_closing) {
            uint32_t cap, avail;
            if (!sd_get_capacity_raw(&cap, &avail) && cap && avail >= 32)
                _rp_record_start(ECEVENT_NONE);
        }
        /* === 30s 间隔的预删除 (累加 tick 实现) === */
        cleanup_acc_ms += REC_TICK_MS;
        if (cleanup_acc_ms < REC_CLEANUP_CHECK_MS) {
            continue;
        }
        cleanup_acc_ms = 0;

        if (g_sd_formatting) {
            os_printf(KERN_INFO "rec_cleanup: skip (sd_format in progress)\n");
            continue;
        }
        if (g_rec_mode == REC_MODE_OFF) {
            continue;
        }
        if (rec_pb_is_active()) {
            /* 回放期间不删, 避免删到正在播的旧文件. 改为 INFO 级别避免刷屏 */
            os_printf(KERN_INFO "rec_cleanup: skip (playback active)\n");
            continue;
        }

        uint32_t tot = 0, fre = 0;
        if (sd_get_capacity(&tot, &fre) != 0 || tot == 0) {
            os_printf(KERN_WARNING "rec_cleanup: sd_get_capacity failed, skip\n");
            continue;
        }
        if (fre >= REC_CLEANUP_HIGH_MB) {
            /* 容量充足, 啥也不做. 不打印避免日志噪音 */
            continue;
        }

        /* 触发预删除. 再次检查 g_sd_formatting 缩小竞态窗口 */
        if (g_sd_formatting) continue;

        uint32_t t0_ms = (uint32_t)os_jiffies_to_msecs(os_jiffies());
        int rc = rec_recycle_oldest();
        uint32_t dt_ms = (uint32_t)os_jiffies_to_msecs(os_jiffies()) - t0_ms;

        uint32_t fre2 = 0;
        sd_get_capacity(&tot, &fre2);
        os_printf(KERN_INFO "rec_cleanup: free %u→%u MB, rc=%d, cost=%u ms\n",
                  (unsigned)fre, (unsigned)fre2, rc, (unsigned)dt_ms);

        if (rc != 0) {
            /* rec_recycle_oldest 返回 -1: 没文件可删. acc 拉到 -90000, 实际等
             * 90+30=120s 再触发下次预删除, 避免空转刷日志 */
            cleanup_acc_ms = -90000;
        }
    }
}

int rec_playback_init(void)
{
    if (g_rp_inited)
        return 0;

    os_mutex_init(&g_sd_gate);
    os_mutex_init(&g_rec_control);
    rec_idx_init();

    /* 初始化回收 mutex. 必须在启动 rec_bootstrap_thread (含预删除循环) 之前完成,
     * 也必须在 rec_create_file_cb 第一次可能调 rec_recycle_oldest 之前 */
    if (!g_recycle_lock_inited) {
        os_mutex_init(&g_recycle_lock);
        g_recycle_lock_inited = 1;
    }

    /* sd_pb_recv msi 在此创建一次, 永不销毁, 避免 pb_start/stop 反复创建
     * 同名 msi 时触发 SDK 的 "不要重复打开" 路径导致 double destroy */
    struct msi *recv = msi_new("sd_pb_recv", 16, NULL);
    if (!recv) {
        os_mutex_del(&g_rec_control);
        os_mutex_del(&g_sd_gate);
        return -1;
    }
    recv->action = NULL;
    recv->enable = 1;

    g_rp_inited = 1;  /* 所有准入锁和接收队列就绪后才对外开放。 */

    /* 启动 bootstrap 任务: 等 SD 卡就绪后再真正开始录像;
     * 启动完后进入常驻循环, 兼做: 预删除 / SD 不可恢复故障监控 / UNSYNC 录像迁移.
     * 栈用量按最深路径估 (这几条路径互斥, 取最大):
     *   - 预删除 rec_recycle_oldest + FATFS ~1.1KB
     *   - UNSYNC 迁移 rec_unsync_migrate (sscanf/snprintf/f_rename) ~0.95KB
     * 原 3072 算下来够, 但 newlib sscanf / FATFS f_rename 嵌套真实栈深不确定,
     * 提到 4096 留 ~2KB 余量, 栈溢出是致命的, 多花 1KB SRAM 值得.
     * 优先级 BELOW_NORMAL 故意低于 mp4_encode (ABOVE_NORMAL), unlink/migrate 时
     * 不抢写卡 CPU, mp4_encode 可继续消费 fbq */
    if (!os_task_create("rec_bootstrap", rec_bootstrap_thread, NULL,
                        OS_TASK_PRIORITY_BELOW_NORMAL, 0, NULL, 4096)) {
        g_rp_inited = 0;
        msi_destroy(recv);
        os_mutex_del(&g_rec_control);
        os_mutex_del(&g_sd_gate);
        return -1;
    }

    os_printf(KERN_INFO "rec_playback_init: ok, mode=%d, sd_pb_recv=%p\n",
              g_rec_mode, recv);
    return 0;
}

/* 向后兼容 (外部若调用不会出错) */
int rec_index_rebuild(void)
{
    rec_idx_reset();  /* 下次查询重新核对目录，并排队补建当天索引。 */
    return 0;
}

/* =========================================================================
 * 录像启停
 * ========================================================================= */

/* mp4 encode 模块的 create_file 回调: 生成新文件并返回 FILE * */
static void *rec_create_file_cb(struct file_process *fp, char *file_name, char *file_path, uint32_t file_size)
{
    /* 上一段由 end_encode 投递；切片线程不再读旧文件、按大小误删或修复索引。 */
    if (g_sd_formatting || g_sd_fault_pending) return NULL;

    /* 检查容量, 循环删旧 */
    uint32_t total_mb = 0, free_mb = 0;
    sd_get_capacity(&total_mb, &free_mb);
    if (total_mb == 0) {
        os_printf(KERN_ERR "rec_create_file: sd not ready\n");
        rec_create_fail_inc();
        return NULL;
    }
    int retry = 0;
    while (!rec_pb_is_active() && !g_sd_formatting && free_mb < REC_LOOP_REMAIN_MB && retry++ < 2) {
        if (rec_recycle_oldest() != 0)
            break;
        sd_get_capacity(&total_mb, &free_mb);
    }
    if (free_mb < 32) {
        os_printf(KERN_ERR "rec_create_file: sd full\n");
        rec_create_fail_inc();
        return NULL;
    }

    /* 根据当前模式确定 event 和时长 */
    uint8_t ev = ECEVENT_NONE;
    uint8_t dur = REC_FILE_SEC_ALL_DAY;
    if (g_rec_mode == REC_MODE_ALARM) {
        ev = g_curfile.event;
        dur = REC_FILE_SEC_ALARM;
    } else {
        ev = g_curfile.event;
    }

    char full_dir[48];

    /* SDK 已保留首个有效 I 帧，frame_time 是采集时的低 32 位系统 tick。
     * 同时读取 UTC 和单调时钟，扣除帧在队列/切片收尾中的等待时间。
     * 必须先按毫秒相减再取秒，不能分别截断后相减；gettimeofday 在本工程
     * 已加时区，这里使用未加时区的 os_systime，避免重复偏移。
     * 极短临界区只读时钟，目录操作和写卡均在恢复中断之后执行。 */
    struct timespec utc_now;
    uint32_t irq_flags = disable_irq();
    os_systime(&utc_now);
    uint64_t boot_ticks = os_jiffies();
    uint8_t synced = time_sync_flag;
    enable_irq(irq_flags);
    uint32_t age_ticks = (uint32_t)boot_ticks - fp->frame_time;
    uint64_t age_ms = os_jiffies_to_msecs(age_ticks);
    uint64_t utc_ms = (uint64_t)utc_now.tv_sec * 1000U + utc_now.tv_nsec / 1000000U;
    uint32_t first_utc = (uint32_t)((utc_ms - age_ms) / 1000U);
    uint32_t first_boot_sec = (uint32_t)((os_jiffies_to_msecs(boot_ticks) - age_ms) / 1000U);

    if (g_rec_unsync_mode && !synced) {
        /* === UNSYNC 模式: 时间没同步, 录到 0:/REC/UNSYNC/, 文件名用开机秒 ===
         * 文件名 <boot_sec>_Eee_dd.REC, 关闭校验后发布为 MP4; NTP 同步后由
         * bootstrap_thread migrate 到真实日期目录 */
        os_strncpy(full_dir, REC_UNSYNC_DIR, sizeof(full_dir) - 1);
        full_dir[sizeof(full_dir) - 1] = '\0';
        void *d = rec_dir_open(full_dir);
        if (!d) {
            if (osal_fmkdir(full_dir) != FR_OK) {
                os_printf(KERN_ERR "rec_create_file: mkdir %s fail\n", full_dir);
                rec_create_fail_inc();
                return NULL;
            }
        } else {
            rec_dir_close(d);
        }
        /* 开机秒可能超过六位，不能套用正常 HHMMSS 文件名的 17 字节上限。
         * SDK 提供的 file_name 缓冲区为 64 字节，此处最多使用 24 字节。 */
        uint32_t boot_sec = first_boot_sec;  /* 未校时也使用首帧时间，迁移后不带启动等待偏差。 */
        os_snprintf(file_name, 24, "%u_E%02d_%02d%s", boot_sec, ev, dur, REC_TEMP_EXT);
        os_snprintf(file_path, 64, "%s/%s", full_dir, file_name);
        /* g_curfile.t_start 在 UNSYNC 模式无真实意义, 存 boot_sec 占位 */
        g_curfile.t_start = boot_sec;
    } else {
        /* === 正常模式: 时间已同步, 真实日期目录 + HHMMSS 文件名 === */
        /* UNSYNC→真实时间的切换点: 此刻是 mp4_encode_thread 自然切文件，上一个
         * UNSYNC MP4 已经 fclose，是可安全 rename 的普通文件。
         * 在这里退出 UNSYNC 模式 + 通知 bootstrap 异步迁移. 绝不在此 stop/destroy
         * msi (会触发 mp4_encode 线程 use-after-free), 让本函数正常返回新文件句柄,
         * 编码线程无缝继续录到真实目录. */
        if (g_rec_unsync_mode) {
            g_rec_unsync_mode     = 0;
            g_unsync_need_migrate = 1;
            os_printf(KERN_INFO "rec_create_file: time synced, exit UNSYNC mode, "
                                "new files go to real dir, migrate pending\n");
        }
        time_t now = (time_t)first_utc;
        char   date_dir[16];
        char   hms[8];
        utc_to_dir((uint32_t) now, date_dir, sizeof(date_dir));
        utc_to_time((uint32_t) now, hms, sizeof(hms));

        os_snprintf(full_dir, sizeof(full_dir), "%s/%s", REC_ROOT_PATH, date_dir);
        void *d = rec_dir_open(full_dir);
        if (!d) {
            if (osal_fmkdir(full_dir) != FR_OK) {
                os_printf(KERN_ERR "rec_create_file: mkdir %s fail\n", full_dir);
                rec_create_fail_inc();
                return NULL;
            }
        } else {
            rec_dir_close(d);
        }
        os_snprintf(file_name, FILE_NAME_LEN + 1, "%s_E%02d_%02d%s", hms, ev, dur, REC_TEMP_EXT);
        os_snprintf(file_path, 64, "%s/%s", full_dir, file_name);
        g_curfile.t_start = (uint32_t) now;
    }

    /* 记录当前文件信息 */
    os_strncpy(g_curfile.fname, file_name, sizeof(g_curfile.fname) - 1);
    g_curfile.fname[sizeof(g_curfile.fname) - 1] = '\0';
    os_strncpy(g_curfile.fpath, file_path, sizeof(g_curfile.fpath) - 1);
    g_curfile.fpath[sizeof(g_curfile.fpath) - 1] = '\0';
    g_curfile.start_tick = fp->frame_time;
    g_curfile.duration_sec = dur;
    g_curfile.extended = 0;

    /* 创建单个 MP4 文件，H264 和 AAC 由 miniMP4 共同写入这个句柄。 */
    /* 独占创建，校时回拨/同秒重试也不能追加到已有录像。 */
    void *mp4_fp = osal_open(file_path, 0, FA_CREATE_NEW | FA_READ | FA_WRITE);
    if (!mp4_fp) {
        os_printf(KERN_ERR "rec_create_file: mp4 fopen %s fail (SD likely full or fs error)\n",
                  file_path);
        /* 清空 g_curfile, 表示当前没有有效的录像文件 */
        os_memset(&g_curfile, 0, sizeof(g_curfile));
        rec_create_fail_inc();
        return NULL;
    }
    /* 成功 fopen mp4: 卡当前可用, 清不可恢复故障计数 */
    rec_create_fail_reset();

    /* AAC 直接复用到已经打开的 mp4_fp，不创建任何伴随音频文件。 */

    os_printf(KERN_INFO "rec_create_file: %s [H264+AAC]\n", file_path);
    os_printf(KERN_INFO "rec_start: first_tick=%u age_ms=%u start=%u synced=%u\n",
              fp->frame_time, (uint32_t)age_ms, g_curfile.t_start, synced);
    return mp4_fp;
}

/* 每段 MP4 关闭后投递后台校验；停止通知同时释放异步关闭标记。 */
static int32_t rec_end_encode_cb(struct file_process *fp)
{
    (void)fp;
    /* 已关闭的临时文件只投递，不在编码线程里扫描或修复。 */
    if (g_curfile.fpath[0] && !g_sd_formatting)
        rec_finalize_enqueue(g_curfile.fpath);
    if (g_rec_file_closing && !g_rec_msi) {
        os_memset(&g_curfile, 0, sizeof(g_curfile));
        g_rec_file_closing = 0;
    }
    return RET_OK;
}

static void rec_loop_free_cb(void **loop)
{
    (void) loop;
}


/* ==== 调试模式: DRY_RUN 模式下不写卡, 接收 fb 立刻返回 RET_ERR 丢弃 ====
 * 1 = 开启 DRY RUN (不写卡, 用于排查非写卡路径的问题)
 * 0 = 正常写卡 (默认) */
#define REC_DRY_RUN_NO_WRITE   0

#if REC_DRY_RUN_NO_WRITE

/* 计数统计 */
static volatile uint32_t g_dry_run_video_cnt = 0;
static volatile uint32_t g_dry_run_audio_cnt = 0;

/* sd_rec_mp4 msi 的 action: 仅接收 H264 + AAC 帧, 不拷贝不写卡, 直接返回 RET_ERR
 * 上游 AUTO_H264 看到 RET_ERR 不会 fb_get, 也就不占 h264_static_buf */
static int32_t rec_dryrun_action(struct msi *msi, uint32_t cmd_id, uint32_t param1, uint32_t param2)
{
    switch (cmd_id) {
        case MSI_CMD_TRANS_FB: {
            struct framebuff *fb = (struct framebuff *) param1;
            if (fb->mtype == F_H264) {
                if (fb->stype == REC_STREAM_STYPE) {
                    /* 只统计我们要的码流 */
                    g_dry_run_video_cnt++;
                    if ((g_dry_run_video_cnt % 200) == 0)
                        os_printf(KERN_INFO "rec_dryrun: v=%d a=%d\n",
                                  g_dry_run_video_cnt, g_dry_run_audio_cnt);
                }
            } else if (fb->mtype == F_AUDIO) {
                g_dry_run_audio_cnt++;
            }
            return RET_ERR;   /* 关键: 返回 ERR 让上游不 fb_get, 立即丢弃 */
        }
        default:
            break;
    }
    return RET_OK;
}
#endif

static int rec_start_raw(uint8_t event_type);
int _rp_record_start(uint8_t event_type)
{
    if (rec_sd_enter()) return -1;
    os_mutex_lock(&g_rec_control, osWaitForever);
    int ret = rec_start_raw(event_type);
    os_mutex_unlock(&g_rec_control);
    rec_sd_leave();
    return ret;
}
static int rec_start_raw(uint8_t event_type)
{
    if (g_rec_mode == REC_MODE_OFF) return -1;
    if (g_sd_formatting) return -1;
    if (g_rec_msi || g_rec_file_closing || mp4_encode_active_workers()) {
        os_printf(KERN_INFO "record already started or stopping\n");
        return g_rec_msi ? 0 : -1;
    }

    g_rec_h264 = msi_find(AUTO_H264, 1);
    if (!g_rec_h264) {
        os_printf(KERN_ERR "record_start: AUTO_H264 not found\n");
        return -1;
    }

#if REC_DRY_RUN_NO_WRITE
    /* ==== DRY RUN 模式: 不初始化 mp4_encode, 仅用一个丢弃 msi ==== */
    g_rec_msi = msi_new("sd_rec_mp4", 2, NULL);
    if (!g_rec_msi) {
        os_printf(KERN_ERR "record_start: dry-run msi create failed\n");
        msi_put(g_rec_h264);
        g_rec_h264 = NULL;
        return -1;
    }
    g_rec_msi->action = rec_dryrun_action;
    g_rec_msi->enable = 1;

    msi_add_output(g_rec_h264, NULL, "sd_rec_mp4");

    g_dry_run_video_cnt = 0;
    g_dry_run_audio_cnt = 0;

    os_printf(KERN_WARNING "rec: DRY RUN mode (no SD write), event=%d\n", event_type);
    return 0;

#else

    /* 初始化 g_curfile (先设置 event, create_file 时会用) */
    g_curfile.event = event_type;
    g_curfile.extended = 0;

    /* 先准备 AAC encoder，再建立双轨 MP4 复用器。 */
    uint32_t mp4_audio_encode = 0;
    if (rec_aac_prepare() != RET_OK) {
        os_printf(KERN_ERR "record_start: AAC encoder prepare failed\n");
        msi_put(g_rec_h264);
        g_rec_h264 = NULL;
        return -1;
    }
    mp4_audio_encode = AAC_ENC;

    static struct file_process fp_cfg;
    os_memset(&fp_cfg, 0, sizeof(fp_cfg));
    fp_cfg.loop        = NULL;
    fp_cfg.rec_path    = (char *) REC_ROOT_PATH;
    fp_cfg.ext_name    = (char *) REC_EXT_NAME;
    fp_cfg.create_file = rec_create_file_cb;
    fp_cfg.loop_free   = rec_loop_free_cb;
    fp_cfg.lock_file   = NULL;
    fp_cfg.end_encode  = rec_end_encode_cb;

    /* 录像 filter_type = REC_STREAM_STYPE, 由 project_config.h 的 REC_STREAM_TYPE 选:
     *   REC_STREAM_TYPE=0 → 双目主码流 VPP_DATA0, 1280x1440
     *   REC_STREAM_TYPE=1 → 双目子码流 GEN420, 640x720
     * rec_time 单位 分钟 */
    uint8_t rec_min = 1;
    g_rec_msi = mp4_encode_msi2_init("sd_rec_mp4",
                                     FRAMEBUFF_SOURCE_CAMERA0,
                                     REC_STREAM_STYPE,
                                     rec_min,
                                     mp4_audio_encode,
                                     &fp_cfg,
                                     0);
    if (!g_rec_msi) {
        os_printf(KERN_ERR "record_start: mp4_encode init failed\n");
        rec_aac_stop("sd_rec_mp4");
        msi_put(g_rec_h264);
        g_rec_h264 = NULL;
        return -1;
    }

    msi_add_output(g_rec_h264, NULL, "sd_rec_mp4");

    /* H264/AAC 两路都接好后再启动，保证首个 MP4 从开始就是双轨文件。 */
    if (rec_aac_attach("sd_rec_mp4") != RET_OK) {
        os_printf(KERN_ERR "record_start: AAC attach failed\n");
        msi_del_output(g_rec_h264, NULL, "sd_rec_mp4");
        msi_destroy(g_rec_msi);
        g_rec_msi = NULL;
        rec_aac_stop("sd_rec_mp4");
        msi_put(g_rec_h264);
        g_rec_h264 = NULL;
        return -1;
    }
    /* The product uses independent event snapshots, no vendor MP4 thumbnails. */
    msi_do_cmd(g_rec_msi, MSI_CMD_MEDIA_CTRL, MSI_MEDIA_CTRL_THUMB, 0);
    /* Alarm clips are stopped by the maintenance task at 30/60s. Leave room
     * beyond the 60s deadline so the muxer cannot roll into a second clip first. */
    if (g_rec_mode == REC_MODE_ALARM)
        msi_do_cmd(g_rec_msi, MSI_CMD_MEDIA_CTRL, MSI_MEDIA_CTRL_SET_RECORD_SEC, 90);
    msi_do_cmd(g_rec_msi, MSI_CMD_MEDIA_CTRL, MSI_MEDIA_CTRL_RECORD_START, 0);

    os_printf(KERN_INFO "record started (single MP4: H264+AAC), event=%d, min=%d\n",
              event_type, rec_min);
    return 0;
#endif
}

int _rp_record_stop(void)
{
    if (!g_rp_inited) return 0;
    os_mutex_lock(&g_rec_control, osWaitForever);
    int ret = rec_stop_raw();
    os_mutex_unlock(&g_rec_control);
    return ret;
}
static int rec_stop_raw(void)
{
    if (!g_rec_msi)
        return 0;

    if (g_rec_h264)
        msi_del_output(g_rec_h264, NULL, "sd_rec_mp4");

    /* 先停止 AAC 输入，避免 MP4 收尾时还有音频 framebuff 进入队列。 */
    rec_aac_stop("sd_rec_mp4");

    /* msi_destroy 只发起异步销毁。先将全局句柄置空并保留
     * g_curfile，等 rec_end_encode_cb 确认 mp4_deinit+fclose 后再投递校正。 */
    struct msi *closing_msi = g_rec_msi;
    g_rec_file_closing = 1;
    g_rec_msi = NULL;
    msi_destroy(closing_msi);
    if (g_rec_h264) {
        msi_put(g_rec_h264);
        g_rec_h264 = NULL;
    }
    if (!g_rec_file_closing)
        os_memset(&g_curfile, 0, sizeof(g_curfile));

    os_printf(KERN_INFO "record stopped\n");
    return 0;
}

static void rec_alarm_check(void)
{
    os_mutex_lock(&g_rec_control, osWaitForever);
    if (g_rec_mode == REC_MODE_ALARM && g_rec_msi && g_curfile.fpath[0] &&
        g_curfile.duration_sec &&
        os_jiffies_to_msecs((uint32_t)os_jiffies() - g_curfile.start_tick) >=
            (uint32_t)g_curfile.duration_sec * 1000U)
        rec_stop_raw();
    os_mutex_unlock(&g_rec_control);
}

static int rec_trigger_alarm_raw(uint8_t event_type);
int rec_trigger_alarm(uint8_t event_type)
{
    if (rec_sd_enter()) return -1;
    os_mutex_lock(&g_rec_control, osWaitForever);
    int ret = rec_trigger_alarm_raw(event_type);
    os_mutex_unlock(&g_rec_control);
    rec_sd_leave();
    return ret;
}
static int rec_trigger_alarm_raw(uint8_t event_type)
{
    if (!g_rp_inited)
        return -1;

    if (g_rec_mode == REC_MODE_OFF) {
        os_printf(KERN_INFO "rec_trigger_alarm: mode=OFF, ignored\n");
        return 0;
    }

    if (g_rec_mode == REC_MODE_ALARM) {
        if (!g_rec_msi) {
            return rec_start_raw(event_type);
        }
        if (!g_curfile.fpath[0]) return 0; /* Still waiting for the first I frame. */
        uint32_t elapsed = (uint32_t)(os_jiffies_to_msecs(
            (uint32_t)os_jiffies() - g_curfile.start_tick) / 1000U);
        if (!g_curfile.extended && elapsed < REC_FILE_SEC_ALARM) {
            g_curfile.extended = 1;
            g_curfile.duration_sec = REC_FILE_SEC_ALARM_EXT;
            if (g_curfile.event == ECEVENT_NONE)
                g_curfile.event = event_type;
            os_printf(KERN_INFO "rec_trigger_alarm: extend to %ds, event=%d\n",
                      REC_FILE_SEC_ALARM_EXT, g_curfile.event);
        }
        return 0;
    }

    /* REC_MODE_ALL_DAY: 标记当前文件 event 类型, 下次切片生效 */
    if (g_curfile.event == ECEVENT_NONE) {
        g_curfile.event = event_type;
        os_printf(KERN_INFO "rec_trigger_alarm: all_day mode, mark event=%d\n", event_type);
    }
    return 0;
}

/* =========================================================================
 * 录像文件列表 (按需扫描 + 合并相邻同类事件)
 * ========================================================================= */

/* 切片达到计划时长后要等下一帧 I 帧，编码器最长允许约 3 秒；文件名又只有
 * 秒级精度，因此列表合并额外保留 5 秒容差。超过该值仍视为真实缺录像。 */
#define REC_LIST_MERGE_GAP_SEC  5U

/* 内部: 遍历当日 day_arr, 按时间范围 + 相邻合并策略生成输出条目.
 * @param out_arr NULL 时仅统计数量 (pass1), 非 NULL 时填充 (pass2)
 * @return 产生的条目数 (pass1 模式下也有效), out_arr 满时停止 */
static int rec_list_fold_day(const scan_item_t *day_arr, uint32_t day_cnt,
                             uint32_t t_start, uint32_t t_end,
                             SAvExEvent *out_arr, int out_cap)
{
    int      out_cnt = 0;
    uint8_t  merged_event = 0;
    uint32_t merged_start = 0;
    uint32_t merged_end = 0;

    for (uint32_t i = 0; i < day_cnt; i++) {
        uint32_t ft0 = day_arr[i].t_start;
        uint32_t ft1 = ft0 + day_arr[i].duration;
        if (ft1 < t_start) continue;
        if (ft0 >= t_end) break;

        /* 已按开始时间排序。只要当前片段与已合并区间重叠，或间隔不超过
         * REC_LIST_MERGE_GAP_SEC，就并入同一条；结束时间取 max，避免嵌套/
         * 重叠片段把区间错误缩短。pass1/pass2 共用同一状态，数量严格一致。 */
        if (out_cnt > 0 && merged_event == day_arr[i].event &&
            ft0 <= merged_end + REC_LIST_MERGE_GAP_SEC) {
            if (ft1 > merged_end)
                merged_end = ft1;
            if (out_arr)
                out_arr[out_cnt - 1].file_len = merged_end - merged_start;
            continue;
        }

        if (out_cnt >= out_cap) break;
        merged_start = ft0;
        merged_end   = ft1;
        merged_event = day_arr[i].event;
        if (out_arr) {
            TcuT2TimeDay((time_t) ft0, &out_arr[out_cnt].start_time);
            out_arr[out_cnt].file_len = day_arr[i].duration;
            out_arr[out_cnt].event    = day_arr[i].event;
            out_arr[out_cnt].flags    = 0;
        }
        out_cnt++;
    }
    return out_cnt;
}

/* 单次查询只返回 t_start 所在那一天的条目 (本地日).
 * 两阶段: 先扫描+统计实际条目数, 再按精确数量 malloc, 再填充.
 *
 * !!! 目录名是"本地日期"(rec_create_file_cb 里 utc_to_dir(now) 得到, 加了 _tg_timezone_),
 *     所以这里必须用 utc_to_dir(t_start) 推目录, 不能按 UTC 86400 对齐.
 *     之前按 UTC 对齐会扫到前一天 (北京 5-6 00:00 对应真实 UTC 5-5 16:00,
 *     86400 对齐后 day0 是 5-5 00:00 UTC, 目录是 "20260505"), 导致 0 records. */
static int rec_list_get_raw(uint32_t t_start, uint32_t t_end, SAvExEvent **out_items);
int rec_list_get(uint32_t t_start, uint32_t t_end, SAvExEvent **out_items)
{
    if (!out_items) return -1;
    *out_items = NULL;
    if (rec_sd_enter()) return -1;
    int ret = rec_list_get_raw(t_start, t_end, out_items);
    rec_sd_leave();
    return ret;
}
static int rec_list_get_raw(uint32_t t_start, uint32_t t_end, SAvExEvent **out_items)
{
    if (!out_items) return -1;
    *out_items = NULL;

    /* 目录按 t_start 对应的本地日期算; 过滤窗口直接用 APP 传的 t_start/t_end.
     * 只扫这一天的目录, 跨天查询请上层分天调用 */
    char date_dir[16];
    utc_to_dir(t_start, date_dir, sizeof(date_dir));
    uint32_t q_start = t_start;
    uint32_t q_end   = t_end;

    scan_item_t *day_arr = NULL;
    uint32_t     day_cnt = 0;
    if (rec_idx_get_day(date_dir, &day_arr, &day_cnt) != 0) return -1;
    if (day_cnt == 0) {
        os_printf(KERN_INFO "rec_list_get: scan(%s) empty (t_start=%u)\n",
                  date_dir, t_start);
        if (day_arr) RP_FREE(day_arr);
        return 0;
    }

    /* Pass 1: 只统计条目数, 不分配输出数组 */
    const int scan_cap = 1440;  /* 单日最多文件数 (每分钟一个), 硬上限 */
    int need = rec_list_fold_day(day_arr, day_cnt, q_start, q_end, NULL, scan_cap);
    if (need <= 0) {
        RP_FREE(day_arr);
        return 0;
    }

    /* 按实际需求精确分配 */
    SAvExEvent *arr = (SAvExEvent *) RP_MALLOC(sizeof(SAvExEvent) * need);
    if (!arr) {
        os_printf(KERN_ERR "rec_list_get: alloc %d items (%d bytes) fail\n",
                  need, (int)(sizeof(SAvExEvent) * need));
        RP_FREE(day_arr);
        return -1;
    }
    os_memset(arr, 0, sizeof(SAvExEvent) * need);

    /* Pass 2: 实际填充 */
    int final_cnt = rec_list_fold_day(day_arr, day_cnt, q_start, q_end, arr, need);
    RP_FREE(day_arr);

    if (final_cnt <= 0) {
        RP_FREE(arr);
        return 0;
    }
    *out_items = arr;
    os_printf(KERN_INFO "rec_list_get: %d items (day %s, range %u-%u)\n",
              final_cnt, date_dir, q_start, q_end);
    return final_cnt;
}

/* 检查某个日期子目录下是否至少有一个 "已完成" 的 MP4 文件.
 * 找到第一个就立刻返回, 不做完整扫描, 节省 IO.
 * 跳过 g_curfile 标记的正在录文件 (未完成, 不可回放) */
static int day_dir_has_mp4(const char *date_dir)
{
    char sub_path[64];
    os_snprintf(sub_path, sizeof(sub_path), "%s/%s", REC_ROOT_PATH, date_dir);
    void *d = rec_dir_open(sub_path);
    if (!d) return -1;

    /* 快照正在录的文件名/路径, 和 scan_day_dir 逻辑一致 */
    char cur_fname_snap[FILE_NAME_LEN + 1] = {0};
    char cur_fpath_snap[96] = {0};
    if (g_rec_msi || g_rec_file_closing) {
        os_strncpy(cur_fname_snap, g_curfile.fname, sizeof(cur_fname_snap) - 1);
        os_strncpy(cur_fpath_snap, g_curfile.fpath, sizeof(cur_fpath_snap) - 1);
    }

    int found = 0;
    void *fno;
    while ((fno = rec_dir_read(d)) != NULL) {
        char *fn = osal_dirent_name(fno);
        if (!fn || osal_dirent_isdir(fno)) continue;
        int nlen = os_strlen(fn);
        if (nlen < 5) continue;
        if (os_strcasecmp(fn + nlen - 4, REC_EXT_NAME) != 0) continue;
        if (parse_filename(fn, NULL, NULL, NULL, NULL, NULL)) continue;
        /* 跳过正在录的文件 */
        if (cur_fname_snap[0] &&
            os_strcmp(fn, cur_fname_snap) == 0 &&
            os_strstr(cur_fpath_snap, date_dir) != NULL) {
            continue;
        }
        found = 1;
        break;
    }
    int failed = rec_dir_failed(d);
    rec_dir_close(d);
    return failed ? -1 : found;
}

/* 扫描 REC_ROOT_PATH 下的 YYYYMMDD 子目录, 返回有录像的日期列表.
 * 只统计"目录内至少有 1 个 MP4 文件"的合法日期目录 */
static int rec_list_days_raw(SDay **out_days);
int rec_list_days_get(SDay **out_days)
{
    if (!out_days) return -1;
    *out_days = NULL;
    if (rec_sd_enter()) return -1;
    int ret = rec_list_days_raw(out_days);
    rec_sd_leave();
    return ret;
}
static int rec_list_days_raw(SDay **out_days)
{
    if (!out_days) return -1;
    *out_days = NULL;

    /* 第一遍: 数数有多少个合法的 YYYYMMDD 目录且内含 MP4 */
    void *dir = rec_dir_open(REC_ROOT_PATH);
    if (!dir) return -1;

    uint32_t cnt = 0;
    void *fno;
    while ((fno = rec_dir_read(dir)) != NULL) {
        char *name = osal_dirent_name(fno);
        if (!name || !osal_dirent_isdir(fno)) continue;
        uint16_t y; uint8_t mon, day;
        if (parse_dirname(name, &y, &mon, &day) != 0) continue;
        int has = day_dir_has_mp4(name);
        if (has < 0) { rec_dir_close(dir); return -1; }
        if (!has) continue;
        cnt++;
    }
    int failed = rec_dir_failed(dir);
    rec_dir_close(dir);
    if (failed) return -1;
    if (cnt == 0) return 0;

    /* 第二遍: 分配数组并填充 */
    SDay *arr = (SDay *) RP_MALLOC(sizeof(SDay) * cnt);
    if (!arr) return -1;
    os_memset(arr, 0, sizeof(SDay) * cnt);

    dir = rec_dir_open(REC_ROOT_PATH);
    if (!dir) { RP_FREE(arr); return -1; }

    uint32_t idx = 0;
    while ((fno = rec_dir_read(dir)) != NULL && idx < cnt) {
        char *name = osal_dirent_name(fno);
        if (!name || !osal_dirent_isdir(fno)) continue;
        uint16_t y; uint8_t mon, day;
        if (parse_dirname(name, &y, &mon, &day) != 0) continue;
        int has = day_dir_has_mp4(name);
        if (has < 0) { rec_dir_close(dir); RP_FREE(arr); return -1; }
        if (!has) continue;
        arr[idx].year  = y;
        arr[idx].month = mon;
        arr[idx].day   = day;
        idx++;
    }
    failed = rec_dir_failed(dir);
    rec_dir_close(dir);
    if (failed) { RP_FREE(arr); return -1; }

    /* 按日期升序排序 */
    if (idx > 1) {
        for (uint32_t i = 0; i + 1 < idx; i++) {
            for (uint32_t j = 0; j + 1 < idx - i; j++) {
                uint32_t a = (uint32_t) arr[j].year * 10000
                           + (uint32_t) arr[j].month * 100
                           + (uint32_t) arr[j].day;
                uint32_t b = (uint32_t) arr[j+1].year * 10000
                           + (uint32_t) arr[j+1].month * 100
                           + (uint32_t) arr[j+1].day;
                if (a > b) {
                    SDay t = arr[j]; arr[j] = arr[j+1]; arr[j+1] = t;
                }
            }
        }
    }

    *out_days = arr;
    os_printf(KERN_INFO "rec_list_days_get: %u days\n", idx);
    return (int) idx;
}

/* =========================================================================
 * P2P 回放
 * ========================================================================= */

typedef enum {
    PB_ST_IDLE = 0,
    PB_ST_PLAYING,
    PB_ST_PAUSED,
    PB_ST_STOP_REQ,
} pb_state_t;

typedef enum {
    PB_SPEED_1X = 0,
    PB_SPEED_2X,
    PB_SPEED_4X,
    PB_SPEED_IKEY,
} pb_speed_t;

static struct {
    p2phandle_t           handle;
    volatile pb_state_t   state;
    volatile pb_speed_t   speed;
    volatile uint8_t      mode_bit;     /* 0: 连续; 1: 事件 */
    volatile uint8_t      thread_alive;
    volatile uint8_t      seek_req;
    volatile uint32_t     seek_t;
    struct msi           *demux_msi;
    struct os_mutex       lock;
    /* 当前播放的文件信息, 用于"当前日期下找下一个" */
    char                  cur_date_dir[16];
    char                  cur_fname[FILE_NAME_LEN + 1];
} g_pb;

/* 给 rec_bootstrap_thread 的预删除循环用 (它在 g_pb 之前定义, 无法直接读). */
static int rec_pb_is_active(void)
{
    return g_pb.thread_alive || mp4_demux_active_workers();
}

/* 按目标 UTC 时间, 查找要播放的文件.
 * 优先级:
 *   1) 覆盖 t_seek 的文件 (t_start <= t_seek < t_start+duration)
 *   2) 当天后续最近的文件 (t_start > t_seek 里最小的那个)
 * 都没有(t_seek 超过当天所有文件的结束时间) 才返回 -1, 不跨天查找.
 * @param out_t0   返回值是实际起播文件的 t_start, 不一定等于 t_seek */
static int pb_locate_file(uint32_t t_seek, char *out_date_dir, char *out_fname,
                          uint32_t *out_t0)
{
    char date_dir[16];
    utc_to_dir(t_seek, date_dir, sizeof(date_dir));
    scan_item_t *arr = NULL; uint32_t cnt = 0;
    int sret = pb_scan_day_retry(date_dir, &arr, &cnt);
    if (sret != 0 || cnt == 0) {
        os_printf(KERN_WARNING "pb_locate: scan(%s) ret=%d cnt=%u t_seek=%u\n",
                  date_dir, sret, cnt, t_seek);
        if (arr) RP_FREE(arr);
        return -1;
    }

    int found = -1;
    /* 1) 先找覆盖区间 */
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t t0 = arr[i].t_start;
        uint32_t t1 = t0 + arr[i].duration;
        if (t_seek >= t0 && t_seek < t1) {
            found = (int) i;
            break;
        }
    }
    /* 2) 未覆盖, 找第一个 t_start >= t_seek 的文件 (fallback: 当天后续最近) */
    if (found < 0) {
        for (uint32_t i = 0; i < cnt; i++) {
            if (arr[i].t_start >= t_seek) {
                found = (int) i;
                os_printf(KERN_INFO "pb_locate: fallback to next file "
                          "in %s, t_seek=%u -> file_t0=%u (+%us)\n",
                          date_dir, t_seek, arr[i].t_start,
                          arr[i].t_start - t_seek);
                break;
            }
        }
    }
    if (found < 0) {
        os_printf(KERN_WARNING "pb_locate: no file after t_seek=%u in %s "
                  "(cnt=%u, last_t0=%u, last_end=%u)\n",
                  t_seek, date_dir, cnt,
                  arr[cnt-1].t_start,
                  arr[cnt-1].t_start + arr[cnt-1].duration);
        RP_FREE(arr);
        return -1;
    }
    os_strncpy(out_date_dir, date_dir, 16);
    build_fname_from_item(&arr[found], out_fname, FILE_NAME_LEN + 1);
    *out_t0 = arr[found].t_start;
    RP_FREE(arr);
    return 0;
}

/* MP4 中 AAC 帧的最近发送时间戳，用于回放诊断日志。 */
static uint32_t g_pb_audio_ts  = 0;

/* 反压机制: TciSendPbFrame 返回 TCE_NETWORK_BUSY (-10004001) 时, 说明探鸽 SDK
 * 内部帧队列已满, 继续塞会导致 PSRAM 被累积的帧 chunk 蚕食, 最终 malloc fail 崩溃.
 *
 * 处理策略: 不丢帧, 原地 sleep PB_BUSY_SLEEP_MS 后重发同一帧, 直到发送成功或到达
 * PB_BUSY_MAX_RETRY 次上限 (避免连接已断时死循环). 重发期间 sd_pb_recv 里的 fb
 * 会短暂堆积, 但 msi 背压会让 mp4_demux 自然停下, 不会无限增长. */
#define TCE_NETWORK_BUSY_VAL  (-10004001)
#define PB_BUSY_SLEEP_MS      200       /* 每次 BUSY 后的休眠间隔 (探鸽文档建议 300ms) */
#define PB_BUSY_MAX_RETRY     20        /* 最多重试次数, 20*200ms=4s 兜底退出 */
/* 预检 MP4 文件是否合法可播放.
 *
 * !!! 为什么要自己预检, 不直接交给 mp4_demux_msi_init 判定:
 *   SDK 的 mp4_demux_msi.c 在 SPS/PPS 解析失败时走 mp4_demux_msi_init_err,
 *   里面 msi_destroy() 会触发 action(POST_DESTROY), 而 POST_DESTROY 用
 *   `os_event_wait(MP4_DEMUX_EXIT, -1)` 等一个永远不会被 set 的事件
 *   (mp4_demux_thread 根本没启动就进 err 分支了) → 调用线程永久挂起.
 *   所以我们必须在调用 mp4_demux_msi_init 之前自己先判断.
 *
 * SDK 的 MP4_open_init 用 fast-start 布局: 文件头是 ftyp + moov + mdat。
 * 这里只按 box header 层级跳转，读取 stts/stsz/stco 计数，不读 mdat。
 *
 * 额外: 正在录制中的文件, moov box 虽已写 header 但内容是预留空位, SPS/PPS
 * 还没填入. 这种文件"可以扫到 moov 签名"但 mp4_demux_msi_init 解析时仍会
 * SPS=0/PPS=0 导致 SDK 死锁. 所以当前正在录的文件必须在上层额外跳过
 * (见下方 pb_is_current_recording_file).
 *
 * 返回: 1=合法可播, 0=结构不完整/不支持, -1=打开或读取失败。 */
typedef struct {
    uint32_t stts_samples;
    uint32_t stsz_count;
    uint32_t stco_count;
    uint8_t  has_stts;
    uint8_t  has_stsz;
    uint8_t  has_stco;
} pb_mp4_track_index_t;

/* 扫描索引：0=有效，-1=结构异常，-2=读写失败。 */
static int pb_mp4_scan_track_index(F_FILE *fp, uint32_t begin, uint32_t end,
                                   uint8_t depth, pb_mp4_track_index_t *idx)
{
    uint32_t pos = begin;

    if (!fp || !idx || depth > 4)
        return -1;

    while (pos <= end && end - pos >= 8U) {
        uint8_t hdr[8];
        uint32_t box_size;
        uint32_t box_end;

        if (osal_fseek(fp, pos) != FR_OK ||
            osal_fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr))
            return -2;

        box_size = rec_mp4_be32(hdr);
        if (box_size == 0U) {
            box_end = end;
        } else {
            if (box_size == 1U || box_size < 8U || box_size > end - pos)
                return -1;
            box_end = pos + box_size;
        }

        if (rec_mp4_box_is(hdr + 4, "mdia") ||
            rec_mp4_box_is(hdr + 4, "minf") ||
            rec_mp4_box_is(hdr + 4, "stbl")) {
            int ret = pb_mp4_scan_track_index(fp, pos + 8U, box_end,
                                               depth + 1U, idx);
            if (ret != 0) return ret;
        } else if (rec_mp4_box_is(hdr + 4, "stsz")) {
            uint8_t body[12]; /* 版本及标志 + 样本大小 + 样本数量 */
            if (box_end - pos < 20U) return -1;
            if (osal_fseek(fp, pos + 8U) != FR_OK ||
                osal_fread(body, 1, sizeof(body), fp) != sizeof(body))
                return -2;
            /* 当前 demux 仅支持 sample_size=0（每帧单独记长度）。 */
            if (rec_mp4_be32(body + 4) != 0U)
                return -1;
            idx->stsz_count = rec_mp4_be32(body + 8);
            idx->has_stsz = 1;
        } else if (rec_mp4_box_is(hdr + 4, "stco")) {
            uint8_t body[8]; /* 版本及标志 + 索引条目数量 */
            if (box_end - pos < 16U) return -1;
            if (osal_fseek(fp, pos + 8U) != FR_OK ||
                osal_fread(body, 1, sizeof(body), fp) != sizeof(body))
                return -2;
            idx->stco_count = rec_mp4_be32(body + 4);
            idx->has_stco = 1;
        } else if (rec_mp4_box_is(hdr + 4, "stts")) {
            uint8_t body[8]; /* 版本及标志 + 索引条目数量 */
            uint32_t entry_count;
            uint32_t samples = 0;
            uint32_t i;

            if (box_end - pos < 16U) return -1;
            if (osal_fseek(fp, pos + 8U) != FR_OK ||
                osal_fread(body, 1, sizeof(body), fp) != sizeof(body))
                return -2;
            entry_count = rec_mp4_be32(body + 4);
            if (entry_count > (box_end - pos - 16U) / 8U)
                return -1;
            for (i = 0; i < entry_count; i++) {
                uint8_t ent[8];
                uint32_t n;
                if (osal_fseek(fp, pos + 16U + i * 8U) != FR_OK ||
                    osal_fread(ent, 1, sizeof(ent), fp) != sizeof(ent))
                    return -2;
                n = rec_mp4_be32(ent);
                if (samples > 0xffffffffU - n)
                    return -1;
                samples += n;
            }
            idx->stts_samples = samples;
            idx->has_stts = 1;
        }

        if (box_end <= pos)
            return -1;
        pos = box_end;
    }
    return 0;
}

static int pb_mp4_is_playable(const char *mp4_full_path)
{
    F_FILE *fp = osal_fopen(mp4_full_path, "rb");
    uint32_t fsize;
    uint32_t pos = 0;
    uint8_t found_moov = 0;
    uint8_t track_count = 0;
    pb_mp4_track_index_t tracks[2];

    if (!fp)
        return -1;
    fsize = osal_fsize(fp);
    if (fsize < 64U) {
        osal_fclose(fp);
        return 0;
    }
    os_memset(tracks, 0, sizeof(tracks));

    while (pos <= fsize && fsize - pos >= 8U) {
        uint8_t hdr[8];
        uint32_t box_size;
        uint32_t box_end;

        if (osal_fseek(fp, pos) != FR_OK ||
            osal_fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr))
            goto io_error;
        box_size = rec_mp4_be32(hdr);
        if (box_size == 0U) {
            box_end = fsize;
        } else {
            if (box_size == 1U || box_size < 8U || box_size > fsize - pos)
                goto invalid;
            box_end = pos + box_size;
        }

        if (rec_mp4_box_is(hdr + 4, "moov")) {
            uint32_t child = pos + 8U;
            found_moov = 1;
            while (child <= box_end && box_end - child >= 8U) {
                uint8_t chdr[8];
                uint32_t child_size;
                uint32_t child_end;

                if (osal_fseek(fp, child) != FR_OK ||
                    osal_fread(chdr, 1, sizeof(chdr), fp) != sizeof(chdr))
                    goto io_error;
                child_size = rec_mp4_be32(chdr);
                if (child_size < 8U || child_size > box_end - child)
                    goto invalid;
                child_end = child + child_size;

                if (rec_mp4_box_is(chdr + 4, "trak")) {
                    if (track_count >= 2U) goto invalid;
                    int ret = pb_mp4_scan_track_index(fp, child + 8U, child_end,
                                                      0, &tracks[track_count]);
                    if (ret == -2) goto io_error;
                    if (ret != 0) goto invalid;
                    track_count++;
                }
                child = child_end;
            }
            break;
        }

        if (box_end <= pos)
            goto invalid;
        pos = box_end;
    }

    if (!found_moov || track_count == 0U)
        goto invalid;

    for (uint8_t i = 0; i < track_count; i++) {
        pb_mp4_track_index_t *idx = &tracks[i];
        /* 本工程的 miniMP4 每个 sample 写一个 chunk，因此三者
         * 必须一致。任意不一致都视为未完成/损坏文件，直接跳过。 */
        if (!idx->has_stts || !idx->has_stsz || !idx->has_stco ||
            idx->stsz_count == 0U ||
            idx->stts_samples != idx->stsz_count ||
            idx->stco_count != idx->stsz_count) {
            os_printf(KERN_ERR "pb: invalid MP4 track %u: stts=%u stsz=%u stco=%u (%s)\n",
                      i, idx->stts_samples, idx->stsz_count, idx->stco_count,
                      mp4_full_path);
            goto invalid;
        }
    }

    osal_fclose(fp);
    return 1;

io_error:
    osal_fclose(fp);
    return -1;
invalid:
    osal_fclose(fp);
    return 0;
}

/* 判断路径是否是当前正在录制中的文件. 正在录的文件 moov 内容尚未写入,
 * 既不能播 (SDK 解析失败挂起), 也不能删 (录像仍在写). 遇到直接跳过,
 * 等下次 APP 回放时它已经录完关闭就能正常播放. */
static int pb_is_current_recording_file(const char *mp4_full_path)
{
    if (!mp4_full_path || (!g_rec_msi && !g_rec_file_closing) || !g_curfile.fpath[0]) return 0;
    return os_strcmp(mp4_full_path, g_curfile.fpath) == 0 ? 1 : 0;
}

/* 1=打开成功，0=结构无效（跳过），-1=读写或资源异常（停止）。
 * 打开或读取失败不代表文件损坏，回放流程中不得删除录像文件。 */
static int pb_open_checked(const char *demux_name, const char *path)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (g_sd_formatting || g_pb.seek_req || g_pb.state == PB_ST_STOP_REQ || g_pb.state == PB_ST_IDLE)
            return -1;
        int valid = pb_mp4_is_playable(path);
        if (valid == 0) return 0;
        if (valid > 0) {
            char attempt_name[32];
            os_snprintf(attempt_name, sizeof(attempt_name), "%s_%u",
                        demux_name, (unsigned)attempt);
            g_pb.demux_msi = mp4_demux_msi_init(attempt_name, path);
            if (g_pb.demux_msi) return 1;
        }
        os_printf(KERN_WARNING "pb: open/read/resource failure, keep file %s (attempt %d/3)\n",
                  path, attempt + 1);
        if (attempt < 2) {
            for (int n = 0; n < 10; ++n) {
                if (g_pb.seek_req || g_pb.state == PB_ST_STOP_REQ || g_pb.state == PB_ST_IDLE)
                    return -1;
                os_sleep_ms(10);
            }
        }
    }
    return -1;
}

/* =========================================================================
 * 卡回放音频: AAC → PCM → G.711A 转码
 *
 * 录像文件音频轨是 AAC-LC。APP 端 AAC 解码不稳定(卡顿/噪音), 因此回放时由
 * 设备端解码成 PCM, 再用 linear2alaw 转 G.711A 发送, 与实时流音频格式一致
 * (8kHz/16bit/mono, 每包 40ms = 320 字节)。
 *
 * 使用 SDK AAC 解码 MSI 的独立线程，PCM 保留 MP4 时间戳后打包发送。
 * 输入必须是完整 ADTS 帧(7 字节头 + AU), demux 输出的 fb 正好是这个格式。
 *
 * AAC 一帧固定 1024 样本 = 128ms = 1024 字节 G.711A, 不是 320 的整数倍,
 * 因此用累加器跨帧凑满 320 字节再发, 保证每包严格 40ms。
 * ========================================================================= */
#define PB_ALAW_PACK_BYTES      320     /* 一包 G.711A (40ms @ 8kHz mono) */
#define PB_PACK_INTERVAL_MS     40      /* 每包 G.711A 对应 40ms (320B @ 8kHz) */
#define PB_PCM_MAX_SAMPLES      2048    /* AAC 一帧最多 1024 样本, 留一倍余量 */
#define PB_ALAW_ACC_BYTES       (PB_PCM_MAX_SAMPLES + PB_ALAW_PACK_BYTES)

static uint8_t *g_pb_alaw_acc = NULL; /* 动态申请的 PSRAM 缓冲 */
static int g_pb_alaw_acc_len = 0;
static uint32_t g_pb_alaw_acc_ts = 0; /* 首个样本在文件中的 PTS，单位毫秒 */
static struct msi *g_pb_aac_msi = NULL;
static struct msi *g_pb_pcm_sink = NULL;

/* 拖动或切换文件时丢弃跨越断点的残留样本。
 * 解码器的生命周期由 pb_aac_msi_destroy/create 单独管理。 */
static void pb_aac_reset(void)
{
    g_pb_alaw_acc_len = 0;
    g_pb_alaw_acc_ts = 0;
}

/* 拆掉解码器 + PCM sink. 销毁 demux 之前必须先调用, 否则解码器的
 * src_msi 指向已释放的 demux. */
static void pb_aac_msi_destroy(void)
{
    if (g_pb_pcm_sink) { msi_destroy(g_pb_pcm_sink); g_pb_pcm_sink = NULL; }
    if (g_pb_aac_msi)  { msi_destroy(g_pb_aac_msi);  g_pb_aac_msi  = NULL; }
}

/* 在当前 demux 上挂 AAC 解码器. demux 每次创建后调用一次.
 * 解码器把 PCM 输出到 pb_pcm_sink, pb_thread 从那里取帧转 G.711A. */
static int pb_aac_msi_create(void)
{
    if (g_pb_aac_msi) return 0;
    if (!g_pb.demux_msi)  return -1;

    /* sink 只负责把解码器的输出收进队列, 不做任何处理.
     *
     * !!! sink 名字必须每次唯一, 不能用固定名:
     *   msi_destroy() 只发起"异步销毁", seek 时 pb_aac_msi_destroy() 之后
     *   立刻重建, 旧 sink 往往还没真正析构完 —— 同名 msi_new 会失败, 表现
     *   为离开第一个文件后 "AAC decode msi init fail" 刷屏. 与 demux 用
     *   sd_pb_demux_%u 自增序号同理. */
    static uint32_t sink_seq = 0;
    char sink_name[24];
    os_snprintf(sink_name, sizeof(sink_name), "pb_pcm_sink_%u", ++sink_seq);

    g_pb_pcm_sink = msi_new(sink_name, 8, NULL);
    if (!g_pb_pcm_sink) {
        os_printf(KERN_ERR "pb: pcm sink create fail (%s)\n", sink_name);
        return -1;
    }
    g_pb_pcm_sink->action = NULL;
    g_pb_pcm_sink->enable = 1;

    AUDEC_INIT ai;
    os_memset(&ai, 0, sizeof(ai));
    ai.track_type    = MEDIA_TRACK;
    ai.priority      = play_interruptible;
    ai.direct_to_dac = 0;    /* 不走 DAC, PCM 交给 sink 走 P2P */
    ai.use_tpc       = 0;
    ai.destroy_self  = 0;
    ai.src_msi       = g_pb.demux_msi;   /* 关键: 解码器自己订阅 demux 输出 */

    g_pb_aac_msi = audio_decode_init(AAC_DEC, 8000, &ai);
    if (!g_pb_aac_msi) {
        os_printf(KERN_ERR "pb: AAC decode msi init fail (src=%s)\n", g_pb.demux_msi->name);
        msi_destroy(g_pb_pcm_sink);
        g_pb_pcm_sink = NULL;
        return -1;
    }

    if (msi_add_output(g_pb_aac_msi, NULL, sink_name) != RET_OK) {
        os_printf(KERN_ERR "pb: attach pcm sink fail (%s)\n", sink_name);
        pb_aac_msi_destroy();
        return -1;
    }
    os_printf(KERN_INFO "pb: AAC decode msi ready (src=%s, sink=%s)\n",
              g_pb.demux_msi->name, sink_name);
    return 0;
}

static uint32_t pb_clock_ms(void)
{
    return (uint32_t)os_jiffies_to_msecs(os_jiffies());
}

#define PB_AV_MAX_DIFF_MS 100U
#define PB_AUDIO_SAMPLES_PER_MS 8U

typedef struct {
    uint32_t packets, pause, errors, busy, decoded, dropped;
    int last_ret;
} pb_audio_stats_t;

/* PCM 携带对应 AAC 样本的结束 PTS。打包时保留首个样本的真实 PTS，
 * 解码失败或丢包造成的时间间隔也必须保留。 */
static void pb_audio_pump(uint32_t base_ms, uint32_t elapsed_ms,
                          uint32_t video_ts, uint8_t video_valid,
                          uint32_t *audio_ts, uint8_t *audio_valid,
                          pb_audio_stats_t *stat)
{
    if (!g_pb_pcm_sink || !g_pb_alaw_acc || !video_valid ||
        g_pb.state != PB_ST_PLAYING || g_pb.seek_req)
        return;

    /* 每次处理次数有限，持有视频帧时不得阻塞等待 PCM 或网络。 */
    for (int n = 0; n < 8; ++n) {
        if (g_pb.speed != PB_SPEED_1X)
            g_pb_alaw_acc_len = 0;
        if (g_pb_alaw_acc_len < PB_ALAW_PACK_BYTES) {
            struct framebuff *pfb = msi_get_fb(g_pb_pcm_sink, 0);
            if (!pfb) return;
            int16_t *pcm = (int16_t *)pfb->data;
            uint32_t samples = pfb->len / sizeof(int16_t);
            uint32_t duration = samples / PB_AUDIO_SAMPLES_PER_MS;
            uint32_t start_ms = pfb->time >= duration ? pfb->time - duration : 0;
            uint32_t trim = 0;
            stat->decoded += samples;
            if (!pcm || !samples || samples > PB_PCM_MAX_SAMPLES ||
                g_pb.speed != PB_SPEED_1X) {
                msi_delete_fb(NULL, pfb);
                stat->dropped++;
                continue;
            }
            if (start_ms < base_ms) {
                uint32_t before_ms = base_ms - start_ms;
                trim = before_ms >= duration ? samples :
                       before_ms * PB_AUDIO_SAMPLES_PER_MS;
                start_ms += trim / PB_AUDIO_SAMPLES_PER_MS;
            }
            if (trim < samples) {
                uint32_t expected_ms = g_pb_alaw_acc_ts +
                    (uint32_t)g_pb_alaw_acc_len / PB_AUDIO_SAMPLES_PER_MS;
                int32_t gap_ms = (int32_t)(start_ms - expected_ms);
                /* 不得把缺失 AAC 帧前后的样本直接拼接到一起。 */
                if (g_pb_alaw_acc_len && (gap_ms < -1 || gap_ms > 1)) {
                    g_pb_alaw_acc_len = 0;
                    stat->dropped++;
                }
                if (!g_pb_alaw_acc_len) g_pb_alaw_acc_ts = start_ms;
                for (uint32_t i = trim; i < samples; ++i)
                    g_pb_alaw_acc[g_pb_alaw_acc_len++] = linear2alaw(pcm[i]);
            }
            msi_delete_fb(NULL, pfb);
            if (g_pb_alaw_acc_len < PB_ALAW_PACK_BYTES) continue;
        }
        uint32_t pkt_ts = g_pb_alaw_acc_ts - base_ms;
        int32_t av_delta = (int32_t)(pkt_ts - video_ts);
        if (av_delta > (int32_t)PB_AV_MAX_DIFF_MS || pkt_ts > elapsed_ms)
            return; /* 保留尚未到发送时间的音频，让视频继续推进。 */
        int stale = av_delta < -(int32_t)PB_AV_MAX_DIFF_MS;
        if (stale) {
            stat->dropped++;
        } else {
            int ret = TciSendPbFrame(g_pb.handle, TCMEDIA_AUDIO_G711A,
                                    g_pb_alaw_acc, PB_ALAW_PACK_BYTES, pkt_ts, 2);
            stat->last_ret = ret;
            if (ret == TCE_NETWORK_BUSY_VAL) {
                stat->busy++;
                return; /* 下次处理时重试，过期的音频包最终会被丢弃。 */
            }
            if (ret > 0) {
                *audio_ts = pkt_ts;
                *audio_valid = 1;
                g_pb_audio_ts = pkt_ts;
                stat->packets++;
            } else if (ret == 0) {
                stat->pause++;
            } else {
                stat->errors++;
            }
        }
        /* 即使发送失败，时间戳也按已消耗的样本数量推进。 */
        g_pb_alaw_acc_len -= PB_ALAW_PACK_BYTES;
        g_pb_alaw_acc_ts += PB_PACK_INTERVAL_MS;
        if (g_pb_alaw_acc_len)
            os_memmove(g_pb_alaw_acc, g_pb_alaw_acc + PB_ALAW_PACK_BYTES,
                       g_pb_alaw_acc_len);
        if (!stale) return; /* 每次处理最多发送一个音频包。 */
    }
}

/* 回放发送任务 */
static void pb_thread(void *arg)
{
    g_pb.thread_alive = 1;
    char full_path[96];
    char demux_name[24];     /* demux msi 名字, 每次切文件加序号避免同名冲突 */
    static uint32_t demux_seq = 0;

    /* 播放结束判定由 demux 的 MSI_VIDEO_DEMUX_GET_STATUS 提供。旧实现按空队列
     * 计数，注释按 5ms/次估算，但循环实际 sleep 20ms，造成约 4 秒切片空档。 */
    uint8_t  frames_consumed = 0;

    /* 音频时间戳与每次 seek/切片后的首个 I 帧重新对齐。 */
    g_pb_audio_ts = 0;
    /* 直到收到第一帧 I 帧才真正开始发送: 探鸽 SDK 对回放首帧是 I 帧才放行.
     * sync_sent 在每次 seek/切文件后清零, 碰到第一帧 I 帧时:
     *   1) 先发同步帧 (utc_time = 该 I 帧实际对应的 UTC 秒)
     *   2) 再发 [SC SPS][SC PPS][SC IDR] 的完整 I 帧
     * pending_sync_t0 是当前文件的 UTC 起点(文件名时间), 用于算 I 帧的 utc */
    uint8_t  sync_sent       = 0;
    uint8_t  sync_response   = 1;    /* 拖动定位时为 1，自动切换下一文件时为 0 */
    uint32_t pending_sync_t0 = 0;    /* 当前文件 t_start (文件名时间, UTC 秒) */
    uint32_t ts_base_ms      = 0;    /* 时间戳基准: 视频首 I 帧的 fb->time, 后续所有 ts 减去它归零
                                      * 目的: 保证音视频 ts 在同一基准, 且都从 0 开始 (同步帧 utc 对齐) */
    /* PTS pacing: 让回放速率严格按文件原始帧率发, 避免比实时快 N 倍把探鸽 P2P
     * 缓冲撑爆 (p2pSendPbStream congestion / SKB pool exhausted).
     * 同步基准: 首 I 帧那一刻的 wall clock. 每帧应当发送时刻 =
     *   wall_start_ms + (fb->time - ts_base_ms)
     * 还没到 → sleep 等; 到了/迟了 → 立刻发. seek/切文件时清零重新对齐. */
    uint32_t wall_start_ms   = 0;
    /* 软件丢帧开关: 正常情况用 SDK 原生 MSI_VIDEO_DEMUX_JMP_TIME seek，这里保持 0。 */
    uint32_t pending_jmp_ms  = 0;
    /* 音视频共用首 I 帧的 PTS 归零；保留真实 PTS 差，不伪造时间戳。
     * 最近发送的时间戳同时用于 100ms 音视频窗口和诊断。 */
    uint32_t sent_video_ts = 0;
    uint32_t sent_audio_ts = 0;
    uint8_t  sent_video_valid = 0;
    uint8_t  sent_audio_valid = 0;

    os_printf(KERN_INFO "pb_thread: start\n");

    /* PCM 由解码器帧池持有，这里只需要 2368 字节的音频累加缓冲。
     * 回放打包缓冲仍全部使用动态申请的 PSRAM。 */
    if (!g_pb_alaw_acc)
        g_pb_alaw_acc = (uint8_t *)_os_malloc_psram(PB_ALAW_ACC_BYTES);
    pb_aac_reset();
    if (!g_pb_alaw_acc)
        os_printf(KERN_ERR "pb: PSRAM audio accumulator alloc failed, video only\n");

    /* sd_pb_recv 是 rec_playback_init 建的常驻 msi, 循环外 find 一次保持引用,
     * 避免每轮循环进 msi_find (全局 mutex + 遍历链表) 白烧 CPU.
     * 退出时在循环后统一 msi_put. */
    struct msi *recv = msi_find("sd_pb_recv", 1);
    if (!recv) {
        os_printf(KERN_ERR "pb_thread: sd_pb_recv not found, exit\n");
        if (g_pb_alaw_acc) { _os_free_psram(g_pb_alaw_acc); g_pb_alaw_acc = NULL; }
        g_pb.state = PB_ST_IDLE;
        g_pb.thread_alive = 0;
        return;
    }

    /* 诊断计数: 每秒打印一次, 看视频/音频 fb 流量是否正常 */
    uint32_t pb_stat_t0 = pb_clock_ms();
    uint32_t pb_stat_v  = 0;   /* 视频 fb 数 */
    uint32_t pb_stat_a  = 0;   /* 音频 fb 数 */
    uint32_t pb_stat_busy = 0;
    pb_audio_stats_t audio_stat = {0};
    uint8_t pb_aac_first_logged = 0;
    uint32_t eof_wait_ms = 0;
    uint8_t eof_waiting = 0;
    uint32_t pause_start_ms = 0;
    uint8_t was_paused = 0;
    /* 同一文件最多自动恢复三次，不因偶尔吐出一帧就清零，避免坏点无限重播。 */
    const uint32_t PB_NO_FRAME_TIMEOUT_MS = 5000U;
    const uint32_t PB_RECOVER_MAX = 3U;
    uint32_t last_frame_ms = pb_clock_ms();
    uint32_t resume_pts_ms = 0;  /* 当前文件内最后成功发送的视频 PTS，不是 UTC。 */
    uint32_t recover_count = 0;
    uint8_t recover_pending = 0;

    /* 每轮让出 CPU，5ms 检查周期用于平滑发送 40ms 音频包。 */
    #define PB_LOOP_YIELD_MS  5

    while (!g_sd_formatting && !g_sd_fault_pending && g_pb.state != PB_ST_STOP_REQ && g_pb.state != PB_ST_IDLE) {

        /* APP 拖动优先。自动恢复只复用当前路径和毫秒 PTS，不改写 APP 的 seek 请求。 */
        if (g_pb.seek_req || (recover_pending && g_pb.state == PB_ST_PLAYING)) {
            uint8_t recovering = !g_pb.seek_req;
            uint32_t request_utc = g_pb.seek_t;
            recover_pending = 0;
            if (!recovering) {
                g_pb.seek_req = 0;
                recover_count = 0;
                sync_response = 1;
            } else {
                if (recover_count >= PB_RECOVER_MAX) {
                    os_printf(KERN_ERR "pb: recovery exhausted, stop; file preserved: %s\n", full_path);
                    TciSendPbEndOfEvent(g_pb.handle);
                    goto pb_exit;
                }
                ++recover_count;
                /* 已同步的播放恢复不是 PLAY_START 应答；未出首帧时保留原应答语义。 */
                if (sync_sent) sync_response = 0;
                os_printf(KERN_WARNING "pb: recover %u/%u file=%s pts=%ums\n",
                          recover_count, PB_RECOVER_MAX, full_path, resume_pts_ms);
            }
            frames_consumed = 0;
            sync_sent       = 0;
            ts_base_ms      = 0;
            g_pb_audio_ts   = 0;
            wall_start_ms   = 0;   /* pacing 基准重新对齐到下个首 I 帧 */
            sent_video_ts = sent_audio_ts = 0;
            sent_video_valid = sent_audio_valid = 0;
            pb_aac_first_logged = 0;
            os_memset(&audio_stat, 0, sizeof(audio_stat));
            eof_waiting = was_paused = 0;
            pb_aac_reset();        /* 丢弃跨 seek 的音频残留 */
            if (g_pb.demux_msi) {
                pb_aac_msi_destroy();   /* 先拆解码器: 它的 src_msi 指向这个 demux */
                msi_destroy(g_pb.demux_msi);
                g_pb.demux_msi = NULL;
            }
            /* 清空 sd_pb_recv 里残留的 fb (切文件时旧 fb 的 ts 属于上一个文件,
             * 留在队列里下轮发出会让 APP 收到乱序/重复 ts, 且旧 fb 占着 fbpool) */
            {
                struct framebuff *rfb;
                while ((rfb = msi_get_fb(recv, 0)) != NULL) {
                    msi_delete_fb(NULL, rfb);
                }
            }

            char new_date_dir[16];
            char new_fname[FILE_NAME_LEN + 1];
            uint32_t new_t0 = 0;
            if (recovering) {
                /* 先释放旧 FIL 和解码链，留出 SD 重新挂载的时间；等待可被停止/拖动打断。 */
                for (int n = 0; n < 100; ++n) {
                    if (g_pb.seek_req || g_pb.state != PB_ST_PLAYING) break;
                    os_sleep_ms(10);
                }
                if (g_pb.seek_req) goto pb_after_fb_handle;
                if (g_pb.state != PB_ST_PLAYING) {
                    recover_pending = 1;
                    goto pb_after_fb_handle;
                }
                os_strncpy(new_date_dir, g_pb.cur_date_dir, sizeof(new_date_dir));
                os_strncpy(new_fname, g_pb.cur_fname, sizeof(new_fname));
                new_t0 = pending_sync_t0;
            } else if (pb_locate_file(request_utc, new_date_dir, new_fname, &new_t0) != 0) {
                if (g_pb.seek_req) goto pb_after_fb_handle;
                os_printf(KERN_ERR "pb: seek %u lookup failed or no match\n", request_utc);
                TciSendPbEndOfEvent(g_pb.handle);
                break;
            }
            os_strncpy(g_pb.cur_date_dir, new_date_dir, sizeof(g_pb.cur_date_dir));
            os_strncpy(g_pb.cur_fname, new_fname, sizeof(g_pb.cur_fname));
            os_snprintf(full_path, sizeof(full_path), "%s/%s/%s",
                        REC_ROOT_PATH, new_date_dir, new_fname);

            /* 打开 demux, 失败 (例如断电残留的坏 MP4 没 moov box) 自动跳下一个文件,
             * 最多尝试 MAX_SKIP 次, 避免一长串坏文件导致死循环. */
            uint32_t jmp_ms = 0;
            {
                const int MAX_SKIP = 8;
                int skipped = 0;
                uint8_t skip_started = 0;   /* 0=当前是 seek 目标文件; 1=已经在跳坏文件 */
                while (1) {
                    /* 正在录制的文件 moov 尚未完整写入, 直接跳过 (不删). */
                    int is_recording = pb_is_current_recording_file(full_path);
                    if (!is_recording) {
                        os_snprintf(demux_name, sizeof(demux_name),
                                    "sd_pb_demux_%u", ++demux_seq);
                        int opened = pb_open_checked(demux_name, full_path);
                        if (opened > 0) break;
                        if (opened < 0) {
                            if (g_pb.seek_req) goto pb_after_fb_handle;
                            if (recovering) {
                                recover_pending = 1;
                                goto pb_after_fb_handle;
                            }
                            os_printf(KERN_ERR "pb: SD/open failure, stop playback; file preserved\n");
                            TciSendPbEndOfEvent(g_pb.handle);
                            goto pb_exit;
                        }
                    }
                    if (is_recording) {
                        os_printf(KERN_INFO "pb: skip current recording: %s\n", full_path);
                    } else {
                        os_printf(KERN_ERR "pb: invalid mp4, skip without deleting: %s (skip %d)\n",
                                  full_path, skipped);
                    }
                    if (++skipped >= MAX_SKIP) {
                        os_printf(KERN_ERR "pb: too many bad files, stop\n");
                        TciSendPbEndOfEvent(g_pb.handle);
                        goto pb_exit;
                    }
                    /* 严格按文件名查找后续文件，避免用仍落在当前坏文件覆盖范围内的
                     * 时间重复定位到同一文件。坏文件保留在卡上，不删除。 */
                    char nxt_date_dir[16];
                    char nxt_fname[FILE_NAME_LEN + 1];
                    uint32_t nxt_t0 = 0;
                    os_strncpy(nxt_date_dir, g_pb.cur_date_dir, sizeof(nxt_date_dir));
                    if (find_next_in_day(g_pb.cur_date_dir, g_pb.cur_fname,
                                         nxt_fname, &nxt_t0) != 0) {
                        os_printf(KERN_ERR "pb: next lookup failed or no later file, stop\n");
                        TciSendPbEndOfEvent(g_pb.handle);
                        goto pb_exit;
                    }
                    os_strncpy(g_pb.cur_date_dir, nxt_date_dir, sizeof(g_pb.cur_date_dir));
                    os_strncpy(g_pb.cur_fname, nxt_fname, sizeof(g_pb.cur_fname));
                    os_snprintf(full_path, sizeof(full_path), "%s/%s/%s",
                                REC_ROOT_PATH, nxt_date_dir, nxt_fname);
                    new_t0 = nxt_t0;
                    /* 已离开原文件，下一文件从头开始，恢复预算重新计算。 */
                    recovering = 0;
                    recover_count = 0;
                    skip_started = 1;
                }
                /* 若跳过了坏文件, seek 偏移语义失效 → 从文件头播 */
                if (skip_started) {
                    jmp_ms = 0;
                } else if (recovering) {
                    jmp_ms = resume_pts_ms;
                } else if (request_utc > new_t0) {
                    jmp_ms = (request_utc - new_t0) * 1000;
                }
            }
            resume_pts_ms = jmp_ms;
            /* 使用 SDK 原生 MSI_VIDEO_DEMUX_JMP_TIME: demux 内部直接定位到 jmp_ms
             * 对应的关键帧，不需要软件丢帧 + fast_output 绕行。 */
            pending_jmp_ms = 0;    /* 软件丢帧不再需要 */
            msi_add_output(g_pb.demux_msi, NULL, "sd_pb_recv");
            if (g_pb_alaw_acc && pb_aac_msi_create() != 0)
                os_printf(KERN_WARNING "pb: AAC decoder unavailable, video only\n");
            /* JMP_TIME 必须在 START 之前发, 否则 thread 已经在按 PTS 节奏跑了.
             * SDK 内部 set MP4_DEMUX_JMP 事件, thread 启动后走 JMP 分支定位 */
            if (jmp_ms > 0) {
                msi_do_cmd(g_pb.demux_msi, MSI_CMD_VIDEO_DEMUX_CTRL,
                           MSI_VIDEO_DEMUX_JMP_TIME, jmp_ms);
            }
            msi_do_cmd(g_pb.demux_msi, MSI_CMD_VIDEO_DEMUX_CTRL, MSI_VIDEO_DEMUX_START, 0);

            pending_sync_t0 = new_t0;
            last_frame_ms = pb_clock_ms();
            os_printf(KERN_INFO "pb: seek_t=%u, file_t0=%u, file=%s (waiting first I)\n",
                      request_utc, new_t0, full_path);
        }

        /* 暂停期间冻结发送计时，恢复后不突发发送积压帧。 */
        if (g_pb.state == PB_ST_PAUSED) {
            last_frame_ms = pb_clock_ms();  /* 暂停不计入无帧超时。 */
            if (!was_paused) {
                pause_start_ms = pb_clock_ms();
                was_paused = 1;
                if (g_pb.demux_msi)
                    msi_do_cmd(g_pb.demux_msi, MSI_CMD_VIDEO_DEMUX_CTRL, MSI_VIDEO_DEMUX_PAUSE, 0);
            }
            os_sleep_ms(10);
            continue;
        }
        if (was_paused) {
            last_frame_ms = pb_clock_ms();
            wall_start_ms += pb_clock_ms() - pause_start_ms;
            if (g_pb.demux_msi)
                msi_do_cmd(g_pb.demux_msi, MSI_CMD_VIDEO_DEMUX_CTRL, MSI_VIDEO_DEMUX_START, 0);
            was_paused = 0;
        }

        /* 取 fb (recv 已在循环外 find, 这里直接用) */
        struct framebuff *fb = msi_get_fb(recv, 0);

        if (!fb) {
            int demux_status = g_pb.demux_msi ?
                msi_do_cmd(g_pb.demux_msi, MSI_CMD_VIDEO_DEMUX_CTRL,
                           MSI_VIDEO_DEMUX_GET_STATUS, 0) : 0;
            /* IO 异常不能误判为播完；首次无帧也有超时，不能无限等首 I 帧。 */
            if (demux_status < 0 ||
                ((demux_status > 0 || !frames_consumed) &&
                 (uint32_t)(pb_clock_ms() - last_frame_ms) >= PB_NO_FRAME_TIMEOUT_MS)) {
                os_printf(KERN_WARNING "pb: stalled status=%d idle=%ums, schedule recovery\n",
                          demux_status, (unsigned)(pb_clock_ms() - last_frame_ms));
                recover_pending = 1;
                continue;
            }
            /* demux 仍在按 PTS 等待下一帧时，短暂空队列是正常现象；只有线程已经
             * 退出且下游队列也取空，才立即切下一个文件。 */
            if (demux_status > 0 || !frames_consumed) {
                goto pb_after_fb_handle;
            }
            /* 为异步解码的 PCM 尾部数据预留有限的发送时间。 */
            if (!eof_waiting) {
                eof_waiting = 1;
                eof_wait_ms = pb_clock_ms();
            }
            if ((uint32_t)(pb_clock_ms() - eof_wait_ms) < 200U)
                goto pb_after_fb_handle;
            eof_waiting = 0;
            /* 判定文件播完 */
            os_printf(KERN_INFO "pb: file end\n");
            if (g_pb.demux_msi) {
                pb_aac_msi_destroy();   /* 先拆解码器: 它的 src_msi 指向这个 demux */
                msi_destroy(g_pb.demux_msi);
                g_pb.demux_msi = NULL;
            }
            frames_consumed = 0;
            sync_sent       = 0;
            ts_base_ms      = 0;
            g_pb_audio_ts   = 0;
            wall_start_ms   = 0;   /* 切下一文件: pacing 基准重新对齐 */
            sent_video_ts = sent_audio_ts = 0;
            sent_video_valid = sent_audio_valid = 0;
            pb_aac_reset();        /* 跨文件: 丢弃音频残留 */
            /* 清空 sd_pb_recv 里残留的 fb */
            {
                struct framebuff *rfb;
                while ((rfb = msi_get_fb(recv, 0)) != NULL) {
                    msi_delete_fb(NULL, rfb);
                }
            }

            /* 关键: 用户 seek 命令优先于"自动切下一文件".
             * 如果 APP 刚好发来 PLAY_START, 让下一轮走 seek 分支处理, 避免
             * 这里创建新 demux 后又被 seek 分支立刻 destroy 造成 double-free 崩溃 */
            if (g_pb.seek_req) {
                continue;
            }

            if (g_pb.mode_bit == 1) {
                /* 事件模式: 通知结束并暂停 */
                TciSendPbEndOfEvent(g_pb.handle);
                g_pb.state = PB_ST_PAUSED;
                continue;
            }
            /* 连续模式: 在当前日期目录下找下一个. 碰到坏 MP4 (如断电残留没 moov)
             * 继续跳后续文件, 最多跳 MAX_SKIP 次避免死循环. */
            char next_fname[FILE_NAME_LEN + 1];
            uint32_t next_t0 = 0;
            const int MAX_SKIP = 8;
            int skipped = 0;
            g_pb.demux_msi = NULL;
            while (1) {
                if (find_next_in_day(g_pb.cur_date_dir, g_pb.cur_fname,
                                     next_fname, &next_t0) != 0) {
                    os_printf(KERN_INFO "pb: next lookup failed or no later file in %s, stop\n",
                              g_pb.cur_date_dir);
                    TciSendPbEndOfEvent(g_pb.handle);
                    goto pb_exit;
                }
                os_strncpy(g_pb.cur_fname, next_fname, sizeof(g_pb.cur_fname));
                os_snprintf(full_path, sizeof(full_path), "%s/%s/%s",
                            REC_ROOT_PATH, g_pb.cur_date_dir, next_fname);
                int is_recording = pb_is_current_recording_file(full_path);
                if (!is_recording) {
                    os_snprintf(demux_name, sizeof(demux_name),
                                "sd_pb_demux_%u", ++demux_seq);
                    int opened = pb_open_checked(demux_name, full_path);
                    if (opened > 0) break;
                    if (opened < 0) {
                        if (g_pb.seek_req) goto pb_after_fb_handle;
                        os_printf(KERN_ERR "pb: SD/open failure, stop playback; file preserved\n");
                        TciSendPbEndOfEvent(g_pb.handle);
                        goto pb_exit;
                    }
                }
                if (is_recording) {
                    os_printf(KERN_INFO "pb: reached current recording %s, stop\n", full_path);
                    TciSendPbEndOfEvent(g_pb.handle);
                    goto pb_exit;
                }
                os_printf(KERN_ERR "pb: invalid mp4, skip without deleting: %s (skip %d)\n",
                          full_path, skipped);
                if (++skipped >= MAX_SKIP) {
                    os_printf(KERN_ERR "pb: too many bad files, stop\n");
                    TciSendPbEndOfEvent(g_pb.handle);
                    goto pb_exit;
                }
            }
            msi_add_output(g_pb.demux_msi, NULL, "sd_pb_recv");
            if (g_pb_alaw_acc && pb_aac_msi_create() != 0)
                os_printf(KERN_WARNING "pb: AAC decoder unavailable, video only\n");
            msi_do_cmd(g_pb.demux_msi, MSI_CMD_VIDEO_DEMUX_CTRL, MSI_VIDEO_DEMUX_START, 0);
            /* 同步帧等第一帧 I 帧到达再发, is_response_to_PLAY_START=0.
             * 自动切下一文件不涉及 seek, pending_jmp_ms 清零从头播 */
            pending_sync_t0 = next_t0;
            resume_pts_ms = 0;
            recover_count = 0;
            last_frame_ms = pb_clock_ms();
            sync_response  = 0;
            pending_jmp_ms  = 0;
            pb_aac_first_logged = 0;
            os_memset(&audio_stat, 0, sizeof(audio_stat));
            os_printf(KERN_INFO "pb: next file %s (waiting first I)\n", full_path);
            continue;
        }

        /* 取到 fb, 重置空闲计数并标记已消费 */
        frames_consumed = 1;
        last_frame_ms = pb_clock_ms();
        eof_waiting = 0;

        /* 诊断计数 */
        if (fb->mtype == F_H264) pb_stat_v++;
        else if (fb->mtype == F_AUDIO) pb_stat_a++;

        /* 探鸽 SDK 对回放流的首帧要求必须是 I 帧才放行, 所以:
         * 1) sync_sent=0 时, 非 I 帧的视频帧 和 所有音频帧 全部丢弃
         * 2) 软件 seek: fb->time < pending_jmp_ms 的 I 帧也丢, 等到时间达标的第一帧 I
         * 3) 收到符合的第一帧 I 帧 -> 发送同步时间帧, sync_sent=1，对齐音频起点
         * 4) 之后正常发送视频 P 帧 + 音频 */
        if (!sync_sent) {
            if (fb->mtype == F_H264) {
                struct fb_h264_s *priv = (struct fb_h264_s *) fb->priv;
                if (!priv || priv->type != 1) {
                    msi_delete_fb(NULL, fb);
                    continue;
                }
                /* 防御性保留: SDK JMP_TIME 正常时 pending_jmp_ms=0, 此处不触发.
                 * 万一未来有需要软件丢帧的场景 (如 SDK JMP_TIME 又挂了) 可直接启用 */
                if (pending_jmp_ms > 0 && fb->time < pending_jmp_ms) {
                    msi_delete_fb(NULL, fb);
                    continue;
                }
                /* I 帧到达且时间戳 >= pending_jmp_ms, 触发同步 */
                uint32_t first_utc = pending_sync_t0 + fb->time / 1000;
                TciSendPbSyncFrame(g_pb.handle, first_utc, sync_response);
                sync_sent = 1;
                ts_base_ms = fb->time;
                /* PTS pacing 基准点: 首 I 帧此刻的 wall clock, 后续帧按
                 * (fb->time - ts_base_ms) 偏移到 wall_start_ms 上发送 */
                wall_start_ms = pb_clock_ms();
                g_pb_audio_ts = 0;   /* 音频 ts 从同步点重新起算 */
                sent_video_ts = sent_audio_ts = 0;
                sent_video_valid = sent_audio_valid = 0;
                pb_aac_reset();      /* 同步点: 从干净状态重新开始累计 */
                os_printf(KERN_INFO "pb: sync_sent utc=%u (file_t0=%u + fb_time=%u ms, ts_base=%u)\n",
                          first_utc, pending_sync_t0, fb->time, ts_base_ms);
                /* 继续走下面的发送逻辑, 不 continue */
            } else {
                /* 还没首 I, 丢掉音频 */
                msi_delete_fb(NULL, fb);
                continue;
            }
        }

        /* 按速度决定是否发送 */
        int skip = 0;
        if (g_pb.speed == PB_SPEED_IKEY) {
            if (fb->mtype == F_H264) {
                struct fb_h264_s *priv = (struct fb_h264_s *) fb->priv;
                if (!priv || priv->type != 1) skip = 1;
            } else {
                skip = 1;   /* 只关键帧模式不发音频 */
            }
        }

        if (!skip) {
            if (fb->mtype == F_H264) {
                struct fb_h264_s *priv = (struct fb_h264_s *) fb->priv;
                int flags = (priv && priv->type == 1) ? FF_KEYFRAME : 0;
                /* 直接使用 MP4 文件里读出的 PTS(相对同步帧归零), 不做任何改写.
                 * 视频与音频共用同一个 ts_base_ms, 保证文件里真实的音视频
                 * 相对关系被原样送出. */
                uint32_t send_ts = fb->time >= ts_base_ms ? fb->time - ts_base_ms : 0;
                /* 等待视频帧发送时刻时，继续处理每包 40ms 的音频数据。 */
                while (send_ts > (uint32_t)(pb_clock_ms() - wall_start_ms) &&
                       g_pb.state == PB_ST_PLAYING && !g_pb.seek_req) {
                    pb_audio_pump(ts_base_ms, pb_clock_ms() - wall_start_ms,
                                  sent_video_ts, sent_video_valid,
                                  &sent_audio_ts, &sent_audio_valid, &audio_stat);
                    os_sleep_ms(PB_LOOP_YIELD_MS);
                }
                if (g_pb.seek_req || g_pb.state != PB_ST_PLAYING) {
                    msi_delete_fb(NULL, fb);
                    goto pb_after_fb_handle;
                }

                /* 有效音频不能落后下一视频帧超过 100ms。
                 * 最多等待解码 100ms，音频缺失不能导致视频卡死。
                 * 音频恢复后按当前 PTS 重新对齐。 */
                uint32_t av_wait_start = pb_clock_ms();
                while (g_pb.speed == PB_SPEED_1X && sent_audio_valid &&
                       (int32_t)(send_ts - sent_audio_ts) > (int32_t)PB_AV_MAX_DIFF_MS &&
                       g_pb.state == PB_ST_PLAYING && !g_pb.seek_req) {
                    pb_audio_pump(ts_base_ms, pb_clock_ms() - wall_start_ms,
                                  sent_video_ts, sent_video_valid,
                                  &sent_audio_ts, &sent_audio_valid, &audio_stat);
                    if ((int32_t)(send_ts - sent_audio_ts) <= (int32_t)PB_AV_MAX_DIFF_MS)
                        break;
                    if ((uint32_t)(pb_clock_ms() - av_wait_start) >= PB_AV_MAX_DIFF_MS) {
                        sent_audio_valid = 0;
                        os_printf(KERN_WARNING "pb: audio stalled, continue video and resync audio\n");
                        break;
                    }
                    os_sleep_ms(PB_LOOP_YIELD_MS);
                }
                if (g_pb.seek_req || g_pb.state != PB_ST_PLAYING) {
                    msi_delete_fb(NULL, fb);
                    goto pb_after_fb_handle;
                }

                /* BUSY 重试: 不丢帧, sleep PB_BUSY_SLEEP_MS 后重发同一帧.
                 * 循环中同时检查 state/seek_req, 避免连接断时死循环. */
                int retry = 0;
                int send_ret;
                while (1) {
                    send_ret = TciSendPbFrame(g_pb.handle, TCMEDIA_VIDEO_H264,
                                              fb->data, fb->len, send_ts, flags);
                    if (send_ret != TCE_NETWORK_BUSY_VAL) break;
                    pb_stat_busy++;
                    if (++retry >= PB_BUSY_MAX_RETRY ||
                        g_pb.state == PB_ST_STOP_REQ || g_pb.state == PB_ST_IDLE ||
                        g_pb.seek_req) {
                        os_printf(KERN_WARNING "pb: video BUSY give up after %d retries\n", retry);
                        break;
                    }
                    for (int waited = 0; waited < PB_BUSY_SLEEP_MS; waited += PB_LOOP_YIELD_MS) {
                        if (g_pb.seek_req || g_pb.state != PB_ST_PLAYING) break;
                        pb_audio_pump(ts_base_ms, pb_clock_ms() - wall_start_ms,
                                      sent_video_ts, sent_video_valid,
                                      &sent_audio_ts, &sent_audio_valid, &audio_stat);
                        os_sleep_ms(PB_LOOP_YIELD_MS);
                    }
                    if (g_pb.seek_req || g_pb.state != PB_ST_PLAYING) break;
                }
                if (send_ret > 0) {
                    sent_video_ts = send_ts;
                    sent_video_valid = 1;
                    /* 保存文件内的绝对 PTS；重建后重新发秒单位同步帧，再归零音视频时间戳。 */
                    resume_pts_ms = fb->time;
                }

            }
            /* 解码器已订阅 AAC 数据。下方每轮循环都处理 PCM，
             * 不能只在新 AAC 帧到达时才处理。 */
        }

        msi_delete_fb(NULL, fb);

pb_after_fb_handle:
        if (sync_sent) {
            pb_audio_pump(ts_base_ms, pb_clock_ms() - wall_start_ms,
                          sent_video_ts, sent_video_valid,
                          &sent_audio_ts, &sent_audio_valid, &audio_stat);
            if (!pb_aac_first_logged && audio_stat.packets) {
                os_printf(KERN_INFO "pb: AAC->G711A first ret=%d v_ts=%u a_ts=%u\n",
                          audio_stat.last_ret, sent_video_ts, sent_audio_ts);
                pb_aac_first_logged = 1;
            }
        }
        /* 每秒打印一次诊断 */
        if ((uint32_t)(pb_clock_ms() - pb_stat_t0) >= 1000) {
            int32_t av_diff_ms = 0;
            if (sent_video_valid && sent_audio_valid)
                av_diff_ms = (int32_t)sent_video_ts - (int32_t)sent_audio_ts;
            os_printf(KERN_INFO
                      "pb stat: v_fb=%u a_fb=%u a_ok=%u pause=%u err=%u busy=%u last_aret=%d v_ts=%u a_ts=%u diff=%dms dec=%u drop=%u\n",
                      pb_stat_v, pb_stat_a, audio_stat.packets, audio_stat.pause,
                      audio_stat.errors, pb_stat_busy + audio_stat.busy, audio_stat.last_ret,
                      sent_video_ts, sent_audio_ts, (int)av_diff_ms,
                      audio_stat.decoded, audio_stat.dropped);
            pb_stat_v = pb_stat_a = pb_stat_busy = 0;
            os_memset(&audio_stat, 0, sizeof(audio_stat));
            pb_stat_t0 = pb_clock_ms();
        }

        /* 保险 C: 无条件让出 CPU, 无论前面处理了多久 */
        os_sleep_ms(PB_LOOP_YIELD_MS);
    }

pb_exit:
    os_mutex_lock(&g_pb.lock, osWaitForever);
    g_pb.state = PB_ST_STOP_REQ;
    os_mutex_unlock(&g_pb.lock);
    if (g_pb_alaw_acc) { _os_free_psram(g_pb_alaw_acc); g_pb_alaw_acc = NULL; }
    pb_aac_reset();

    /* 清理: 只销毁 demux msi; sd_pb_recv 是 rec_playback_init 创建的
     * 常驻 msi, 不销毁以避免同名冲突 */
    if (g_pb.demux_msi) {
        pb_aac_msi_destroy();   /* 先拆解码器: 它的 src_msi 指向这个 demux */
        msi_destroy(g_pb.demux_msi);
        g_pb.demux_msi = NULL;
    }
    /* STOP 只发出退出请求，底层可能仍在读卡。排空后才能宣布回放空闲，
     * 否则删除旧文件/恢复挂载会越过仍持有文件的 worker。
     * 等待期间持续回收队列，防止输出池满阻碍底层退出。 */
    uint32_t close_log_ms = pb_clock_ms();
    while (mp4_demux_active_workers()) {
        struct framebuff *pending;
        while ((pending = msi_get_fb(recv, 0)) != NULL)
            msi_delete_fb(NULL, pending);
        if ((uint32_t)(pb_clock_ms() - close_log_ms) >= 3000U) {
            close_log_ms = pb_clock_ms();
            os_printf(KERN_WARNING "pb_stop: waiting demux IO close\n");
        }
        os_sleep_ms(10);
    }
    /* 清空 sd_pb_recv 里残留的 fb (下次 pb_start 看到旧帧会混乱), 复用循环外的 recv */
    {
        struct framebuff *fb;
        while ((fb = msi_get_fb(recv, 0)) != NULL) {
            msi_delete_fb(NULL, fb);
        }
    }
    msi_put(recv);
    os_mutex_lock(&g_pb.lock, osWaitForever);
    g_pb.state = PB_ST_IDLE;
    g_pb.thread_alive = 0;
    os_mutex_unlock(&g_pb.lock);
    os_printf(KERN_INFO "pb_thread: exit\n");
}

static int pb_start_raw(void *handle, uint32_t t_seek, uint8_t mode_bit);
int pb_start(void *handle, uint32_t t_seek, uint8_t mode_bit)
{
    if (rec_sd_enter()) return -1;
    /* 与删除旧文件互斥，先发布回放占用标记再允许回收线程继续。 */
    os_mutex_lock(&g_recycle_lock, osWaitForever);
    int ret = g_sd_formatting ? -1 : pb_start_raw(handle, t_seek, mode_bit);
    os_mutex_unlock(&g_recycle_lock);
    rec_sd_leave();
    return ret;
}
static int pb_start_raw(void *handle, uint32_t t_seek, uint8_t mode_bit)
{
    if (!g_rp_inited) return -1;

    /* 懒初始化 g_pb.lock */
    static uint8_t pb_lock_inited = 0;
    if (!pb_lock_inited) {
        os_mutex_init(&g_pb.lock);
        pb_lock_inited = 1;
    }

    os_mutex_lock(&g_pb.lock, -1);

    if (g_pb.thread_alive && g_pb.state == PB_ST_STOP_REQ) {
        os_mutex_unlock(&g_pb.lock);
        return -1; /* 旧会话未排空时不能用 seek 把停止状态改回播放。 */
    }

    if (g_pb.thread_alive) {
        /* 已有线程: 触发 seek */
        g_pb.handle = (p2phandle_t) handle;
        g_pb.seek_t = t_seek;
        g_pb.seek_req = 1;
        g_pb.mode_bit = mode_bit;
        g_pb.state = PB_ST_PLAYING;
        os_mutex_unlock(&g_pb.lock);
        os_printf(KERN_INFO "pb_start: reseek to %u\n", t_seek);
        return 0;
    }

    g_pb.handle = (p2phandle_t) handle;
    g_pb.seek_t = t_seek;
    g_pb.seek_req = 1;
    g_pb.mode_bit = mode_bit;
    g_pb.speed = PB_SPEED_1X;
    g_pb.state = PB_ST_PLAYING;
    g_pb.demux_msi = NULL;

    /* sd_pb_recv 已在 rec_playback_init 创建, 这里不再重建 */

    /* 栈交给 OS 自动分配 (stack=NULL 触发 krhino_task_dyn_create): 走 kernel
     * 系统堆 (SRAM), task 自然 return 时 OS 自动回收, 不会泄漏.
     *
     * 8KB: 原先 4KB 只够 FatFS + mp4_demux + TciSendPbFrame. 现在 AAC 解码
     * (audio_decode_data) 是内联跑在本线程栈上的 —— SDK 自己的 aac_decode
     * 线程单独就要 2KB 栈, 两者叠加会溢出. 栈溢出会静默打死本线程
     * (日志表现为 pb_thread 突然不再输出任何信息). */
    g_pb.thread_alive = 1; /* 创建前发布，格式化不能错过尚未被调度的任务。 */
    void *hdl = os_task_create("pb_thread", (os_task_func_t)pb_thread, NULL,
                               OS_TASK_PRIORITY_NORMAL, 0, NULL, 8192);
    if (!hdl) {
        g_pb.thread_alive = 0;
        g_pb.state = PB_ST_IDLE;
        os_mutex_unlock(&g_pb.lock);
        return -1;
    }

    os_mutex_unlock(&g_pb.lock);
    return 0;
}

int pb_stop(void)
{
    if (!g_pb.thread_alive) return mp4_demux_active_workers() ? -1 : 0;
    os_mutex_lock(&g_pb.lock, osWaitForever);
    if (g_pb.thread_alive) g_pb.state = PB_ST_STOP_REQ;
    os_mutex_unlock(&g_pb.lock);
    int wait = 0;
    while (g_pb.thread_alive && wait++ < 100)
        os_sleep_ms(10);
    if (rec_pb_is_active()) {
        os_printf(KERN_WARNING "pb_stop: pending, SD handles still active\n");
        return -1;
    }
    return 0;
}

void pb_session_close(void *handle)
{
    if (!g_pb.thread_alive) return;
    os_mutex_lock(&g_pb.lock, osWaitForever);
    if (g_pb.thread_alive && g_pb.handle == (p2phandle_t)handle)
        g_pb.state = PB_ST_STOP_REQ;
    os_mutex_unlock(&g_pb.lock);
}

int pb_pause(void)
{
    if (!g_pb.thread_alive) return 0;
    os_mutex_lock(&g_pb.lock, osWaitForever);
    if (g_pb.state == PB_ST_PLAYING) {
        g_pb.state = PB_ST_PAUSED;
        os_printf(KERN_INFO "pb_pause\n");
    }
    os_mutex_unlock(&g_pb.lock);
    return 0;
}

int pb_resume(void)
{
    if (!g_pb.thread_alive) return 0;
    os_mutex_lock(&g_pb.lock, osWaitForever);
    if (g_pb.state == PB_ST_PAUSED) {
        g_pb.state = PB_ST_PLAYING;
        os_printf(KERN_INFO "pb_resume\n");
    }
    os_mutex_unlock(&g_pb.lock);
    return 0;
}

int pb_set_forward(uint32_t param)
{
    if (!g_pb.thread_alive) return -1;
    os_mutex_lock(&g_pb.lock, osWaitForever);
    if (g_pb.state == PB_ST_STOP_REQ) {
        os_mutex_unlock(&g_pb.lock);
        return -1;
    }
    if (param == 0) g_pb.speed = PB_SPEED_1X;
    else if (param == 1) g_pb.speed = PB_SPEED_2X;
    else if (param == 2) g_pb.speed = PB_SPEED_4X;
    else g_pb.speed = PB_SPEED_IKEY;

    /* APP 的交互: PAUSE -> FORWARD 希望从暂停点按新倍速继续播, 所以:
     *   - 若当前处于 PAUSED, 自动切回 PLAYING, 从暂停位置继续
     *   - 若已在 PLAYING, 仅切倍速
     * 不做任何 seek, 文件偏移 / demux 状态保持不变 */
    if (g_pb.state == PB_ST_PAUSED) {
        g_pb.state = PB_ST_PLAYING;
        os_printf(KERN_INFO "pb_set_forward: speed=%d, resume from pause\n", g_pb.speed);
    } else {
        os_printf(KERN_INFO "pb_set_forward: speed=%d\n", g_pb.speed);
    }
    os_mutex_unlock(&g_pb.lock);
    return 0;
}
